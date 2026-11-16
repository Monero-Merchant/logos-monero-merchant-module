#pragma once
#include <cstdint>
#include <boost/multiprecision/cpp_int.hpp>
#include <limits>
#include <stdexcept>

namespace merchant {
// Convert integer USD cents at a scaled USD/XMR price, rounding up to atomic units.
inline uint64_t usdToAtomic(uint64_t cents, uint64_t price, uint64_t decimals)
{
    if (!cents || cents > 9999999999ULL || !price || decimals > 12)
        throw std::runtime_error("Invalid USD amount or oracle price");
    unsigned __int128 scale = 1;
    for (uint64_t i = 0; i < decimals; ++i) scale *= 10;
    const unsigned __int128 numerator = static_cast<unsigned __int128>(cents) * scale * 1000000000000ULL;
    const unsigned __int128 denominator = static_cast<unsigned __int128>(price) * 100;
    const auto atomic = (numerator + denominator - 1) / denominator;
    if (!atomic || atomic > std::numeric_limits<uint64_t>::max())
        throw std::runtime_error("Converted XMR amount is out of range");
    return static_cast<uint64_t>(atomic);
}
// EUR cents × USD/EUR ÷ USD/XMR, with no intermediate rounding.
inline uint64_t eurToAtomic(uint64_t cents, uint64_t xmrPrice, uint64_t xmrDecimals,
                            uint64_t eurPrice, uint64_t eurDecimals)
{
    using boost::multiprecision::cpp_int;
    if (!cents || cents > 9999999999ULL || !xmrPrice || !eurPrice || xmrDecimals > 12 || eurDecimals > 12)
        throw std::runtime_error("Invalid EUR amount or oracle price");
    cpp_int xmrScale = 1, eurScale = 1;
    for (uint64_t i = 0; i < xmrDecimals; ++i) xmrScale *= 10;
    for (uint64_t i = 0; i < eurDecimals; ++i) eurScale *= 10;
    const cpp_int numerator = cpp_int(cents) * eurPrice * xmrScale * 1000000000000ULL;
    const cpp_int denominator = cpp_int(100) * eurScale * xmrPrice;
    const cpp_int atomic = (numerator + denominator - 1) / denominator;
    if (atomic <= 0 || atomic > std::numeric_limits<uint64_t>::max())
        throw std::runtime_error("Converted XMR amount is out of range");
    return atomic.convert_to<uint64_t>();
}
}
