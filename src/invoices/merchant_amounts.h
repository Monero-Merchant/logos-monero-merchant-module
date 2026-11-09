#pragma once
#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace merchant
{
inline uint64_t atomicNumber(const std::string& value)
{
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Invalid atomic amount");
    uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("Atomic amount exceeds uint64");
    return result;
}
inline std::string xmrAmount(uint64_t amount)
{
    auto value = std::to_string(amount);
    if (value.size() < 13)
        value.insert(0, 13 - value.size(), '0');
    value.insert(value.size() - 12, ".");
    return value;
}
inline std::string parseXmrAmount(const std::string& value)
{
    const auto dot = value.find('.');
    auto whole = value.substr(0, dot);
    auto fraction = dot == std::string::npos ? std::string() : value.substr(dot + 1);
    if (whole.empty() || whole.find_first_not_of("0123456789") != std::string::npos ||
        fraction.find_first_not_of("0123456789") != std::string::npos || fraction.size() > 12 ||
        (dot != std::string::npos && fraction.empty()))
        throw std::runtime_error("Enter a positive XMR amount with at most 12 decimal places.");
    fraction.append(12 - fraction.size(), '0');
    auto combined = whole + fraction;
    const auto first = combined.find_first_not_of('0');
    if (first == std::string::npos)
        throw std::runtime_error("Invoice amount must be greater than zero.");
    combined.erase(0, first);
    return std::to_string(atomicNumber(combined));
}
inline uint64_t addAmount(uint64_t total, uint64_t amount)
{
    if (amount > std::numeric_limits<uint64_t>::max() - total)
        throw std::runtime_error("Payment total exceeds uint64");
    return total + amount;
}
} // namespace merchant
