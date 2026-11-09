#pragma once
#include "pos_crypto.h"
#include <functional>
#include <mutex>
#include <utility>
struct sqlite3;
namespace merchant::pos
{
// Protocol and authorization are independent of the Delivery event loop.
class Service
{
public:
    using Backend = std::function<Json(const std::string&, const Json&)>;
    Service(const std::string& directory, Backend backend);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    Json snapshot();
    Json invitation();
    Json authorize(const std::string& peer, bool approve);
    std::pair<std::string, std::string> handle(const std::string& raw);
    const std::string& topic() const
    {
        return topic_;
    }

private:
    Json query(const char* sql, std::initializer_list<Json> args = {});
    Json one(const char* sql, std::initializer_list<Json> args = {});
    void exec(const char* sql);
    std::string status(const std::string& peer);
    Json dispatch(const std::string& peer, const Json& request);
    Json invoice(const std::string& peer, const std::string& operation, const Json& args);
    Json compact(const Json& row, const std::string& order, bool stale);
    Json devices();
    Json merchant() const;
    sqlite3* db_ = nullptr;
    int lock_ = -1;
    std::mutex mutex_;
    std::string key_, id_, topic_;
    Backend backend_;
};
} // namespace merchant::pos
