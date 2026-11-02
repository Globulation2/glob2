// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Building.h"
#include "EntityRandom.h"
#include "GradientFieldIdentity.h"
#include "AdaptiveGradientFeatures.h"
#include <array>
#include <cstdint>
#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <tuple>
class Map;
class Unit;
class Building;

// Diagnostic reader context is thread-local, scoped to a unit's owner step;
// it never participates in simulation state, snapshots, or random streams.
struct RoutingRatioReader {
    static inline thread_local const Unit* current=nullptr;
    const Unit* previous;
    explicit RoutingRatioReader(const Unit* unit):previous(current){current=unit;}
    ~RoutingRatioReader(){current=previous;}
};
struct RoutingRatioStudy {
    enum Family { Materials,Markets,Guard,Clear,Forbidden,Consumer,BuildingField,Point };
    using QueryKey=std::tuple<unsigned,unsigned,unsigned,unsigned,bool>;
    std::map<QueryKey,std::uint64_t> queries;
    std::ofstream counts,state,routes,process,work,ticks,kernel,model,points;
    struct ModelTotals {
        std::uint64_t evaluations=0,above=0;
        std::int64_t predictionSum=0;
        std::array<std::int64_t,7> inputSum{};
        std::array<std::uint64_t,3> countSum{};
    };
    std::map<unsigned,ModelTotals> modelTotals;
    std::map<std::tuple<unsigned,unsigned,unsigned>,std::pair<std::uint64_t,std::uint64_t>> pointTotals;
    void pointResult(unsigned tick,unsigned team,unsigned physical,unsigned selected,bool reachable) {
        if(window!=tick/512){flush();window=tick/512;windowSamples.fill(0);}
        auto& n=pointTotals[{team,physical,selected}];++n.first;n.second+=reachable;
    }
    void modelPrediction(unsigned tick,GradientFieldKey key,const AdaptiveGradientFeatures::Base& input,
        const AdaptiveGradientFeatures::Counts& counts,int prediction,std::int64_t threshold) {
        if(window!=tick/512){flush();window=tick/512;windowSamples.fill(0);}
        auto& n=modelTotals[(unsigned(key.family)*32+key.team)*9+key.swim];
        ++n.evaluations;n.above+=prediction>=threshold;n.predictionSum+=prediction;
        for(unsigned i=0;i<input.size();++i)n.inputSum[i]+=input[i];
        for(unsigned i=0;i<counts.size();++i)n.countSum[i]+=counts[i];
    }
    std::array<std::array<std::array<std::uint64_t,9>,8>,7> previousWork{};
    std::array<std::uint64_t,64> tickHistogram{};
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now(),tickStarted;
    double cpuStarted=0;
    void beginTick(){tickStarted=std::chrono::steady_clock::now();}
    void endTick();
    bool collectRoutes=false;
    unsigned window=0,routeSamples=0;
    std::array<unsigned,8> windowSamples{};
    std::uint64_t ordinal=0,eligible=0,candidates=0,capped=0;
    explicit RoutingRatioStudy(const std::string& prefix,bool routeCollection);
    ~RoutingRatioStudy();
    void flush();
    void sample(Map&);
    void observe(Map&,unsigned family,int team,int physical,int x,int y,bool planning=false,
        int material=0,Building* building=nullptr,BuildingRoute route=BuildingRoute::Automatic);
};
