#include "incoming_payments.h"
#include "merchant_amounts.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <map>
#include <stdexcept>

namespace merchant
{
using Json = nlohmann::json;
namespace
{
uint64_t number(const Json& value, const char* field)
{
    if ((!value.is_number_unsigned() && !value.is_number_integer()) ||
        (value.is_number_integer() && !value.is_number_unsigned() && value.get<int64_t>() < 0))
        throw std::runtime_error(std::string("Invalid ") + field);
    return value.get<uint64_t>();
}
std::string text(const Json& value, const char* field)
{
    if (!value.is_string() || value.get_ref<const std::string&>().empty())
        throw std::runtime_error(std::string("Missing ") + field);
    return value.get<std::string>();
}
uint64_t subaddress(const Json& value)
{
    if (value.is_number_integer())
        return number(value, "subaddress index");
    // Incoming Monero history rows identify one receiving subaddress. An
    // outgoing row may contain a comma-separated set, which is not a receipt.
    const auto raw = text(value, "receiving subaddress index");
    uint64_t index = 0;
    const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), index);
    if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size())
        throw std::runtime_error("Ambiguous receiving subaddress index");
    return index;
}

} // namespace

std::string walletIdentity(const Json& status)
{
    return text(status.at("network"), "network") + ":" + text(status.at("address"), "wallet address");
}

Json incomingPayments(const Json& status, const Json& history)
{
    const auto wallet = walletIdentity(status);
    if (!history.is_object() || !history.contains("rows") || !history["rows"].is_array())
        throw std::runtime_error("History contains no transaction rows array");
    std::map<std::string, Json> unique;
    for (const auto& row : history["rows"])
    {
        if (!row.is_object() || !row.contains("direction") || !row["direction"].is_string())
            throw std::runtime_error("History contains an invalid transaction direction");
        if (row["direction"] != "in")
            continue;
        if (!row.contains("failed") || !row["failed"].is_boolean())
            throw std::runtime_error("Incoming history row has no failure flag");
        if (row["failed"].get<bool>())
            continue;
        auto txid = text(row.at("txid"), "transaction hash");
        if (txid.size() != 64 ||
            !std::all_of(txid.begin(), txid.end(), [](unsigned char c) { return std::isxdigit(c); }))
            throw std::runtime_error("Invalid transaction hash");
        std::transform(txid.begin(), txid.end(), txid.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        const auto account = number(row.at("account"), "account index");
        const auto index = subaddress(row.at("subaddrIndex"));
        if (account > UINT32_MAX || index > UINT32_MAX)
            throw std::runtime_error("Invalid receiving account/index");
        const auto confirmations = number(row.at("confirmations"), "confirmations");
        const auto height = number(row.at("height"), "block height");
        const auto timestamp = number(row.at("timestamp"), "timestamp");
        if (!row.contains("pending") || !row["pending"].is_boolean())
            throw std::runtime_error("Incoming history row has no pending flag");
        const auto amount = std::to_string(atomicNumber(text(row.at("amount"), "atomic amount")));
        const bool pending = row["pending"].get<bool>();
        const auto id = wallet + ":" + txid + ":" + std::to_string(account) + ":" + std::to_string(index);
        Json payment{{"id", id},
                     {"txid", txid},
                     {"amount_atomic", amount},
                     {"account_index", account},
                     {"subaddress_index", index},
                     {"confirmations", confirmations},
                     {"pending", pending},
                     {"height", height},
                     {"timestamp", timestamp},
                     {"present", true}};
        const auto found = unique.find(id);
        if (found != unique.end() && found->second != payment)
            throw std::runtime_error("Conflicting incoming records for the same transaction and subaddress");
        unique[id] = std::move(payment);
    }
    Json payments = Json::array();
    for (const auto& item : unique)
        payments.push_back(item.second);
    std::sort(payments.begin(), payments.end(),
              [](const Json& a, const Json& b)
              {
                  if (a["timestamp"] != b["timestamp"])
                      return a["timestamp"] > b["timestamp"];
                  return a["id"] < b["id"];
              });
    return payments;
}
} // namespace merchant
