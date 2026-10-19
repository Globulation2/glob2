// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

// Semantic inference contract, deliberately independent of fitted coefficients.
// Family/resource identities never enter these arrays.
namespace AdaptiveGradientFeatures {
using Base=std::array<std::int64_t,7>; // age,q32,q128,added,removed,added/goalCount,removed/goalCount
using Counts=std::array<std::uint64_t,3>; // invalid goal, reachable-no-move, eligible movement queries
inline constexpr unsigned CounterMask=(1u<<0)|(1u<<4)|(1u<<5);
inline constexpr std::array<unsigned,3> CounterIds{0,4,5};
}
