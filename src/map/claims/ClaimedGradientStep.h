// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/GradientDestinationPlanes.h"
#include "field/Grid.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace ResourceClaims
{
struct ClaimedGradientStep
{
    enum class Status { Unavailable, Ready, Pending, Arrived };
    Status status = Status::Unavailable;
    int dx = 0, dy = 0;
};

// The caller retains the publication lease for both field planes throughout
// this read. Geometry excludes moving AND immobile traffic; traffic is a
// separate predicate so congestion never masquerades as a broken route.
// No allocation, stochastic sidestep, source reselection or farm flood occurs.
template<class Geometry, class Traffic, class EntryCost>
ClaimedGradientStep claimedGradientStep(field::Grid grid,
    std::span<const std::uint16_t> values, const gradient_kernel::OwnedDestinations& destinations,
    std::uint32_t current, std::uint32_t access,
    Geometry geometry, Traffic traffic, EntryCost entryCost)
{
    using Status = ClaimedGradientStep::Status;
    const auto cells = grid.cells();
    if (cells > std::size_t(std::numeric_limits<int>::max()) || current >= cells || access >= cells)
        return {};
    const int width = grid.width(), height = grid.height();
    const int x = int(current % width), y = int(current / width);
    const int targetX = int(access % width), targetY = int(access / width);
    const int distanceX = x > targetX ? x - targetX : targetX - x;
    const int distanceY = y > targetY ? y - targetY : targetY - y;
    // A harvest stance is the wrapped 3x3 neighborhood of this exact access.
    // An unrelated gradient peak or a probe endpoint never means arrival.
    if (std::min(distanceX, width - distanceX) <= 1 &&
        std::min(distanceY, height - distanceY) <= 1)
        return {Status::Arrived};
    if (values.size() != cells || destinations.cells != cells || !destinations.coordinates)
        return {};
    const auto matches = [&](std::size_t index) {
        return destinations.x(index) == targetX && destinations.y(index) == targetY;
    };
    const auto here = values[current];
    if (here <= 1 || !matches(current)) return {};
    // Match Map::directionByGradient's tabClose ordering and weighted ranking.
    static constexpr std::array<field::Offset, 8> directions{{
        {-1,-1}, {0,-1}, {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}}};
    bool congested = false;
    std::int64_t best = -1;
    ClaimedGradientStep result;
    for (const auto direction : directions) {
        const auto next = std::uint32_t(grid.index(x + direction.x, y + direction.y));
        const auto value = values[next];
        if (value <= here || !matches(next) || !geometry(next)) continue;
        const auto cost = entryCost(next, direction.x != 0 && direction.y != 0);
        if (!cost) continue;
        const auto score = std::int64_t(value) - std::int64_t(cost);
        if (score < 0) continue;
        if (!traffic(next)) { congested = true; continue; }
        if (score > best) {
            best = score;
            result = {Status::Ready, direction.x, direction.y};
        }
    }
    if (result.status != Status::Ready && congested) result.status = Status::Pending;
    return result;
}
}
