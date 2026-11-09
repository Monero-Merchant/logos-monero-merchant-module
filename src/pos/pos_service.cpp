#include "pos_service.h"
#include "invoices/merchant_amounts.h"
#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <openssl/crypto.h>
#include <sqlite3.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
namespace merchant::pos
{
namespace
{
void require(bool ok, const char* error)
{
    if (!ok)
        throw std::runtime_error(error);
}
Json refusal(const std::string& status, const std::string& message)
{
    return {{"ok", false}, {"status", status}, {"message", message}};
}
} // namespace
Service::Service(const std::string& directory, Backend backend) : backend_(std::move(backend))
{
    require(!directory.empty(), "Logos did not provide a persistence directory");
    namespace fs = std::filesystem;
    const auto dir = fs::path(directory) / "pos-v1";
    fs::create_directories(dir);
    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace);
    try
    {
        lock_ = ::open((dir / "service.lock").c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
        require(lock_ >= 0 && flock(lock_, LOCK_EX | LOCK_NB) == 0, "POS state is already in use");
        const auto path = dir / "pos.sqlite3";
        int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
        require(fd >= 0, "Cannot create private POS database");
        const auto permission = fchmod(fd, 0600);
        close(fd);
        require(permission == 0, "Cannot protect POS database");
        require(sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nullptr) ==
                    SQLITE_OK,
                "Cannot open POS database");
        sqlite3_busy_timeout(db_, 5000);
        exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;");
        auto version = one("PRAGMA user_version").at("user_version").get<int>();
        require(version == 0 || version == 1, "Unsupported POS schema; use fresh development state");
        if (version == 0)
            exec(R"SQL(
BEGIN IMMEDIATE;
CREATE TABLE identity(id INTEGER PRIMARY KEY CHECK(id=1), private_key TEXT NOT NULL);
CREATE TABLE invitations(digest TEXT PRIMARY KEY, expires_at INTEGER NOT NULL, used_by TEXT);
CREATE TABLE devices(peer_id TEXT PRIMARY KEY, name TEXT NOT NULL, status TEXT NOT NULL CHECK(status IN ('pending','approved','revoked')), created_at INTEGER NOT NULL, changed_at INTEGER NOT NULL);
CREATE TABLE requests(sender TEXT NOT NULL,id TEXT NOT NULL,digest TEXT NOT NULL,reply TEXT,created_at INTEGER NOT NULL,PRIMARY KEY(sender,id));
CREATE TABLE orders(peer_id TEXT NOT NULL REFERENCES devices(peer_id),order_id TEXT NOT NULL,backend_id TEXT NOT NULL UNIQUE,amount_xmr TEXT NOT NULL,wallet_id TEXT NOT NULL,created_at INTEGER NOT NULL,PRIMARY KEY(peer_id,order_id));
PRAGMA user_version=1;
COMMIT;
)SQL");
        auto identity = one("SELECT private_key FROM identity WHERE id=1");
        if (identity.is_null())
        {
            key_ = randomBytes(32);
            query("INSERT INTO identity VALUES(1,?)", {encode(key_)});
        }
        else
            key_ = decode(identity.at("private_key"));
        id_ = digest(publicKey(key_));
        topic_ = "/monero-merchant/2/" + id_ + "/json";
    }
    catch (...)
    {
        if (db_)
            sqlite3_close(db_);
        db_ = nullptr;
        if (lock_ >= 0)
            close(lock_);
        lock_ = -1;
        throw;
    }
}
Service::~Service()
{
    if (db_)
        sqlite3_close(db_);
    if (lock_ >= 0)
        close(lock_);
    if (!key_.empty())
        OPENSSL_cleanse(key_.data(), key_.size());
}
void Service::exec(const char* sql)
{
    require(sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK, "POS database operation failed");
}
Json Service::query(const char* sql, std::initializer_list<Json> args)
{
    sqlite3_stmt* raw = nullptr;
    require(sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr) == SQLITE_OK, "POS database query failed");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, sqlite3_finalize);
    int index = 1;
    for (const auto& value : args)
    {
        int rc;
        if (value.is_null())
            rc = sqlite3_bind_null(raw, index++);
        else if (value.is_string())
        {
            const auto& text = value.get_ref<const std::string&>();
            rc =
                sqlite3_bind_text(raw, index++, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
        }
        else
            rc = sqlite3_bind_int64(raw, index++, value.get<int64_t>());
        require(rc == SQLITE_OK, "POS parameter binding failed");
    }
    Json rows = Json::array();
    int rc;
    while ((rc = sqlite3_step(raw)) == SQLITE_ROW)
    {
        Json row = Json::object();
        for (int i = 0; i < sqlite3_column_count(raw); ++i)
        {
            auto name = sqlite3_column_name(raw, i);
            if (sqlite3_column_type(raw, i) == SQLITE_NULL)
                row[name] = nullptr;
            else if (sqlite3_column_type(raw, i) == SQLITE_INTEGER)
                row[name] = sqlite3_column_int64(raw, i);
            else
                row[name] = reinterpret_cast<const char*>(sqlite3_column_text(raw, i));
        }
        rows.push_back(std::move(row));
    }
    require(rc == SQLITE_DONE, "POS database operation failed");
    return rows;
}
Json Service::one(const char* sql, std::initializer_list<Json> args)
{
    auto rows = query(sql, args);
    return rows.empty() ? Json() : rows[0];
}
std::string Service::status(const std::string& peer)
{
    const auto row = one("SELECT status FROM devices WHERE peer_id=?", {peer});
    return row.is_null() ? "not_paired" : row.at("status").get<std::string>();
}
Json Service::devices()
{
    return query("SELECT * FROM devices ORDER BY created_at,peer_id");
}
Json Service::merchant() const
{
    return {{"peer_id", id_},
            {"public_key", encode(publicKey(key_))},
            {"merchant_name", "Monero Merchant"},
            {"backend_source", "basecamp"},
            {"transport", "delivery"},
            {"network", "logos.dev"},
            {"topic", topic_}};
}
Json Service::snapshot()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return {{"ok", true}, {"merchant", merchant()}, {"listen", topic_}, {"devices", devices()}};
}
Json Service::invitation()
{
    std::lock_guard<std::mutex> lock(mutex_);
    query("DELETE FROM invitations WHERE expires_at<=? OR used_by IS NOT NULL", {now()});
    require(one("SELECT COUNT(*) AS count FROM invitations")["count"].get<int>() < 16,
            "Too many active invitations; wait for expiry");
    const auto secret = encode(randomBytes(32));
    const auto expiry = now() + 300;
    query("INSERT INTO invitations VALUES(?,?,NULL)", {digest(secret), expiry});
    auto invite = merchant();
    invite["version"] = 2;
    invite["protocol"] = "/monero-merchant/delivery/2.0.0";
    invite["secret"] = secret;
    invite["expires_at"] = expiry;
    return {{"ok", true}, {"invitation", invite}};
}
Json Service::authorize(const std::string& peer, bool approve)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto previous = status(peer);
    require(previous != "not_paired", "Unknown terminal identity");
    require(!approve || previous != "revoked", "A revoked terminal must enroll with a fresh identity");
    query("UPDATE devices SET status=?,changed_at=? WHERE peer_id=?",
          {approve ? "approved" : "revoked", now(), peer});
    return {{"ok", true}, {"devices", devices()}};
}
std::pair<std::string, std::string> Service::handle(const std::string& raw)
{
    const auto message = open(key_, topic_, "request", raw);
    const auto peer = digest(message.sender), hash = digest(message.payload.dump());
    require(message.payload.is_object() && message.payload.value("request_id", "") == message.id,
            "Request ID mismatch");
    Json reply;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        query("DELETE FROM requests WHERE created_at<?", {now() - 600});
        const auto saved =
            one("SELECT digest,reply FROM requests WHERE sender=? AND id=?", {peer, message.id});
        if (!saved.is_null())
        {
            require(saved["digest"] == hash, "Request ID reused with different content");
            require(!saved["reply"].is_null(),
                    "Request outcome uncertain; use a fresh request ID and the same order ID");
            reply = Json::parse(saved["reply"].get<std::string>());
        }
        else
        {
            require(one("SELECT COUNT(*) AS count FROM requests")["count"].get<int>() < 4096,
                    "POS request queue full");
            query("INSERT INTO requests VALUES(?,?,?,NULL,?)", {peer, message.id, hash, now()});
        }
    }
    if (reply.is_null())
    {
        reply = dispatch(peer, message.payload);
        reply["version"] = 1;
        reply["request_id"] = message.id;
        std::lock_guard<std::mutex> lock(mutex_);
        query("UPDATE requests SET reply=? WHERE sender=? AND id=?", {reply.dump(), peer, message.id});
    }
    // A cached response is still subject to current wallet ownership. Retrying
    // a transport UUID must not disclose an old wallet's invoice after a switch.
    bool approved;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        approved = status(peer) == "approved";
    }
    if (approved && reply.value("ok", false) && (reply.contains("invoice") || reply.contains("invoices")))
    {
        try
        {
            const auto snapshot = backend_("getInvoices", Json::array());
            require(snapshot.value("ok", false), "Invoice storage unavailable");
            const auto wallet = snapshot.at("wallet").at("id").get<std::string>();
            auto rows = reply.contains("invoice") ? Json::array({reply["invoice"]}) : reply["invoices"];
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& row : rows)
            {
                const auto owned =
                    one("SELECT wallet_id FROM orders WHERE peer_id=? AND order_id=? AND backend_id=?",
                        {peer, row.at("order_id"), row.at("id")});
                require(!owned.is_null() && owned["wallet_id"] == wallet,
                        "Return to the original merchant wallet for this invoice");
            }
        }
        catch (...)
        {
            reply = refusal("invoice_error", "Invoice unavailable for the current merchant wallet");
            reply["version"] = 1;
            reply["request_id"] = message.id;
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto current = status(peer);
    const auto operation = message.payload.value("type", "");
    if (current == "revoked" || ((operation == "GetBackendStatus" || operation == "CreateInvoice" ||
                                  operation == "GetInvoice" || operation == "GetInvoices") &&
                                 current != "approved"))
        reply = {{"version", 1}, {"request_id", message.id}, {"ok", false}, {"status", current}};
    const auto topic = "/monero-pos/2/" + digest(id_ + peer) + "/json";
    return {topic, seal(key_, message.sender, topic, "reply", message.id, reply).dump()};
}
Json Service::dispatch(const std::string& peer, const Json& request)
{
    try
    {
        require(request.at("version") == 1, "Unsupported request version");
        const auto operation = request.at("type").get<std::string>();
        const auto args = request.value("args", Json::object());
        require(args.is_object(), "Invalid request arguments");
        if (operation == "Pair")
        {
            const auto secret = args.at("secret").get<std::string>();
            auto name = args.at("device_name").get<std::string>();
            const auto first = name.find_first_not_of(" \r\n\t"), last = name.find_last_not_of(" \r\n\t");
            require(secret.size() == 43 && first != std::string::npos && name.size() <= 320,
                    "Invalid pairing details");
            name = name.substr(first, last - first + 1);
            require(name.size() <= 80, "Terminal name is too long");
            std::lock_guard<std::mutex> lock(mutex_);
            if (status(peer) != "not_paired")
                return refusal(status(peer), "Peer already enrolled");
            const auto invite = one("SELECT * FROM invitations WHERE digest=?", {digest(secret)});
            if (invite.is_null() || invite["expires_at"].get<int64_t>() <= now() ||
                !invite["used_by"].is_null())
                return refusal("invalid_invitation", "Invitation expired or already used");
            require(one("SELECT COUNT(*) AS count FROM devices")["count"].get<int>() < 512,
                    "Terminal limit reached");
            exec("BEGIN IMMEDIATE");
            try
            {
                query("UPDATE invitations SET used_by=? WHERE digest=?", {peer, digest(secret)});
                query("INSERT INTO devices VALUES(?,?,?,?,?)", {peer, name, "pending", now(), now()});
                exec("COMMIT");
            }
            catch (...)
            {
                exec("ROLLBACK");
                throw;
            }
            return {{"ok", true}, {"status", "pending"}};
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto state = status(peer);
            if (operation == "Session")
                return {{"ok", true}, {"status", state}};
            if (state != "approved")
                return refusal(state, "Terminal is not authorized");
        }
        if (operation == "GetBackendStatus")
        {
            const auto status = backend_("getWalletStatus", Json::array());
            require(status.value("ok", false), "Merchant status unavailable");
            return {{"ok", true},
                    {"status", "ready"},
                    {"backend",
                     {{"network", status.value("network", "unknown")},
                      {"source", "basecamp"},
                      {"wallet_open",
                       status.contains("wallet") && status["wallet"].is_string() && status["wallet"] != ""},
                      {"connected", status.value("connected", false)},
                      {"synchronized", status.value("synchronized", false)}}}};
        }
        if (operation == "CreateInvoice" || operation == "GetInvoice" || operation == "GetInvoices")
            return invoice(peer, operation, args);
        return refusal("unsupported_operation", "Operation is not exposed by this POS");
    }
    catch (const std::exception&)
    {
        return refusal("invalid_request", "Invalid POS request or unavailable merchant backend");
    }
}
Json Service::compact(const Json& row, const std::string& order, bool stale)
{
    return {{"id", row.at("id")},
            {"order_id", order},
            {"amount_xmr", row.at("amount_due_xmr")},
            {"received_xmr", row.value("received_xmr", "0")},
            {"remaining_xmr", row.at("remaining_xmr")},
            {"status", row.at("status")},
            {"creation_state", row.at("creation_state")},
            {"payment_uri", row.value("payment_uri", "")},
            {"required_confirmations", row.value("required_confirmations", 5)},
            {"stale", stale || row.value("stale", false)},
            {"requires_review", row.value("requires_review", false)},
            {"created_at", row.value("created_at", int64_t(0))}};
}
Json Service::invoice(const std::string& peer, const std::string& operation, const Json& args)
{
    try
    {
        const auto snapshot = backend_("getInvoices", Json::array());
        require(snapshot.value("ok", false) && snapshot.at("invoices").is_array(),
                "Invoice storage unavailable");
        const auto wallet = snapshot.at("wallet").at("id").get<std::string>();
        require(!wallet.empty(), "Open the merchant wallet before checkout");
        const auto stale = snapshot.value("stale", true);
        if (operation == "CreateInvoice")
        {
            const auto order = args.at("order_id").get<std::string>();
            require(uuid(order), "Invalid order ID");
            const auto atomic = merchant::parseXmrAmount(args.at("amount_xmr").get<std::string>());
            const auto amount = merchant::xmrAmount(merchant::atomicNumber(atomic));
            const auto backendId = "pos-" + digest(peer + ":" + order);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                require(status(peer) == "approved", "Terminal access changed");
                const auto saved = one("SELECT * FROM orders WHERE peer_id=? AND order_id=?", {peer, order});
                if (!saved.is_null())
                {
                    require(saved["amount_xmr"] == amount, "Order already has a different amount");
                    require(saved["wallet_id"] == wallet,
                            "Return to the original merchant wallet for this order");
                }
                else
                {
                    require(!stale && snapshot.value("state", "") == "ready" &&
                                snapshot["wallet"].value("connected", false) &&
                                snapshot["wallet"].value("synchronized", false),
                            "Wait for a synchronized merchant wallet");
                    query("INSERT INTO orders VALUES(?,?,?,?,?,?)",
                          {peer, order, backendId, amount, wallet, now() * 1000});
                }
            }
            // Backend validates this expected wallet atomically with invoice reservation.
            const auto reply =
                backend_("createPosInvoice",
                         Json::array({backendId, "POS order " + order.substr(0, 8), amount, wallet}));
            if (!reply.value("ok", false))
                return {
                    {"ok", false},
                    {"status", "invoice_error"},
                    {"message", reply.value("error", "Invoice creation needs recovery; retry this order")},
                    {"order_id", order},
                    {"recovery_required", reply.value("recovery_required", false)}};
            const auto row = reply.at("invoice");
            require(row.at("wallet_id") == wallet && row.at("id") == backendId,
                    "Merchant wallet changed during creation");
            return {{"ok", true}, {"status", "invoice"}, {"invoice", compact(row, order, stale)}};
        }
        Json records;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            records = query("SELECT backend_id,order_id FROM orders WHERE peer_id=? AND wallet_id=?",
                            {peer, wallet});
        }
        std::unordered_map<std::string, std::string> orders;
        for (const auto& saved : records)
            orders.emplace(saved.at("backend_id"), saved.at("order_id"));
        Json rows = Json::array();
        for (const auto& row : snapshot["invoices"])
        {
            const auto owned = orders.find(row.at("id").get<std::string>());
            if (owned != orders.end())
                rows.push_back(compact(row, owned->second, stale));
        }
        if (operation == "GetInvoice")
        {
            const auto order = args.at("order_id").get<std::string>();
            for (const auto& row : rows)
                if (row["order_id"] == order)
                    return {{"ok", true}, {"status", "invoice"}, {"invoice", row}};
            return refusal("invoice_error",
                           "Order unavailable or incomplete; retry the same order in its original wallet");
        }
        const auto offset = args.value("offset", Json(0));
        require(offset.is_number_integer() && offset >= 0 && offset <= 100000, "Invalid transaction page");
        std::sort(rows.begin(), rows.end(),
                  [](const Json& a, const Json& b)
                  {
                      if (a["created_at"] == b["created_at"])
                          return a["id"] > b["id"];
                      return a["created_at"] > b["created_at"];
                  });
        const auto start = offset.get<size_t>();
        Json page = Json::array();
        for (size_t i = start; i < rows.size() && i < start + 5; ++i)
            page.push_back(rows[i]);
        return {
            {"ok", true},       {"status", "invoices"},
            {"stale", stale},   {"invoices", page},
            {"offset", offset}, {"next_offset", start + 5 < rows.size() ? Json(start + 5) : Json(nullptr)}};
    }
    catch (const std::exception& e)
    {
        return refusal("invoice_error", e.what());
    }
}
} // namespace merchant::pos
