// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

// Simulation-thread-only derived fields. Every miss rebuilds the complete field,
// so eviction, query order and save/load cannot alter the chosen direction.
struct TerrainSafetyCache
{
    struct Field
    {
        static constexpr std::uint32_t Unreachable = std::numeric_limits<std::uint32_t>::max();
        static constexpr std::uint32_t Blocked = Unreachable - 1;
        std::vector<std::uint32_t> costs;
        std::uint64_t generation = 0;
        std::uint64_t used = 0;
    };

    // This bounds retained cells, not the temporary propagation queue. Keep at
    // least one field if an exceptionally large map itself exceeds the budget.
    static constexpr std::size_t MaximumBytes = 64u * 1024u * 1024u;
    std::map<unsigned, Field> fields;
    std::uint64_t groundGeneration = 1, airGeneration = 1;
    std::uint64_t clock = 0, builds = 0;

    void invalidate(bool ground, bool air)
    {
        if ((ground && ++groundGeneration == 0) || (air && ++airGeneration == 0))
            reset(); // Never let generation wrap make a stale field appear fresh.
    }

    void reset()
    {
        fields.clear();
        groundGeneration = airGeneration = 1;
        clock = builds = 0;
    }

    Field& acquire(unsigned key, std::size_t cells)
    {
        auto found = fields.find(key);
        if (found == fields.end())
        {
            const auto capacity = std::max<std::size_t>(1, MaximumBytes / (cells * sizeof(std::uint32_t)));
            if (fields.size() >= capacity)
            {
                const auto oldest = std::min_element(fields.begin(), fields.end(),
                    [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
                fields.erase(oldest);
            }
            found = fields.try_emplace(key).first;
        }
        if (++clock == 0)
        {
            for (auto& entry : fields) entry.second.used = 0;
            clock = 1;
        }
        found->second.used = clock;
        return found->second;
    }
};
