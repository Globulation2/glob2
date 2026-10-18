// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EntityRandom.h"
#include "ResourceGradientPolicy.h"
#include "field/GradientWorkspace.h"
#include <deque>
#include <fstream>
#include <map>
#include <vector>
class Map;

// Opt-in observer only. Reference fields, episode copies, output streams and
// sampling bookkeeping are allocated only when --resource-gradient-probe is on.
// It never changes a live field, the scheduler state or a unit's RNG.
struct ResourceGradientProbe
{
    explicit ResourceGradientProbe(const char* path);
    ~ResourceGradientProbe();
    struct Episode {
        unsigned tick=0, delay=8, queries=0, planningQueries=0;
        unsigned behaviorInterval=0, behaviorSeed=0;
        bool movementObserved=false;
        // Reachability errors are counts, never added to movement direction cost.
        unsigned deferFalseNegative=0, deferFalsePositive=0;
        unsigned refreshFalseNegative=0, refreshFalsePositive=0;
        std::array<std::int64_t,7> inputs{};
        std::vector<std::uint16_t> deferred,refreshed;
        std::int64_t deferExtra=0,refreshExtra=0;
    };
    struct Field {
        unsigned firstTick=0,lastQuery=0,lastEpisode=0;
        bool episodeKnown=false, movementObserved=false;
        std::deque<Episode> episodes;
    };
    std::ofstream output,study,planning;
    GradientWorkspace scratch;
    std::map<int,Field> fields;
    unsigned window=UINT32_MAX,lastTick=0;
    std::uint64_t query=0,planningQuery=0;
    Field* sampleField(unsigned tick,int key);
    void rebuildFresh(Map&,int team,int resource,int swim,std::vector<std::uint16_t>&);
    void observePlanning(Map&,int team,int resource,int swim,int x,int y,const std::uint16_t* live);
    void stageEpisodes(Map&);
    void finishStudy(unsigned tick);
    void writeEpisode(int key,const Episode&,bool complete);
    void observe(Map&,int team,int resource,int swim,int x,int y,
        const std::uint16_t* live,EntityRandom random);
};
