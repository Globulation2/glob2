// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientWorkspace.h"
#include "GradientDestinationPlanes.h"
#include "Grid.h"
#include <limits>
#include <stdexcept>

namespace gradient_kernel
{
namespace destination_detail
{
// This is a separate specialization from the cost-only relaxation kernels:
// callers that do not request destinations pay no branch, load or allocation.
// All edges are positive. Consequently every possible equal-cost predecessor
// settles before its successor is expanded. A source-only improvement does not
// need another queue entry, even when scalar/SIMD/device traversal orders differ.
template<unsigned Buckets, bool Masked, class CostAt>
void propagate(std::uint16_t* gradient, DestinationPlanes destinations,
    int maxCost, field::Grid grid, GradientWorkspace& workspace, CostAt costAt)
{
    static_assert(Buckets == 64 || Buckets == 128 || Buckets == 256);
    if (!gradient || !destinations.coordinates ||
        grid.width() > DestinationPlanes::Invalid || grid.height() > DestinationPlanes::Invalid ||
        grid.cells() > std::size_t(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Invalid gradient destination planes or dimensions");

    GradientBucket* queue;
    if constexpr (Buckets == BUCKETS)
        queue = workspace.buckets.data();
    else {
        if (!workspace.terrain) workspace.terrain = std::make_unique<TerrainGradientWorkspace>();
        workspace.terrain->prepare(Buckets);
        queue = workspace.terrain->buckets.data();
    }
    for (unsigned b = 0; b < Buckets; ++b) queue[b].clear();
    auto& deferred = workspace.deferredSeeds;
    deferred.clear();
    const unsigned shift = Masked ? unsigned(grid.widthShift()) : 0;
    std::size_t pending = 0;
    for (std::size_t i = 0; i < grid.cells(); ++i) {
        if (gradient[i] <= GRADIENT_UNREACHABLE) {
            destinations.coordinates[i] = DestinationPlanes::InvalidCoordinate;
            continue;
        }
        destinations.set(i,std::uint16_t(Masked ? i & grid.maskX() : i % grid.width()),
            std::uint16_t(Masked ? i >> shift : i / grid.width()));
        const int cost = GRADIENT_AT_GOAL - gradient[i];
        if (cost < int(Buckets)) { queue[cost].push(std::uint32_t(i)); ++pending; }
        else deferred.emplace_back(cost, int(i));
    }
    std::sort(deferred.begin(), deferred.end());
    const int limit = std::min(maxCost, COST_LIMIT);
    std::size_t nextSeed = 0;
    for (int cur = 0; (pending || nextSeed < deferred.size()) && cur <= limit; ++cur) {
        if (!pending) cur = deferred[nextSeed].first;
        if (cur > limit) break;
        for (; nextSeed < deferred.size() && deferred[nextSeed].first == cur; ++nextSeed) {
            queue[unsigned(cur) % Buckets].push(std::uint32_t(deferred[nextSeed].second));
            ++pending;
        }
        auto& bucket = queue[unsigned(cur) % Buckets];
        const auto count = bucket.size;
        const auto value = std::uint16_t(GRADIENT_AT_GOAL - cur);
        for (std::size_t q = 0; q < count; ++q) {
            const std::size_t i = bucket.cells[q];
            if (gradient[i] != value) continue;
            const auto steps = costAt(i);
            if (!steps.cardinal || !steps.diagonal || steps.cardinal >= Buckets || steps.diagonal >= Buckets)
                throw std::invalid_argument("Destination gradient edge exceeds selected queue");
            const auto source = destinations.coordinates[i];
            const int x = Masked ? int(i & grid.maskX()) : int(i % grid.width());
            const int y = Masked ? int(i >> shift) : int(i / grid.width());
            const auto relax = [&](std::size_t n,unsigned cost) {
                const auto candidate=std::uint16_t(GRADIENT_AT_GOAL-cost);
                const auto old=gradient[n];
                if (!old || old>candidate) return;
                if (old==candidate && destinations.coordinates[n]<=source) return;
                destinations.coordinates[n]=source;
                if (old<candidate) {
                    gradient[n]=candidate;
                    queue[cost%Buckets].push(std::uint32_t(n)); ++pending;
                }
            };
            const auto left=std::size_t(grid.wrapX(x-1)),right=std::size_t(grid.wrapX(x+1));
            const auto north=std::size_t(grid.wrapY(y-1))*grid.width(),south=std::size_t(grid.wrapY(y+1))*grid.width();
            const auto middle=std::size_t(y)*grid.width();
            const unsigned diagonal=unsigned(cur)+steps.diagonal,cardinal=unsigned(cur)+steps.cardinal;
            if (diagonal<=unsigned(limit)) { relax(north+left,diagonal); relax(north+right,diagonal); relax(south+left,diagonal); relax(south+right,diagonal); }
            if (cardinal<=unsigned(limit)) { relax(north+x,cardinal); relax(middle+left,cardinal); relax(middle+right,cardinal); relax(south+x,cardinal); }

        }
        pending -= count;
        bucket.clear();
    }
}
} // namespace destination_detail

// Private worker inputs only; costAt(i) is the cost of entering i, matching
// reverse propagation in the legacy kernels. Reseed the cost plane each call.
template<unsigned Buckets = BUCKETS, class CostAt>
void propagateDestinationFieldCPU(std::uint16_t* gradient, DestinationPlanes destinations,
    int maxCost, field::Grid grid, GradientWorkspace& workspace, CostAt costAt)
{
    if (grid.powerOfTwo())
        destination_detail::propagate<Buckets, true>(gradient, destinations, maxCost, grid, workspace, costAt);
    else
        destination_detail::propagate<Buckets, false>(gradient, destinations, maxCost, grid, workspace, costAt);
}
} // namespace gradient_kernel
