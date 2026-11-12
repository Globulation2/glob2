// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <vector>

namespace ResourceClaims
{
// Saved and checksummed with active claim routes: keep the values stable.
enum class RouteStatus { Found, Unreachable, CostLimit, ExpansionLimit };
struct Route
{
    RouteStatus status = RouteStatus::Unreachable;
    std::vector<std::uint32_t> cells; // Excludes start, includes terminal stance.
    std::uint32_t cost = 0, expansions = 0;
};
}
