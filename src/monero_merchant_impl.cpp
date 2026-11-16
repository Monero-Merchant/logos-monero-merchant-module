#include "monero_merchant_impl.h"
#include "invoices/incoming_monitor.h"
#include "invoices/incoming_payments.h"
#include "invoices/invoice_store.h"
#include "invoices/merchant_amounts.h"
#include "invoices/usd_amounts.h"
#include "logos_sdk.h"
#include "pos/pos_delivery.h"

#include <algorithm>
#include <chrono>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUrl>
#include <QDebug>
#include <nlohmann/json.hpp>
#include <vector>
#include <limits>
#include <ctime>

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

std::string MoneroMerchantImpl::getPriceSettings()
{
    nlohmann::json settings;
    {
        std::lock_guard<std::mutex> lock(priceSettingsMutex_);
        settings = nlohmann::json::parse(priceSettings_, nullptr, false);
    }
    if (!settings.is_object()) return error("invalid_price_settings", "Price settings could not be read.");
    try {
        const auto chain = settings.at("chain_id").get<int64_t>();
        logos::CallError callError;
        const auto raw = modules().eth_rpc_module.get_chain_config(chain, &callError, 5000);
        if (!callError.ok()) return error("rpc_unavailable", "Ethereum RPC module is unavailable.");
        const auto reply = nlohmann::json::parse(raw);
        if (!reply.value("ok", false)) {
            if (reply.value("error", "") != "no config for chain " + std::to_string(chain))
                return error("rpc_unavailable", "Could not read the shared chain configuration.");
            settings["endpoint"] = "";
        } else settings["endpoint"] = reply.at("config").at("endpoint");
        return nlohmann::json{{"ok", true}, {"settings", settings}}.dump();
    } catch (...) { return error("invalid_price_settings", "Could not read the price or RPC settings."); }
}

std::string MoneroMerchantImpl::setPriceSettings(const std::string& settingsJson)
{
    try {
        auto input = nlohmann::json::parse(settingsJson);
        const auto chain = input.at("chain_id").get<int64_t>();
        const auto feed = input.at("feed").get<std::string>();
        const auto sequencer = input.at("sequencer").get<std::string>();
        const auto endpoint = input.at("endpoint").get<std::string>();
        const auto age = input.at("max_age_seconds").get<int64_t>();
        const auto grace = input.at("grace_seconds").get<int64_t>();
        auto address = [](const std::string& s) {
            return s.size() == 42 && s.substr(0, 2) == "0x" &&
                s.find_first_not_of("0123456789abcdefABCDEF", 2) == std::string::npos &&
                s.substr(2).find_first_not_of('0') != std::string::npos;
        };
        const QUrl url(QString::fromStdString(endpoint), QUrl::StrictMode);
        if (chain <= 0 || chain > 2147483647 || !address(feed) ||
            (!sequencer.empty() && !address(sequencer)) || (chain == 42161 && sequencer.empty()) ||
            age < 1 || age > 604800 || grace < 0 || grace > 604800 ||
            !url.isValid() || url.host().isEmpty() || (url.scheme() != "https" && url.scheme() != "http") ||
            !url.userInfo().isEmpty() || url.hasFragment())
            return error("invalid_price_settings", "Check the chain ID, endpoint, contract addresses and time limits.");
        logos::CallError callError;
        const auto existingRaw = modules().eth_rpc_module.get_chain_config(chain, &callError, 5000);
        if (!callError.ok()) return error("rpc_unavailable", "Ethereum RPC module is unavailable.");
        const auto existing = nlohmann::json::parse(existingRaw);
        nlohmann::json rpcConfig;
        if (existing.value("ok", false)) rpcConfig = existing.at("config");
        else if (existing.value("error", "") == "no config for chain " + std::to_string(chain)) rpcConfig = nlohmann::json::object();
        else return error("rpc_unavailable", "Could not read the shared chain configuration.");
        rpcConfig["endpoint"] = endpoint;
        const bool saved = modules().eth_rpc_module.set_chain_config(chain, rpcConfig.dump(), &callError, 5000);
        if (!callError.ok() || !saved) return error("rpc_save_failed", "Could not save the shared Ethereum RPC endpoint.");
        nlohmann::json settings{{"chain_id", chain}, {"feed", feed}, {"sequencer", sequencer},
                               {"max_age_seconds", age}, {"grace_seconds", grace}};
        const auto serialized = settings.dump();
        std::lock_guard<std::mutex> lock(priceSettingsMutex_);
        QSaveFile file(QString::fromStdString(instancePersistencePath()) + "/price-settings.json");
        if (!file.open(QIODevice::WriteOnly) || file.write(serialized.data(), serialized.size()) != static_cast<qint64>(serialized.size()) || !file.commit())
            return error("price_save_failed", "The shared RPC endpoint was saved, but price settings could not be saved.");
        priceSettings_ = serialized;
        settings["endpoint"] = endpoint;
        return nlohmann::json{{"ok", true}, {"settings", settings}}.dump();
    } catch (...) { return error("invalid_price_settings", "Invalid price settings."); }
}

std::string MoneroMerchantImpl::getWalletStatus()
{
    // Wallet IPC marshals onto the main thread. Holding stateMutex_ here
    // deadlocks if that thread handles a status/reconciliation request meanwhile.
    logos::CallError callError;
    const auto reply = modules().monero_wallet_backend.wallet_status(&callError, 5000);
    return walletReply(reply, callError);
}

MoneroMerchantImpl::~MoneroMerchantImpl()
{
    aboutToUnload();
}

void MoneroMerchantImpl::onContextReady()
{
    {
        std::lock_guard<std::mutex> lock(priceSettingsMutex_);
        QFile file(QString::fromStdString(instancePersistencePath()) + "/price-settings.json");
        if (file.exists()) {
            if (file.open(QIODevice::ReadOnly)) priceSettings_ = file.readAll().toStdString();
            else priceSettings_ = "invalid";
        } else priceSettings_ = nlohmann::json{{"chain_id", 42161},
            {"feed", "0x8c76E8cab5ef3B410a318DDb82d83Ed47D7d2701"},
            {"sequencer", "0xFdB631F5EE196F0ed6FAa767959853A9F217697D"},
            {"max_age_seconds", 3600}, {"grace_seconds", 3600}}.dump();
    }

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
                    const auto started = std::chrono::steady_clock::now();
                    invoices_->reconcile(nlohmann::json::parse(snapshot));
                    qDebug().noquote() << "MerchantPayment reconcile_done epoch_ms=" << QDateTime::currentMSecsSinceEpoch()
                        << "elapsed_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
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
    const auto changed = [weak](const char* reason)
    {
        return [weak, reason](const std::string&) {
            if (auto monitor = weak.lock()) monitor->refresh(reason);
        };
    };
    auto* wallet = &modules().monero_wallet_backend;
    auto handles = std::make_shared<std::vector<logos::SubHandle>>();
    handles->push_back(wallet->onBalance_changed(changed("balance_changed")));
    handles->push_back(wallet->onSync_progress(changed("sync_progress")));
    handles->push_back(wallet->onWallet_state_changed(changed("wallet_state_changed")));
    wallet->onSubscriptionStatus(
        [weak](logos::SubStatus, std::uint64_t)
        {
            // Reconcile after arming/reconnection; events are not a durable queue.
            if (auto monitor = weak.lock())
                monitor->refresh("subscription_status");
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
                if (method == "quoteUsd" && args.size() == 1)
                {
                    const auto cents = args[0].get<uint64_t>();
                    if (!cents || cents > 9999999999ULL)
                        throw std::runtime_error("Invalid USD amount");
                    nlohmann::json settings;
                    {
                        std::lock_guard<std::mutex> lock(priceSettingsMutex_);
                        settings = nlohmann::json::parse(priceSettings_);
                    }
                    const auto chain = settings.at("chain_id").get<int64_t>();
                    auto rpc = [this, chain](const char* method, const nlohmann::json& params) {
                        logos::CallError err;
                        auto raw = modules().eth_rpc_module.raw_rpc(chain, method, params.dump(), &err, 15000);
                        if (!err.ok()) throw std::runtime_error("Price RPC unavailable; check Wallet & settings");
                        auto v = nlohmann::json::parse(raw, nullptr, false);
                        if (!v.is_object() || !v.value("ok", false) || !v.contains("result") || !v["result"].is_string())
                            throw std::runtime_error("Price RPC request failed; check network and proxy settings");
                        return v["result"].get<std::string>();
                    };
                    auto word = [](const std::string& hex, size_t offset) -> uint64_t {
                        if (hex.size() < 2 + offset + 64 || hex.substr(0, 2) != "0x")
                            throw std::runtime_error("Invalid oracle response");
                        auto w = hex.substr(2 + offset, 64);
                        if (w.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos ||
                            w.substr(0, 48).find_first_not_of('0') != std::string::npos)
                            throw std::runtime_error("Oracle value out of range");
                        return std::stoull(w.substr(48), nullptr, 16);
                    };
                    auto read = [&](const char* address, const char* data) {
                        return rpc("eth_call", nlohmann::json::array({{{"to", address}, {"data", data}}, "latest"}));
                    };
                    const auto chainHex = rpc("eth_chainId", nlohmann::json::array());
                    if (chainHex.size() < 3 || chainHex.substr(0, 2) != "0x" ||
                        chainHex.find_first_not_of("0123456789abcdefABCDEF", 2) != std::string::npos ||
                        std::stoull(chainHex.substr(2), nullptr, 16) != static_cast<uint64_t>(chain))
                        throw std::runtime_error("Configured RPC returned a different chain ID");
                    const auto now = static_cast<uint64_t>(std::time(nullptr));
                    const auto sequencer = settings.at("sequencer").get<std::string>();
                    if (!sequencer.empty()) {
                        const auto uptime = read(sequencer.c_str(), "0xfeaf968c");
                        const auto started = word(uptime, 128);
                        if (word(uptime, 64) != 0 || !started || started > now ||
                            now - started <= settings.at("grace_seconds").get<uint64_t>())
                            throw std::runtime_error("Sequencer unavailable or recovering");
                    }
                    const auto feed = settings.at("feed").get<std::string>();
                    const auto decimals = word(read(feed.c_str(), "0x313ce567"), 0);
                    if (decimals > 12) throw std::runtime_error("Unsupported oracle precision");
                    const auto quote = read(feed.c_str(), "0xfeaf968c");
                    const auto price = word(quote, 64);
                    const auto updated = word(quote, 192);
                    if (!price || !updated || updated > now || now - updated > settings.at("max_age_seconds").get<uint64_t>())
                        throw std::runtime_error("XMR/USD price is unavailable or too old");
                    const auto atomic = merchant::usdToAtomic(cents, price, decimals);
                    return nlohmann::json{{"amount_xmr", merchant::xmrAmount(static_cast<uint64_t>(atomic))},
                                          {"price", std::to_string(price)}, {"decimals", decimals}, {"updated_at", updated},
                                          {"chain_id", chain}, {"feed", feed}};
                }
                if (method == "createPosInvoice" && args.size() == 5)
                    return nlohmann::json::parse(createInvoiceForWallet(args[0], args[1], args[2], args[3], args[4]));
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
    incoming_->refresh("explicit_refresh");
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
                                                       const std::string& expectedWallet,
                                                       const std::string& pricingJson)
{
    std::unique_lock<std::mutex> operation(invoiceOperationMutex_, std::try_to_lock);
    if (!operation.owns_lock())
        return error("invoice_busy", "Another invoice operation is in progress. Retry the same order.");
    std::shared_ptr<merchant::InvoiceStore> store;
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex_);
        store = invoices_;
    }
    if (!store)
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
        const auto pricing = pricingJson.empty() ? nlohmann::json() : nlohmann::json::parse(pricingJson);
        nlohmann::json existing;
        {
            std::lock_guard<std::recursive_mutex> lock(stateMutex_);
            existing = store->find(requestId);
        }
        if (!existing.is_null())
        {
            if (!expectedWallet.empty() && existing["wallet_id"] != expectedWallet)
                return error("wallet_changed", "Return to the original POS order wallet.");
            if (existing["amount_due_atomic"] != amount || existing["title"] != title)
                return error("request_conflict", "This request ID already belongs to a different invoice.");
            if (!pricing.is_null()) {
                store->attachPricing(requestId, pricing);
                existing = store->find(requestId);
            } else if (!existing["pricing"].is_null())
                return error("request_conflict", "This invoice has different pricing details.");
            if (existing["creation_state"] == "ready")
                return invoiceReply(existing, true);
            return finishInvoiceAddress(store, requestId, !existing["allocation_attempted"].get<bool>());
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
        {
            std::lock_guard<std::recursive_mutex> lock(stateMutex_);
            (void)store->reserve(requestId, title, amount, status, pricing);
        }
        return finishInvoiceAddress(store, requestId, true);
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
    std::unique_lock<std::mutex> operation(invoiceOperationMutex_, std::try_to_lock);
    if (!operation.owns_lock())
        return error("invoice_busy", "Another invoice operation is in progress. Retry this invoice.");
    std::shared_ptr<merchant::InvoiceStore> store;
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex_);
        store = invoices_;
    }
    if (!store)
        return getInvoices();
    return finishInvoiceAddress(store, invoiceId, false);
}

std::string MoneroMerchantImpl::finishInvoiceAddress(const std::shared_ptr<merchant::InvoiceStore>& store,
                                                    const std::string& id, bool allocate)
{
    using Json = nlohmann::json;
    try
    {
        Json invoice;
        {
            std::lock_guard<std::recursive_mutex> lock(stateMutex_);
            invoice = store->find(id);
        }
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
            {
                std::lock_guard<std::recursive_mutex> lock(stateMutex_);
                store->markAttempted(id);
            }
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
        {
            std::lock_guard<std::recursive_mutex> lock(stateMutex_);
            store->bind(id, found["index"].get<uint32_t>(), found["address"]);
            invoice = store->find(id);
        }
        publishInvoices();
        if (incoming_)
            incoming_->refresh("invoice_created");
        return invoiceReply(invoice);
    }
    catch (const std::exception& e)
    {
        try
        {
            {
                std::lock_guard<std::recursive_mutex> lock(stateMutex_);
                store->recoveryRequired(id, e.what());
            }
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
