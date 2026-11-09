#pragma once

#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>

struct sqlite3;

namespace merchant::detail
{
// SQLite ownership, binding and fresh schema creation stay out of invoice rules.
class InvoiceDatabase
{
public:
    explicit InvoiceDatabase(const std::string& directory);
    ~InvoiceDatabase();
    InvoiceDatabase(const InvoiceDatabase&) = delete;
    InvoiceDatabase& operator=(const InvoiceDatabase&) = delete;

    nlohmann::json one(const char* sql, std::initializer_list<nlohmann::json> values = {}) const;
    nlohmann::json list(const char* sql, std::initializer_list<nlohmann::json> values = {}) const;
    void write(const char* sql, std::initializer_list<nlohmann::json> values = {});

private:
    friend class Transaction;
    void initializeSchema();
    sqlite3* db_ = nullptr;
};

class Transaction
{
public:
    explicit Transaction(InvoiceDatabase& database);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();

private:
    InvoiceDatabase& database_;
    bool committed_ = false;
};
} // namespace merchant::detail
