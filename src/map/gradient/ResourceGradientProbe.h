// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "EntityRandom.h"
#include "field/GradientWorkspace.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <deque>
#include <map>
#include <vector>

class Map;

// Optional, non-simulation diagnostic. Owns private CPU fields and never uses
// the simulation RNG. Its state is deliberately absent from saves/checksums.
struct ResourceGradientProbe
{
    explicit ResourceGradientProbe(const char* path);
    struct Reference {
        std::vector<std::uint16_t> seeds, cells;
        std::vector<std::uint8_t> sources;
        std::uint32_t tick = 0;
    };
    static constexpr std::array<unsigned,9> intervals{1,2,4,8,16,32,64,128,256};
    std::ofstream output;
    std::map<const std::uint16_t*,std::uint32_t> captured;
    GradientWorkspace scratch;
    struct Field {
        std::array<Reference,intervals.size()> references, delayed;
        std::array<std::deque<Reference>,intervals.size()> pending;
        std::array<std::uint32_t,intervals.size()> lastScheduled{};
        std::uint32_t firstTick=0;
    };
    std::map<int,Field> fields;
    std::uint32_t window = UINT32_MAX;
    std::uint64_t query = 0;
    void observe(Map&, int team, int resource, int swim, int x, int y,
        const std::uint16_t* live, EntityRandom random, bool moved, int dx, int dy);
};
