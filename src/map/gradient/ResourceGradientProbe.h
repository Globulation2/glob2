// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientQueryContext.h"
#include "AdaptiveGradientState.h"
#include "ResourceGradientContradictions.h"
#include "field/GradientWorkspace.h"
#include "SnapshotGradient.h"
#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <vector>
class Map;

// All-family observer; the historical filename/CLI is retained for compatibility.
// No instance, snapshots, oracle selectors or streams exist with collection off.
// The collector contract is separate from the H128 model label contract.
struct ResourceGradientProbe {
    static constexpr const char* CollectorContract="unified-gradient-h128-v4";
    // Capture-anchored outcome window: default delay8 gives offsets [8,127].
    static constexpr unsigned HorizonTicks=128;
    static constexpr bool labelEligible(unsigned age,unsigned delay){return age>=delay && age<HorizonTicks;}
    static constexpr bool complete(unsigned age){return age>=HorizonTicks;}
    explicit ResourceGradientProbe(const char* path);
    ~ResourceGradientProbe();
    struct Episode {
        unsigned tick=0,delay=8,queries=0,planningQueries=0,behaviorInterval=0,behaviorSeed=0;
        bool movementObserved=false,contradictionCaptureKnown=false;
        unsigned guardBalanceQueries=0;
        unsigned deferFalseUnreachable=0,refreshFalseUnreachable=0,deferFalseReachable=0,refreshFalseReachable=0;
        unsigned deferStalls=0,refreshStalls=0,deferInvalidGoals=0,refreshInvalidGoals=0;
        unsigned deferMissedMoves=0,refreshMissedMoves=0,deferEscapeFailures=0,refreshEscapeFailures=0;
        unsigned deferFalseNegative=0,deferFalsePositive=0,refreshFalseNegative=0,refreshFalsePositive=0;
        std::uint64_t routeChanges=0,seedCostChanges=0,capturedGoalCount=0;
        std::uint32_t servingCaptureTick=0;
        AdaptiveGradientState::Targets capturedTargets,currentTargets;
        ResourceGradientContradictionCounts contradictions;
        AdaptiveGradientFeatures::Base inputs{};
        std::vector<std::uint16_t> deferred,refreshed;
        std::int64_t deferExtra=0,refreshExtra=0;
    };
    struct Field { unsigned firstTick=0,lastQuery=0,lastEpisode=0;bool episodeKnown=false,movementObserved=false;std::deque<Episode> episodes; };
    struct MovementHistory {
        ResourceGradientContradictionCounts total;
    };
    // Packed stable keys preserve deterministic iteration and sparse enrollment.
    std::map<int,Field> fields;
    std::map<int,MovementHistory> movementHistory;
    std::ofstream output,study,planning,movement;
    unsigned movementEpoch=0,window=UINT32_MAX,lastTick=0;
    std::uint64_t query=0,planningQuery=0;
    GradientWorkspace scratch;
    gradient_preparation::CrowdingScratch seedScratch;
    void recordMovement(GradientFieldKey key,const GradientMovementEvidence& e) {movementHistory[int(key.packed())].total.record(e);}
    void resetMovement() {writeMovementTotals();movementHistory.clear();++movementEpoch;}
    void writeMovementTotals();
    Field* sampleField(unsigned tick,GradientFieldKey);
    void rebuildFresh(Map&,GradientFieldKey,std::vector<std::uint16_t>&);
    void observe(Map&,const GradientQueryContext&,const std::uint16_t* live);
    void observePlanning(Map&,GradientFieldKey,int x,int y,const std::uint16_t* live);
    void stageEpisodes(Map&);
    void finishStudy(unsigned tick);
    void writeEpisode(int key,const Episode&,bool complete);
};
