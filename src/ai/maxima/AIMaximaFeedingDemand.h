// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <vector>
#include "UnitTiming.h"
#include "AIMaximaFoodLedger.h"

namespace AIMaxima
{
// Retain fractional rates while summing a population, including very slow
// hunger clocks. The external workload is uniform compass-direction travel:
// cardinal and diagonal actions have equal weight and use engine quantization.
// Hungry travel to the provider continues consuming food; service/entry/exit
// pause hunger. Include that excursion once, independently of carried stock.
inline constexpr long long MealRatePrecision=1024;
// Return units are FoodLedger::RateScale * MealRatePrecision meals/tick.
inline long long recipientMealRate(int hungerMaximum,int hungerTrigger,int hungriness,
    int movementSpeed,int movementAction,int approachTiles,int servicePauseTicks)
{
    if(hungriness<=0 || hungerTrigger>=hungerMaximum)return 0;
    constexpr long long precision=MealRatePrecision;
    const long long actions=(static_cast<long long>(hungerMaximum)-std::max(0,hungerTrigger)+hungriness-1)/hungriness;
    const int straight=std::clamp(movementSpeed,1,UNIT_DELTA_QUANTUM);
    const int diagonal=std::clamp(unitActionStepSpeed(straight,movementAction,1,1),1,UNIT_DELTA_QUANTUM);
    const long long actionTicks=(UNIT_DELTA_QUANTUM*precision/straight+UNIT_DELTA_QUANTUM*precision/diagonal)/2;
    const long long cycle=(actions+std::max(0,approachTiles)+2)*actionTicks
        +static_cast<long long>(std::max(0,servicePauseTicks))*precision;
    return AIMaximaFoodLedger::RateScale*precision*precision/std::max(precision,cycle);
}

// Uniform growth of the observed recipient mix is feasible exactly when every
// recipient subset can reach enough shared service capacity (the seven cuts of
// this three-class bipartite network). Flexible seats are never counted twice.
inline int feedingPopulationCapacity(const std::array<int,3>& demand,int population,
    const std::array<long long,8>& visitsByAdmission)
{
    if(population<=0)return 0;
    long long result=1000000;
    for(unsigned subset=1;subset<8;++subset) {
        long long required=0,available=0;
        for(int unit=0;unit<3;++unit)if(subset&(1u<<unit))required+=std::max(0,demand[unit]);
        if(!required)continue;
        for(unsigned admitted=1;admitted<8;++admitted)
            if(admitted&subset)available+=std::max(0LL,visitsByAdmission[admitted]);
        // Bound before multiplying, also for diagnostic fixtures with huge rates.
        if(available>=(required*1000000+population-1)/population)continue;
        result=std::min(result,available*population/required);
    }
    return int(result);
}

struct FeedingProvider
{
    int colony=0;
    int capacity=0;
    unsigned admitted=0;
};

// Cold planner allocation. Each recipient class is charged once, and one
// physical provider has a single shared visit budget across all classes.
inline std::vector<long long> allocateFeedingDemand(
    const std::vector<std::array<int,3>>& demand,
    const std::vector<FeedingProvider>& providers)
{
    std::vector<long long> result(providers.size()),remaining;
    remaining.reserve(providers.size());
    for(const auto& p:providers)remaining.push_back(std::max(0,p.capacity));
    for(size_t colony=0;colony<demand.size();++colony) {
        std::array<long long,3> capacity{};
        for(size_t p=0;p<providers.size();++p)if(providers[p].colony==int(colony))
            for(int unit=0;unit<3;++unit)if(providers[p].admitted&(1u<<unit))capacity[unit]+=remaining[p];
        std::array<int,3> order{0,1,2};
        std::stable_sort(order.begin(),order.end(),[&](int a,int b) {
            const long long da=std::max(1,demand[colony][a]),db=std::max(1,demand[colony][b]);
            const auto qa=capacity[a]/da,qb=capacity[b]/db;
            return qa!=qb ? qa<qb : (capacity[a]%da)*db<(capacity[b]%db)*da;
        });
        for(int unit:order) {
            long long wanted=std::max(0,demand[colony][unit]);
            // Use dedicated capacity before consuming flexible providers.
            for(int flexibility=1;flexibility<=3 && wanted>0;++flexibility) {
                const auto eligible=[&](size_t p) {
                    return providers[p].colony==int(colony) && (providers[p].admitted&(1u<<unit))
                        && std::popcount(providers[p].admitted&7u)==flexibility;
                };
                long long available=0;
                for(size_t p=0;p<providers.size();++p)if(eligible(p))available+=remaining[p];
                const long long assigned=std::min(wanted,available);
                if(!assigned)continue;
                long long outstanding=assigned;
                for(size_t p=0;p<providers.size();++p)if(eligible(p)) {
                    const long long share=assigned*remaining[p]/available;
                    result[p]+=share;remaining[p]-=share;outstanding-=share;
                }
                // Only rounding residue remains; stable provider order gives
                // exact integer conservation without per-meal iteration.
                for(size_t p=0;p<providers.size() && outstanding;++p)if(eligible(p) && remaining[p]) {
                    ++result[p];--remaining[p];--outstanding;
                }
                wanted-=assigned;
            }
        }
    }
    return result;
}
}
