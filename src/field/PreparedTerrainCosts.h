// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TerrainMovementCosts.h"

namespace gradient_kernel
{
// Immutable movement metadata shared by searches using the same swimming profile.
// Terrain IDs map to classes of equal (cardinal, diagonal) costs. Classes then
// map each direction to a slot for its unique edge cost: a cardinal cost may
// share a slot with another class's diagonal cost. Mutable queues stay private
// to each search; slots select their append cursors for the current cost layer.
// Every reachable class must have positive costs below the selected queue size,
// so a future layer cannot alias the bucket being expanded. Built-in tables use
// BUCKETS; runtime searches validate their reachable profiles against a 64-, 128-
// or 256-bucket ring before expansion. Unused registry profiles may exceed that
// ring and are skipped when reserving append cursors.
template<std::size_t N>
struct PreparedTerrainCosts
{
    // Every terrain ID has a class. Only [0, classCount) of the class arrays and
    // [0, stepCount) of the edge-slot arrays below contain prepared entries.
    std::array<std::uint16_t, N> terrainClasses{};
    std::array<EntrySteps, N> classes{};
    std::array<std::uint16_t, N> cardinalSlots{}, diagonalSlots{};
    std::array<std::uint16_t, 2 * N> steps{};
    std::array<std::uint8_t, 2 * N> maxAppends{};
    unsigned classCount = 0, stepCount = 0;

    constexpr explicit PreparedTerrainCosts(const std::array<EntrySteps, N> &costs)
    {
        for (unsigned t = 0; t < N; ++t)
        {
            const auto cost = costs[t];
            unsigned c = 0;
            while (c < classCount && (classes[c].cardinal != cost.cardinal || classes[c].diagonal != cost.diagonal))
                ++c;
            terrainClasses[t] = c;
            if (c < classCount) continue;
            classes[classCount++] = cost;
            cardinalSlots[c] = slot(cost.cardinal);
            diagonalSlots[c] = slot(cost.diagonal);
            // One expanded cell uses one class, so reserve the maximum per-cell
            // attempts for this slot, not the sum across classes. Equal costs
            // within a class share all eight attempts; otherwise each has four.
            // Include rejected attempts: relaxation writes before advancing.
            const unsigned maximum = cost.cardinal == cost.diagonal ? 8 : 4;
            maxAppends[cardinalSlots[c]] = std::max<unsigned>(maxAppends[cardinalSlots[c]], maximum);
            maxAppends[diagonalSlots[c]] = std::max<unsigned>(maxAppends[diagonalSlots[c]], maximum);
        }
    }

private:
    constexpr unsigned slot(unsigned step)
    {
        unsigned s = 0;
        while (s < stepCount && steps[s] != step) ++s;
        if (s == stepCount) steps[stepCount++] = step;
        return s;
    }
};

inline constexpr auto PREPARED_TERRAIN_COSTS = [] {
    // Explicit initialization avoids a default constructor for invalid profiles.
    return std::array<PreparedTerrainCosts<TERRAIN_COUNT>, 7>{
        PreparedTerrainCosts<TERRAIN_COUNT>(TERRAIN_ENTRY_COSTS[0]),
        PreparedTerrainCosts<TERRAIN_COUNT>(TERRAIN_ENTRY_COSTS[1]),
        PreparedTerrainCosts<TERRAIN_COUNT>(TERRAIN_ENTRY_COSTS[2]),
        PreparedTerrainCosts<TERRAIN_COUNT>(TERRAIN_ENTRY_COSTS[3]),
        PreparedTerrainCosts<TERRAIN_COUNT>(TERRAIN_ENTRY_COSTS[4]),
        PreparedTerrainCosts<TERRAIN_COUNT>(TERRAIN_ENTRY_COSTS[5]),
        PreparedTerrainCosts<TERRAIN_COUNT>(TERRAIN_ENTRY_COSTS[6])};
}();
static_assert(PREPARED_TERRAIN_COSTS.size() == TERRAIN_ENTRY_COSTS.size());
}
