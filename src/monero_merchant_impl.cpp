#include "monero_merchant_impl.h"
#include "invoices/incoming_monitor.h"
#include "invoices/incoming_payments.h"
#include "invoices/invoice_store.h"
#include "invoices/merchant_amounts.h"
#include "logos_sdk.h"
#include "pos/pos_delivery.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <vector>

namespace
{
std::string error(const char* code, const char* message)
{
    return nlohmann::json{{"ok", false}, {"code", code}, {"error", message}}.dump();
}

std::string walletReply(const std::string& raw, const logos::CallError& callError)
{
    if (!callError.ok())
    {
        return nlohmann::json{{"ok", false},
                              {"code", callError.code},
                              {"error", callError.message},
                              {"origin", callError.origin}}
            .dump();
    }
    const auto reply = nlohmann::json::parse(raw, nullptr, false);
    if (!reply.is_object() || !reply.contains("ok") || !reply["ok"].is_boolean())
    {
        return error("invalid_wallet_response",
                     "The wallet backend returned invalid JSON or no boolean ok flag.");
    }
    return raw;
}

std::string invoiceReply(const nlohmann::json& invoice, bool reused = false)
{
    nlohmann::json reply{{"ok", true}, {"invoice", invoice}};
    if (reused)
        reply["reused"] = true;
    return reply.dump();
}

nlohmann::json findInvoiceAddress(const nlohmann::json& addresses, const std::string& label)
{
    nlohmann::json found;
    for (const auto& row : addresses["subaddresses"])
    {
        if (row.value("label", "") != label)
            continue;
        if (!found.is_null())
            throw merchant::InvoiceError(
                "address_conflict",
                "Multiple addresses have this invoice label; review them in the wallet application.");
        found = row;
    }
    return found;
}

bool addressAlreadyUsed(const nlohmann::json& addresses, const nlohmann::json& address)
{
    return std::any_of(
        addresses["subaddresses"].begin(), addresses["subaddresses"].end(), [&](const auto& old)
        { return old.at("index") == address.at("index") || old.at("address") == address.at("address"); });
}

void validateInvoiceAddress(const nlohmann::json& address, const nlohmann::json& invoice,
                            const nlohmann::json& created)
{
    if (!address.contains("index") || !address["index"].is_number_integer() || address["index"] <= 0 ||
        address["index"] > UINT32_MAX || !address.contains("address") || !address["address"].is_string() ||
        address["address"] == "" || address["address"] == invoice["wallet_address"])
        throw merchant::InvoiceError("invalid_address", "The receiving subaddress response was invalid.");
    if (created.is_object() && created.value("ok", false) &&
        (created.value("address", "") != address["address"] ||
         created.value("index", nlohmann::json()) != address["index"]))
        throw merchant::InvoiceError("address_conflict",
                                     "Created address did not match the independent address list.");
}

} // namespace

std::string MoneroMerchantImpl::getWalletStatus()
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    logos::CallError callError;
    const auto reply = modules().monero_wallet_backend.wallet_status(&callError);
    return walletReply(reply, callError);
}

MoneroMerchantImpl::~MoneroMerchantImpl()
{
    aboutToUnload();
}

void MoneroMerchantImpl::onContextReady()
{
    try
    {
        invoices_ = std::make_shared<merchant::InvoiceStore>(instancePersistencePath());
        invoiceStorageError_.clear();
    }
    catch (const std::exception& e)
    {
        invoiceStorageError_ = e.what();
    }
    incoming_ = makeIncomingMonitor(
        [this](const std::string& method, IncomingMonitor::Completion done)
        {
            auto completion = [done = std::move(done)](logos::AsyncResult<std::string> result)
            { done(walletReply(result.value, result.error)); };
            if (method == "wallet_status")
                modules().monero_wallet_backend.wallet_statusAsyncResult(std::move(completion), 5000);
            else
                modules().monero_wallet_backend.historyAsyncResult(std::move(completion), 10000);
        },
        [this](const std::string& snapshot)
        {
            std::lock_guard<std::recursive_mutex> lock(stateMutex_);
            if (invoices_)
            {
                try
                {
                    invoices_->reconcile(nlohmann::json::parse(snapshot));
                    invoiceStorageError_.clear();
                }
                catch (const std::exception& e)
                {
                    invoiceStorageError_ = e.what();
                }
            }
            // Commit reconciliation before publishing invoice state.
            publishInvoices();
        });

    const std::weak_ptr<IncomingMonitor> weak = incoming_;
    const auto changed = [weak](const std::string&)
    {
        if (auto monitor = weak.lock())
            monitor->refresh();
    };
    auto* wallet = &modules().monero_wallet_backend;
    auto handles = std::make_shared<std::vector<logos::SubHandle>>();
    handles->push_back(wallet->onBalance_changed(changed));
    handles->push_back(wallet->onSync_progress(changed));
    handles->push_back(wallet->onWallet_state_changed(changed));
    wallet->onSubscriptionStatus(
        [weak](logos::SubStatus, std::uint64_t)
        {
            // Reconcile after arming/reconnection; events are not a durable queue.
            if (auto monitor = weak.lock())
                monitor->refresh();
        });
    cancelSubscriptions_ = [wallet, handles]() mutable
    {
        wallet->onSubscriptionStatus({});
        for (const auto& handle : *handles)
            handle.cancel();
    };
    try
    {
        pos_ = merchant::pos::startDelivery(
            modules(), instancePersistencePath(),
            [this](const std::string& method, const nlohmann::json& args)
            {
                if (method == "getWalletStatus")
                    return nlohmann::json::parse(getWalletStatus());
                if (method == "getInvoices")
                    return nlohmann::json::parse(getInvoices());
                if (method == "createPosInvoice" && args.size() == 4)
                    return nlohmann::json::parse(createInvoiceForWallet(args[0], args[1], args[2], args[3]));
                throw std::runtime_error("Unsupported POS backend operation");
            });
        posError_.clear();
    }
    catch (...)
    {
        posError_ = "POS service could not start. Check private state permissions and ensure only one "
                    "merchant instance uses this state directory.";
    }
}

LogosShutdown MoneroMerchantImpl::aboutToUnload()
{
    if (pos_)
    {
        pos_->stop();
        pos_.reset();
    }
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    if (cancelSubscriptions_)
    {
        cancelSubscriptions_();
        cancelSubscriptions_ = {};
    }
    if (incoming_)
        incoming_->stop();
    invoices_.reset();
    return LogosShutdown::Synchronous;
}

std::string MoneroMerchantImpl::getInvoices()
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    if (!invoices_)
        return error("storage_unavailable", invoiceStorageError_.empty() ? "Invoice storage is not ready."
                                                                         : invoiceStorageError_.c_str());
    try
    {
        const auto monitor =
            incoming_ ? nlohmann::json::parse(incoming_->snapshot()) : nlohmann::json::object();
        auto result = invoices_->snapshot(monitor);
        if (!invoiceStorageError_.empty())
        {
            result["ok"] = false;
            result["stale"] = true;
            result["error"] = invoiceStorageError_;
            result["code"] = "reconciliation_error";
            for (auto& invoice : result["invoices"])
                invoice["stale"] = true;
        }
        return result.dump();
    }
    catch (const std::exception& e)
    {
        return error("storage_error", e.what());
    }
}

void MoneroMerchantImpl::publishInvoices()
{
    invoicesChanged(getInvoices());
}
std::string MoneroMerchantImpl::refreshInvoices()
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    if (!incoming_)
        return error("not_ready", "Payment monitoring is starting.");
    incoming_->refresh();
    return R"({"ok":true,"scheduled":true})";
}

std::string MoneroMerchantImpl::setConfirmationPolicy(int64_t confirmations)
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    if (!invoices_)
        return getInvoices();
    try
    {
        invoices_->setConfirmationPolicy(confirmations);
        publishInvoices();
        return nlohmann::json{{"ok", true}, {"confirmation_policy", invoices_->confirmationPolicy()}}.dump();
    }
    catch (const merchant::InvoiceError& e)
    {
        return error(e.code.c_str(), e.what());
    }
    catch (const std::exception& e)
    {
        return error("storage_error", e.what());
    }
}

std::string MoneroMerchantImpl::createInvoice(const std::string& requestId, const std::string& title,
                                              const std::string& amountXmr)
{
    return createInvoiceForWallet(requestId, title, amountXmr, {});
}

std::string MoneroMerchantImpl::createInvoiceForWallet(const std::string& requestId, const std::string& title,
                                                       const std::string& amountXmr,
                                                       const std::string& expectedWallet)
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    if (!invoices_)
        return getInvoices();
    std::string amount;
    try
    {
        amount = merchant::parseXmrAmount(amountXmr);
    }
    catch (const std::exception& e)
    {
        return error("invalid_amount", e.what());
    }
    try
    {
        const auto existing = invoices_->find(requestId);
        if (!existing.is_null())
        {
            if (!expectedWallet.empty() && existing["wallet_id"] != expectedWallet)
                return error("wallet_changed", "Return to the original POS order wallet.");
            if (existing["amount_due_atomic"] != amount || existing["title"] != title)
                return error("request_conflict", "This request ID already belongs to a different invoice.");
            if (existing["creation_state"] == "ready")
                return invoiceReply(existing, true);
            return finishInvoiceAddress(requestId, !existing["allocation_attempted"].get<bool>());
        }
        const auto raw = getWalletStatus();
        const auto status = nlohmann::json::parse(raw);
        if (!status.value("ok", false))
            return raw;
        if (!status.contains("wallet") || !status["wallet"].is_string() || status["wallet"] == "")
            return error("no_wallet", "Open a Stagenet wallet before creating an invoice.");
        if (status.value("network", "") != "stagenet")
            return error("wrong_network", "Invoices currently require Stagenet.");
        if (!expectedWallet.empty())
        {
            if (merchant::walletIdentity(status) != expectedWallet)
                return error(
                    "wallet_changed",
                    "Merchant wallet changed before invoice reservation; retry in the original wallet.");
            if (!status.value("connected", false) || !status.value("synchronized", false))
                return error("wallet_unavailable", "Wait for a connected, synchronized merchant wallet.");
        }
        (void)invoices_->reserve(requestId, title, amount, status);
        return finishInvoiceAddress(requestId, true);
    }
    catch (const merchant::InvoiceError& e)
    {
        return error(e.code.c_str(), e.what());
    }
    catch (const std::exception& e)
    {
        return error("invoice_error", e.what());
    }
}

std::string MoneroMerchantImpl::recoverInvoice(const std::string& invoiceId)
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    if (!invoices_)
        return getInvoices();
    return finishInvoiceAddress(invoiceId, false);
}

std::string MoneroMerchantImpl::finishInvoiceAddress(const std::string& id, bool allocate)
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    using Json = nlohmann::json;
    try
    {
        auto invoice = invoices_->find(id);
        if (invoice.is_null())
            return error("not_found", "Invoice does not exist.");
        if (invoice["creation_state"] == "ready")
            return invoiceReply(invoice, true);
        const auto label = "Merchant invoice " + id;
        auto* wallet = &modules().monero_wallet_backend;
        const auto status = [&]()
        {
            logos::CallError callError;
            auto value = Json::parse(walletReply(wallet->wallet_status(&callError, 5000), callError));
            if (!value.value("ok", false))
                throw merchant::InvoiceError("wallet_error",
                                             value.value("error", "Wallet status unavailable."));
            if (merchant::walletIdentity(value) != invoice["wallet_id"] || value.value("wallet", "").empty())
                throw merchant::InvoiceError("wallet_changed",
                                             "Open the original invoice wallet to recover its address.");
            return value;
        };
        const auto receiving = [&]()
        {
            logos::CallError callError;
            auto value = Json::parse(walletReply(wallet->receive_info(0, &callError, 5000), callError));
            if (!value.value("ok", false) || value.value("address", "") != invoice["wallet_address"] ||
                !value.contains("subaddresses") || !value["subaddresses"].is_array())
                throw merchant::InvoiceError(
                    "receive_unavailable",
                    "Receiving addresses could not be verified for the invoice wallet.");
            return value;
        };
        (void)status();
        const auto before = receiving();
        auto found = findInvoiceAddress(before, label);
        if (!found.is_null() && !invoice["allocation_attempted"].get<bool>())
            throw merchant::InvoiceError("address_conflict", "An existing address already uses this invoice "
                                                             "label. It cannot be reused for a new invoice.");
        Json created;
        if (found.is_null() && allocate && !invoice["allocation_attempted"].get<bool>())
        {
            (void)status();
            // Persist intent BEFORE the mutation. A restart can only reconcile
            // this request by its label; it must never blindly create again.
            invoices_->markAttempted(id);
            logos::CallError callError;
            created =
                Json::parse(walletReply(wallet->create_subaddress(0, label, &callError, 10000), callError));
            found = findInvoiceAddress(receiving(), label);
            if (!found.is_null() && addressAlreadyUsed(before, found))
                throw merchant::InvoiceError("address_conflict",
                                             "The wallet returned an address that was already in use.");
        }
        (void)status();
        if (found.is_null())
            throw merchant::InvoiceError(
                "address_recovery_required",
                "No unique receiving address was found. Check the original wallet and recover this invoice; "
                "allocation is not retried automatically.");
        validateInvoiceAddress(found, invoice, created);
        invoices_->bind(id, found["index"].get<uint32_t>(), found["address"]);
        publishInvoices();
        if (incoming_)
            incoming_->refresh();
        return invoiceReply(invoices_->find(id));
    }
    catch (const std::exception& e)
    {
        try
        {
            invoices_->recoveryRequired(id, e.what());
            publishInvoices();
        }
        catch (...)
        {
        }
        const auto* typed = dynamic_cast<const merchant::InvoiceError*>(&e);
        return Json{{"ok", false},
                    {"code", typed ? typed->code : "invoice_error"},
                    {"error", e.what()},
                    {"invoice_id", id},
                    {"recovery_required", true}}
            .dump();
    }
}

std::string MoneroMerchantImpl::acknowledgeInvoiceReview(const std::string& invoiceId)
{
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    if (!invoices_)
        return getInvoices();
    try
    {
        invoices_->acknowledgeReview(invoiceId);
        publishInvoices();
        return R"({"ok":true})";
    }
    catch (const merchant::InvoiceError& e)
    {
        return error(e.code.c_str(), e.what());
    }
    catch (const std::exception& e)
    {
        return error("storage_error", e.what());
    }
}

std::string MoneroMerchantImpl::getPosPairing()
{
    if (!pos_)
        return error("pos_unavailable", posError_.empty() ? "POS service is starting." : posError_.c_str());
    try
    {
        return pos_->snapshot().dump();
    }
    catch (...)
    {
        return error("pos_storage", "Cannot read POS terminal records.");
    }
}
std::string MoneroMerchantImpl::createPosInvitation()
{
    if (!pos_)
        return getPosPairing();
    try
    {
        return pos_->invitation().dump();
    }
    catch (...)
    {
        return error(
            "pos_invitation",
            "Cannot create invitation. Check Delivery readiness or wait for existing invitations to expire.");
    }
}
std::string MoneroMerchantImpl::approvePosDevice(const std::string& peerId)
{
    if (!pos_)
        return getPosPairing();
    try
    {
        return pos_->authorize(peerId, true).dump();
    }
    catch (...)
    {
        return error("pos_approval",
                     "Unknown or revoked terminal. A revoked terminal must enroll with a fresh identity.");
    }
}
std::string MoneroMerchantImpl::revokePosDevice(const std::string& peerId)
{
    if (!pos_)
        return getPosPairing();
    try
    {
        return pos_->authorize(peerId, false).dump();
    }
    catch (...)
    {
        return error("pos_revocation", "Cannot revoke this terminal identity.");
    }
}
