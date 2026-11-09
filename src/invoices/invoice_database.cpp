#include "invoice_database.h"
#include "invoice_store.h"

#include <filesystem>
#include <sqlite3.h>

namespace merchant::detail
{
using Json = nlohmann::json;
namespace
{
void execute(sqlite3* db, const char* sql)
{
    char* message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) != SQLITE_OK)
    {
        const std::string error = message ? message : sqlite3_errmsg(db);
        sqlite3_free(message);
        throw InvoiceError("storage_error", error);
    }
}
void createInvoices(sqlite3* db)
{
    execute(db, R"SQL(CREATE TABLE invoices(
 id TEXT PRIMARY KEY, wallet_id TEXT NOT NULL, wallet_name TEXT NOT NULL, network TEXT NOT NULL,
 wallet_address TEXT NOT NULL, account_index INTEGER NOT NULL DEFAULT 0,
 subaddress_index INTEGER, receiving_address TEXT, title TEXT NOT NULL, amount_due_atomic TEXT NOT NULL,
 required_confirmations INTEGER NOT NULL DEFAULT 5 CHECK(required_confirmations >= 0),
 status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ('pending','partial','accepted','paid')),
 creation_state TEXT NOT NULL DEFAULT 'allocating' CHECK(creation_state IN ('allocating','recovery_required','ready')),
 allocation_attempted INTEGER NOT NULL DEFAULT 0, creation_error TEXT NOT NULL DEFAULT '',
 received_atomic TEXT NOT NULL DEFAULT '0', confirmed_atomic TEXT NOT NULL DEFAULT '0',
 requires_review INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL,
 accepted_at INTEGER, paid_at INTEGER, reconciled_at INTEGER,
 UNIQUE(wallet_id, account_index, subaddress_index), UNIQUE(wallet_id, receiving_address)
);)SQL");
}
void createSettings(sqlite3* db)
{
    execute(db, R"SQL(
CREATE TABLE merchant_settings (
 id INTEGER PRIMARY KEY CHECK(id=1),
 confirmation_policy INTEGER NOT NULL DEFAULT 5 CHECK(confirmation_policy IN (1,5,10))
);
INSERT INTO merchant_settings(id,confirmation_policy) VALUES(1,5);
)SQL");
}
class Statement
{
    sqlite3* db_;
    sqlite3_stmt* statement_ = nullptr;

public:
    Statement(sqlite3* db, const char* sql) : db_(db)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr) != SQLITE_OK)
            throw InvoiceError("storage_error", sqlite3_errmsg(db));
    }
    ~Statement()
    {
        sqlite3_finalize(statement_);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void bind(int index, const Json& value)
    {
        int code;
        if (value.is_null())
            code = sqlite3_bind_null(statement_, index);
        else if (value.is_string())
        {
            const auto& s = value.get_ref<const std::string&>();
            code =
                sqlite3_bind_text(statement_, index, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
        }
        else if (value.is_boolean())
            code = sqlite3_bind_int(statement_, index, value.get<bool>() ? 1 : 0);
        else
            code = sqlite3_bind_int64(statement_, index, value.get<int64_t>());
        if (code != SQLITE_OK)
            throw InvoiceError("storage_error", sqlite3_errmsg(db_));
    }
    void bind(std::initializer_list<Json> values)
    {
        int index = 1;
        for (const auto& value : values)
            bind(index++, value);
    }
    bool next()
    {
        const int code = sqlite3_step(statement_);
        if (code != SQLITE_ROW && code != SQLITE_DONE)
            throw InvoiceError("storage_error", sqlite3_errmsg(db_));
        return code == SQLITE_ROW;
    }
    Json row() const
    {
        Json result = Json::object();
        for (int i = 0; i < sqlite3_column_count(statement_); ++i)
        {
            const char* name = sqlite3_column_name(statement_, i);
            switch (sqlite3_column_type(statement_, i))
            {
            case SQLITE_NULL:
                result[name] = nullptr;
                break;
            case SQLITE_INTEGER:
                result[name] = sqlite3_column_int64(statement_, i);
                break;
            default:
                result[name] = reinterpret_cast<const char*>(sqlite3_column_text(statement_, i));
            }
        }
        return result;
    }
};
} // namespace

InvoiceDatabase::InvoiceDatabase(const std::string& directory)
{
    if (directory.empty())
        throw InvoiceError("storage_unavailable", "Logos did not provide a persistence directory.");
    std::filesystem::create_directories(directory);
    const auto path = std::filesystem::path(directory) / "merchant-v4.sqlite3";
    if (sqlite3_open_v2(path.string().c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK)
    {
        const std::string message = db_ ? sqlite3_errmsg(db_) : "Cannot open invoice database";
        if (db_)
            sqlite3_close(db_);
        db_ = nullptr;
        throw InvoiceError("storage_error", message);
    }
    try
    {
        sqlite3_busy_timeout(db_, 3000);
        execute(db_, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
        initializeSchema();
    }
    catch (...)
    {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}

void InvoiceDatabase::initializeSchema()
{
    int version;
    {
        Statement query(db_, "PRAGMA user_version");
        query.next();
        version = query.row().begin().value().get<int>();
    }
    if (version != 0 && version != 4)
        throw InvoiceError(
            "unsupported_schema",
            "Unsupported invoice schema. Use fresh development state; legacy migration is not supported.");
    if (version == 0)
    {
        Transaction transaction(*this);
        createInvoices(db_);
        createSettings(db_);
        execute(db_, R"SQL(
CREATE TABLE payments (
 id TEXT PRIMARY KEY, invoice_id TEXT NOT NULL REFERENCES invoices(id), wallet_id TEXT NOT NULL,
 txid TEXT NOT NULL, account_index INTEGER NOT NULL, subaddress_index INTEGER NOT NULL,
 amount_atomic TEXT NOT NULL, confirmations INTEGER NOT NULL, pending INTEGER NOT NULL,
 present INTEGER NOT NULL, height INTEGER NOT NULL, timestamp INTEGER NOT NULL,
 first_seen_at INTEGER NOT NULL, last_seen_at INTEGER NOT NULL,
 UNIQUE(wallet_id,txid,account_index,subaddress_index)
);
CREATE INDEX invoice_payments ON payments(invoice_id);
CREATE TABLE invoice_events (
 id INTEGER PRIMARY KEY, invoice_id TEXT NOT NULL REFERENCES invoices(id),
 kind TEXT NOT NULL, details TEXT NOT NULL, created_at INTEGER NOT NULL
);
PRAGMA user_version=4;
)SQL");
        transaction.commit();
    }
}

InvoiceDatabase::~InvoiceDatabase()
{
    if (db_)
        sqlite3_close(db_);
}

Json InvoiceDatabase::one(const char* sql, std::initializer_list<Json> values) const
{
    Statement query(db_, sql);
    query.bind(values);
    return query.next() ? query.row() : Json();
}

Json InvoiceDatabase::list(const char* sql, std::initializer_list<Json> values) const
{
    Statement query(db_, sql);
    query.bind(values);
    Json rows = Json::array();
    while (query.next())
        rows.push_back(query.row());
    return rows;
}

void InvoiceDatabase::write(const char* sql, std::initializer_list<Json> values)
{
    Statement statement(db_, sql);
    statement.bind(values);
    statement.next();
}

Transaction::Transaction(InvoiceDatabase& database) : database_(database)
{
    execute(database_.db_, "BEGIN IMMEDIATE");
}

Transaction::~Transaction()
{
    if (!committed_)
        sqlite3_exec(database_.db_, "ROLLBACK", nullptr, nullptr, nullptr);
}

void Transaction::commit()
{
    execute(database_.db_, "COMMIT");
    committed_ = true;
}
} // namespace merchant::detail
