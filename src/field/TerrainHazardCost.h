// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>

namespace gradient_kernel
{
// Recovery-time preference, calibrated offline. Health rates are Q8 HP/tick.
// Absolute unit speed cancels when travel + recovery is expressed in field units.
inline constexpr unsigned TERRAIN_DAMAGE_TICKS_PER_HP = 20;
// Retain the compact 256-bucket fields, including their diagonal edges. Extreme
// authored hazards saturate rather than invalidating previously readable maps.
inline constexpr unsigned MAX_TERRAIN_ROUTE_CARDINAL = 181;
constexpr unsigned hazardRouteCost(unsigned travelCost, int healthQ8)
{
    const auto damage = unsigned(std::max(0, -healthQ8));
    const auto weighted = (std::uint64_t(travelCost) *
        (256u + TERRAIN_DAMAGE_TICKS_PER_HP * damage) + 128u) / 256u;
    return unsigned(std::min<std::uint64_t>(MAX_TERRAIN_ROUTE_CARDINAL, weighted));
}
}
