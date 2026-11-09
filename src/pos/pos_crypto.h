#pragma once
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
namespace merchant::pos
{
using Json = nlohmann::json;
std::string randomBytes(size_t size);
std::string encode(const std::string& bytes);
std::string decode(const std::string& text);
std::string digest(const std::string& bytes);
std::string publicKey(const std::string& privateKey);
bool uuid(const std::string& value);
int64_t now();
struct Message
{
    std::string sender, id;
    Json payload;
};
Json seal(const std::string& key, const std::string& recipient, const std::string& topic,
          const std::string& direction, const std::string& id, const Json& payload);
Message open(const std::string& key, const std::string& topic, const std::string& direction,
             const std::string& raw);
} // namespace merchant::pos
