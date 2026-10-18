// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "EntityRandom.h"
#include "Material.h"
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
    struct Episode {
        unsigned tick=0;int key=0;std::vector<int> features;
        std::vector<std::uint16_t> oldCells,newCells;
        unsigned queries=0;std::uint64_t refreshCpuNs=0;
        std::array<long long,4> defer{},refresh{};
    };
    // Diagnostic counters: O(material count) state, no spatial bins or per-cell history.
    struct SourceCounts { std::uint64_t count=0,added=0,removed=0; };
    std::array<SourceCounts,MaterialCount> sourceCounts{};
    std::map<const std::uint16_t*,SourceCounts> sourceCaptures;
    bool sourcesInitialized=false;
    void initializeSources(Map&);
    void stockChanged(unsigned before,unsigned after);
    void captureSources(const std::uint16_t*,int resource);
    void verifySources(Map&);
    std::ofstream study;
    void writeEpisode(const Episode&,bool complete);
    void finishStudy(unsigned tick);
    void stageEpisodes(Map&);
    struct Field {
        std::deque<Episode> episodes;
        unsigned lastEpisode=0,lastQuery=UINT32_MAX;
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
