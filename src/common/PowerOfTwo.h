// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <type_traits>

// The divisor must be a positive power of two (as live map dimensions are).
// Preserve C++ remainder semantics, including negative dividends and the usual
// signed/unsigned promotions. Coordinate-normalization callers should prefer
// their map's normalizeX/normalizeY helpers when they need a nonnegative result.
template<class Dividend, class Divisor>
constexpr auto powerOfTwoRemainder(Dividend dividend, Divisor divisor)
    -> decltype(dividend % divisor)
{
    using Result = decltype(dividend % divisor);
    using Unsigned = std::make_unsigned_t<Result>;
    const Result value = static_cast<Result>(dividend);
    const Unsigned bits = static_cast<Unsigned>(value);
    const Unsigned mask = static_cast<Unsigned>(divisor) - 1;
    if constexpr (std::is_signed_v<Result>)
        if (value < 0)
            return -static_cast<Result>((Unsigned{0} - bits) & mask);
    return static_cast<Result>(bits & mask);
}

// Standalone grid utilities also accept arbitrary dimensions. Their map-sized
// calls use masks, while other callers retain the ordinary remainder contract.
template<class Dividend, class Divisor>
constexpr auto dimensionRemainder(Dividend dividend, Divisor divisor)
    -> decltype(dividend % divisor)
{
    if (divisor > 0 && (divisor & (divisor - 1)) == 0)
        return powerOfTwoRemainder(dividend, divisor);
    return dividend % divisor;
}
