#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace merchant
{
// Throws on ambiguous or malformed incoming data instead of inventing a receipt.
nlohmann::json incomingPayments(const nlohmann::json& status, const nlohmann::json& history);
std::string walletIdentity(const nlohmann::json& status);
} // namespace merchant
