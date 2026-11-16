#pragma once
#include <cstdint>
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
}
