#include "invoice_store.h"
#include "incoming_payments.h"
#include "invoice_database.h"
#include "merchant_amounts.h"

#include <chrono>
#include <mutex>
#include <set>

namespace merchant
{
using Json = nlohmann::json;
namespace
{
int64_t now()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
bool authoritative(const Json& monitor)
{
    return monitor.value("ok", false) && monitor.value("state", "") == "ready" &&
           !monitor.value("stale", true) && monitor.contains("wallet") && monitor["wallet"].is_object() &&
           monitor["wallet"].value("connected", Json()) != false &&
           monitor["wallet"].value("synchronized", Json()) != false;
}
struct ReceiptTotals
{
    uint64_t received = 0;
    uint64_t confirmed = 0;

    std::string status(uint64_t due) const
    {
        if (confirmed >= due)
            return "paid";
        if (received >= due)
            return "accepted";
        return received ? "partial" : "pending";
    }
};

ReceiptTotals receiptTotals(const Json& invoice)
{
    ReceiptTotals totals;
    for (const auto& payment : invoice["payments"])
    {
        if (!payment["present"].get<bool>())
            continue;
        const auto amount = atomicNumber(payment["amount_atomic"]);
        totals.received = addAmount(totals.received, amount);
        // Historical invoices can still have a zero-confirmation policy.
        if (invoice["required_confirmations"] == 0 ||
            (!payment["pending"].get<bool>() &&
             payment["confirmations"] >= invoice["required_confirmations"]))
        {
            totals.confirmed = addAmount(totals.confirmed, amount);
        }
    }
    return totals;
}
} // namespace

struct InvoiceStore::Impl
{
    detail::InvoiceDatabase database;
    mutable std::mutex mutex;

    explicit Impl(const std::string& directory) : database(directory)
    {
        database.write(
            "UPDATE invoices SET creation_state='recovery_required', creation_error='Address allocation was "
            "interrupted. Recover using the original wallet.' WHERE creation_state='allocating'");
    }

    int confirmationPolicy() const
    {
        const auto row = database.one("SELECT confirmation_policy FROM merchant_settings WHERE id=1");
        if (row.is_null())
            throw InvoiceError("storage_error", "Confirmation policy is unavailable.");
        return row.at("confirmation_policy").get<int>();
    }

    Json association(const std::string& wallet, uint32_t account, uint32_t index) const
    {
        return database.one("SELECT id FROM invoices WHERE wallet_id=? AND account_index=? AND "
                            "subaddress_index=? AND creation_state='ready'",
                            {wallet, account, index});
    }

    void recordReceipt(const std::string& invoiceId, const std::string& wallet, const Json& payment);
    void markMissingReceipts(const std::string& wallet, const std::set<std::string>& seen);
    void updateInvoice(const std::string& id);

    void event(const std::string& id, const char* kind, const Json& details)
    {
        database.write("INSERT INTO invoice_events(invoice_id,kind,details,created_at) VALUES(?,?,?,?)",
                       {id, kind, details.dump(), now()});
    }
    Json invoice(const std::string& id) const
    {
        auto invoice = database.one("SELECT * FROM invoices WHERE id=?", {id});
        if (invoice.is_null())
            return invoice;
        const auto due = atomicNumber(invoice["amount_due_atomic"]);
        const auto received = atomicNumber(invoice["received_atomic"]);
        invoice["amount_due_xmr"] = xmrAmount(due);
        invoice["received_xmr"] = xmrAmount(received);
        invoice["confirmed_xmr"] = xmrAmount(atomicNumber(invoice["confirmed_atomic"]));
        invoice["remaining_xmr"] = xmrAmount(received < due ? due - received : 0);
        invoice["overpaid_xmr"] = xmrAmount(received > due ? received - due : 0);
        invoice["requires_review"] = invoice["requires_review"] != 0;
        invoice["allocation_attempted"] = invoice["allocation_attempted"] != 0;
        invoice["payment_uri"] = invoice["creation_state"] == "ready" && received < due
                                     ? "monero:" + invoice["receiving_address"].get<std::string>() +
                                           "?tx_amount=" + invoice["remaining_xmr"].get<std::string>()
                                     : "";
        invoice["payments"] =
            database.list("SELECT * FROM payments WHERE invoice_id=? ORDER BY timestamp DESC,id", {id});
        for (auto& payment : invoice["payments"])
        {
            payment["pending"] = payment["pending"] != 0;
            payment["present"] = payment["present"] != 0;
            payment["amount_xmr"] = xmrAmount(atomicNumber(payment["amount_atomic"]));
        }
        invoice["events"] = database.list(
            "SELECT kind,details,created_at FROM invoice_events WHERE invoice_id=? ORDER BY id DESC LIMIT 50",
            {id});
        return invoice;
    }
};

void InvoiceStore::Impl::recordReceipt(const std::string& invoiceId, const std::string& wallet,
                                       const Json& payment)
{
    const auto id = payment.at("id").get<std::string>();
    (void)atomicNumber(payment.at("amount_atomic"));
    const auto old = database.one("SELECT * FROM payments WHERE id=?", {id});
    if (!old.is_null() &&
        (old["invoice_id"] != invoiceId || old["amount_atomic"] != payment["amount_atomic"]))
        throw InvoiceError("payment_conflict",
                           "An existing receipt changed identity or amount; review the wallet history.");
    if (!old.is_null() && old["present"] == 1 &&
        (!payment.at("present").get<bool>() || payment.at("confirmations") < old["confirmations"]))
        database.write("UPDATE invoices SET requires_review=1 WHERE id=?", {invoiceId});
    database.write(
        R"SQL(INSERT INTO payments(id,invoice_id,wallet_id,txid,account_index,subaddress_index,amount_atomic,confirmations,pending,present,height,timestamp,first_seen_at,last_seen_at)
VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET confirmations=excluded.confirmations,pending=excluded.pending,present=excluded.present,height=excluded.height,timestamp=excluded.timestamp,last_seen_at=excluded.last_seen_at)SQL",
        {id, invoiceId, wallet, payment.at("txid"), payment.at("account_index"),
         payment.at("subaddress_index"), payment.at("amount_atomic"), payment.at("confirmations"),
         payment.at("pending"), payment.at("present"), payment.at("height"), payment.at("timestamp"), now(),
         now()});
}

void InvoiceStore::Impl::markMissingReceipts(const std::string& wallet, const std::set<std::string>& seen)
{
    // Also catch missing receipts after a restart, when the monitor's in-memory
    // disappearance cache no longer contains them.
    for (const auto& old :
         database.list("SELECT id,invoice_id FROM payments WHERE wallet_id=? AND present=1", {wallet}))
        if (!seen.count(old["id"].get<std::string>()))
        {
            database.write("UPDATE payments SET present=0 WHERE id=?", {old["id"]});
            database.write("UPDATE invoices SET requires_review=1 WHERE id=?", {old["invoice_id"]});
        }
}

void InvoiceStore::Impl::updateInvoice(const std::string& id)
{
    const auto invoice = this->invoice(id);
    const auto totals = receiptTotals(invoice);
    const auto due = atomicNumber(invoice["amount_due_atomic"]);
    const auto state = totals.status(due);
    const bool regression = invoice["status"] == "paid" && state != "paid";
    database.write(
        R"SQL(UPDATE invoices SET status=?,received_atomic=?,confirmed_atomic=?,reconciled_at=?,updated_at=?,
requires_review=CASE WHEN ? THEN 1 ELSE requires_review END,
accepted_at=CASE WHEN ? AND accepted_at IS NULL THEN ? ELSE accepted_at END,
paid_at=CASE WHEN ? AND paid_at IS NULL THEN ? ELSE paid_at END WHERE id=?)SQL",
        {state, std::to_string(totals.received), std::to_string(totals.confirmed), now(), now(), regression,
         totals.received >= due, now(), state == "paid", now(), id});
    if (state != invoice["status"])
        event(id, "status_changed",
              {{"from", invoice["status"]},
               {"to", state},
               {"received_atomic", std::to_string(totals.received)},
               {"confirmed_atomic", std::to_string(totals.confirmed)}});
}

InvoiceStore::InvoiceStore(const std::string& directory) : impl_(std::make_unique<Impl>(directory)) {}
InvoiceStore::~InvoiceStore() = default;
Json InvoiceStore::find(const std::string& id) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->invoice(id);
}
int InvoiceStore::confirmationPolicy() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->confirmationPolicy();
}
void InvoiceStore::setConfirmationPolicy(int64_t confirmations)
{
    if (confirmations != 1 && confirmations != 5 && confirmations != 10)
        throw InvoiceError("invalid_confirmation_policy", "Choose 1, 5 or 10 confirmations.");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    detail::Transaction transaction(impl_->database);
    impl_->database.write("UPDATE merchant_settings SET confirmation_policy=? WHERE id=1", {confirmations});
    transaction.commit();
}
Json InvoiceStore::reserve(const std::string& id, const std::string& title, const std::string& atomic,
                           const Json& wallet)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (id.size() < 12 || id.size() > 80 ||
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
            std::string::npos)
        throw InvoiceError("invalid_request_id",
                           "Invoice request ID must be 12–80 letters, digits, hyphens or underscores.");
    if (title.size() > 240)
        throw InvoiceError("invalid_title", "Invoice title is too long.");
    if (!atomicNumber(atomic))
        throw InvoiceError("invalid_amount", "Invoice amount must be positive.");
    const auto identity = walletIdentity(wallet);
    auto existing = impl_->invoice(id);
    if (!existing.is_null())
    {
        if (existing["wallet_id"] != identity || existing["title"] != title ||
            existing["amount_due_atomic"] != atomic)
            throw InvoiceError("request_conflict", "This request ID already belongs to a different invoice.");
        return existing;
    }
    detail::Transaction transaction(impl_->database);
    const auto policy = impl_->confirmationPolicy();
    impl_->database.write("INSERT INTO "
                          "invoices(id,wallet_id,wallet_name,network,wallet_address,title,amount_due_atomic,"
                          "required_confirmations,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?)",
                          {id, identity, wallet.at("wallet"), wallet.at("network"), wallet.at("address"),
                           title, atomic, policy, now(), now()});
    impl_->event(id, "creation_started", {{"amount_atomic", atomic}, {"required_confirmations", policy}});
    transaction.commit();
    return impl_->invoice(id);
}
void InvoiceStore::markAttempted(const std::string& id)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->database.write(
        "UPDATE invoices SET allocation_attempted=1,updated_at=? WHERE id=? AND creation_state!='ready'",
        {now(), id});
}
void InvoiceStore::bind(const std::string& id, uint32_t index, const std::string& address)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!index || address.empty())
        throw InvoiceError("invalid_address", "Invoice needs a non-primary receiving subaddress.");
    const auto invoice = impl_->invoice(id);
    if (invoice.is_null())
        throw InvoiceError("not_found", "Invoice does not exist.");
    if (invoice["creation_state"] == "ready")
    {
        if (invoice["subaddress_index"] != index || invoice["receiving_address"] != address)
            throw InvoiceError("address_conflict", "The invoice already has a permanent receiving address.");
        return;
    }
    detail::Transaction transaction(impl_->database);
    impl_->database.write("UPDATE invoices SET "
                          "subaddress_index=?,receiving_address=?,creation_state='ready',creation_error='',"
                          "updated_at=? WHERE id=?",
                          {index, address, now(), id});
    impl_->event(id, "address_assigned",
                 {{"account_index", 0}, {"subaddress_index", index}, {"address", address}});
    transaction.commit();
}
void InvoiceStore::recoveryRequired(const std::string& id, const std::string& message)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->database.write(
        "UPDATE invoices SET creation_state='recovery_required',creation_error=?,updated_at=? WHERE id=? AND "
        "creation_state!='ready'",
        {message, now(), id});
}
void InvoiceStore::acknowledgeReview(const std::string& id)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->invoice(id).is_null())
        throw InvoiceError("not_found", "Invoice does not exist.");
    detail::Transaction transaction(impl_->database);
    impl_->database.write("UPDATE invoices SET requires_review=0,updated_at=? WHERE id=?", {now(), id});
    impl_->event(id, "review_acknowledged", Json::object());
    transaction.commit();
}
void InvoiceStore::reconcile(const Json& snapshot)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!authoritative(snapshot))
        return;
    const auto wallet = snapshot.at("wallet").at("id").get<std::string>();
    if (!snapshot.at("payments").is_array())
        throw InvoiceError("invalid_snapshot", "Payment snapshot contains no receipts.");

    detail::Transaction transaction(impl_->database);
    std::set<std::string> seen;
    for (const auto& payment : snapshot["payments"])
    {
        const auto target =
            impl_->association(wallet, payment.at("account_index"), payment.at("subaddress_index"));
        if (target.is_null())
            continue;
        if (!seen.insert(payment.at("id").get<std::string>()).second)
            throw InvoiceError("invalid_snapshot", "Duplicate payment in reconciliation.");
        impl_->recordReceipt(target["id"], wallet, payment);
    }
    impl_->markMissingReceipts(wallet, seen);
    for (const auto& row : impl_->database.list(
             "SELECT id FROM invoices WHERE wallet_id=? AND creation_state='ready'", {wallet}))
        impl_->updateInvoice(row["id"]);
    transaction.commit();
}
Json InvoiceStore::snapshot(const Json& monitor) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Json invoices = Json::array();
    const auto wallet = monitor.value("wallet", Json());
    const auto walletId = wallet.is_object() ? wallet.value("id", "") : std::string();
    for (const auto& row : impl_->database.list(
             "SELECT id FROM invoices WHERE wallet_id=? ORDER BY created_at DESC,id DESC", {walletId}))
    {
        auto invoice = impl_->invoice(row["id"]);
        invoice["active_wallet"] = wallet.is_object() && wallet.value("id", "") == invoice["wallet_id"];
        invoice["stale"] = !invoice["active_wallet"].get<bool>() || !authoritative(monitor) ||
                           invoice["reconciled_at"].is_null();
        invoices.push_back(std::move(invoice));
    }
    return {{"ok", true},
            {"confirmation_policy", impl_->confirmationPolicy()},
            {"state", monitor.value("state", "starting")},
            {"wallet", wallet},
            {"stale", !authoritative(monitor)},
            {"monitor_error", monitor.value("error", Json())},
            {"invoices", std::move(invoices)},
            {"updated_at", now()}};
}
} // namespace merchant
