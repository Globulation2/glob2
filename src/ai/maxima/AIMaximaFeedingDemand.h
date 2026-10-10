// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <vector>
#include <queue>
#include <limits>
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

// The admission graph represents configured recipient/provider eligibility.
// Integer feasibility preserves exact floor rounding without enumerating 2^N cuts.
struct FeedingCapacity {
    long long rate=0;
    std::vector<Uint8> admitted;
};
inline int feedingPopulationCapacity(const std::vector<int>& demand,int population,
    const std::vector<FeedingCapacity>& providers)
{
    if(population<=0)return 0;
    // The built-in three-recipient graph has exactly seven nonempty cuts.
    // Keep that fixed-size case allocation-free; larger catalogs use the
    // bounded graph algorithm below rather than general subset enumeration.
    if(demand.size()==NB_UNIT_TYPE) {
        std::array<int,3> stockDemand{};
        long long totalDemand=0;
        for(unsigned unit=0;unit<NB_UNIT_TYPE;++unit) {
            stockDemand[unit]=std::max(0,demand[unit]);totalDemand+=stockDemand[unit];
        }
        const long long sufficient=(totalDemand*1000000+population-1)/population;
        std::array<long long,8> rates{};
        for(const auto& provider:providers) {
            unsigned mask=0;
            for(unsigned unit=0;unit<NB_UNIT_TYPE && unit<provider.admitted.size();++unit)
                if(provider.admitted[unit])mask|=1u<<unit;
            // Rates beyond the maximum supported population cannot affect a
            // cut, so saturating also protects malformed diagnostic inputs.
            rates[mask]+=std::min(std::max(0LL,provider.rate),sufficient-rates[mask]);
        }
        return feedingPopulationCapacity(stockDemand,population,rates);
    }
    struct Edge{int to,reverse;long long capacity;};
    const int source=0,firstRecipient=1,firstProvider=1+int(demand.size());
    const int sink=firstProvider+int(providers.size());
    auto feasible=[&](int candidate) {
        std::vector<std::vector<Edge>> graph(sink+1);
        auto add=[&](int a,int b,long long capacity) {
            graph[a].push_back({b,int(graph[b].size()),capacity});
            graph[b].push_back({a,int(graph[a].size()-1),0});
        };
        long long required=0;
        for(size_t unit=0;unit<demand.size();++unit) {
            const long long amount=static_cast<long long>(std::max(0,demand[unit]))*candidate;
            required+=amount;add(source,firstRecipient+unit,amount);
        }
        if(!required)return true;
        for(size_t p=0;p<providers.size();++p) {
            const long long rate=std::max(0LL,providers[p].rate);
            const long long capacity=rate>=required/population+1?required:std::min(required,rate*population);
            add(firstProvider+p,sink,capacity);
            for(size_t unit=0;unit<demand.size() && unit<providers[p].admitted.size();++unit)
                if(demand[unit]>0 && providers[p].admitted[unit])add(firstRecipient+unit,firstProvider+p,required);
        }
        long long flow=0;
        std::vector<int> level(sink+1),cursor(sink+1);
        auto augment=[&](auto&& self,int node,long long amount)->long long {
            if(node==sink)return amount;
            for(int& index=cursor[node];index<int(graph[node].size());++index) {
                auto& edge=graph[node][index];
                if(edge.capacity && level[edge.to]==level[node]+1) {
                    const auto sent=self(self,edge.to,std::min(amount,edge.capacity));
                    if(sent){edge.capacity-=sent;graph[edge.to][edge.reverse].capacity+=sent;return sent;}
                }
            }
            return 0;
        };
        while(flow<required) {
            std::fill(level.begin(),level.end(),-1);level[source]=0;
            std::queue<int> queue;queue.push(source);
            while(!queue.empty()) {
                const int node=queue.front();queue.pop();
                for(const auto& edge:graph[node])if(edge.capacity && level[edge.to]<0){level[edge.to]=level[node]+1;queue.push(edge.to);}
            }
            if(level[sink]<0)return false;
            std::fill(cursor.begin(),cursor.end(),0);
            while(const auto sent=augment(augment,source,required-flow))flow+=sent;
        }
        return true;
    };
    int low=0,high=1000000;
    while(low<high){const int mid=low+(high-low+1)/2;if(feasible(mid))low=mid;else high=mid-1;}
    return low;
}

struct FeedingProvider
{
    int colony=0;
    int capacity=0;
    unsigned admitted=0;
    std::vector<Uint8> recipients;
    bool accepts(unsigned unit) const {return recipients.empty()?unit<3 && (admitted&(1u<<unit)):unit<recipients.size() && recipients[unit];}
    unsigned flexibility() const {return recipients.empty()?std::popcount(admitted&7u):std::count(recipients.begin(),recipients.end(),Uint8(1));}
};

// Cold planner allocation. Each recipient class is charged once, and one
// physical provider has a single shared visit budget across all classes.
template<class Demand> inline std::vector<long long> allocateFeedingDemand(
    const std::vector<Demand>& demand,
    const std::vector<FeedingProvider>& providers)
{
    std::vector<long long> result(providers.size()),remaining;
    remaining.reserve(providers.size());
    for(const auto& p:providers)remaining.push_back(std::max(0,p.capacity));
    for(size_t colony=0;colony<demand.size();++colony) {
        std::vector<long long> capacity(demand[colony].size());
        for(size_t p=0;p<providers.size();++p)if(providers[p].colony==int(colony))
            for(unsigned unit=0;unit<capacity.size();++unit)if(providers[p].accepts(unit))capacity[unit]+=remaining[p];
        std::vector<int> order(capacity.size());
        for(unsigned i=0;i<order.size();++i)order[i]=i;
        std::stable_sort(order.begin(),order.end(),[&](int a,int b) {
            const long long da=std::max(1,demand[colony][a]),db=std::max(1,demand[colony][b]);
            const auto qa=capacity[a]/da,qb=capacity[b]/db;
            return qa!=qb ? qa<qb : (capacity[a]%da)*db<(capacity[b]%db)*da;
        });
        for(int unit:order) {
            long long wanted=std::max(0,demand[colony][unit]);
            // Use dedicated capacity before consuming flexible providers.
            for(int flexibility=1;flexibility<=int(capacity.size()) && wanted>0;++flexibility) {
                const auto eligible=[&](size_t p) {
                    return providers[p].colony==int(colony) && providers[p].accepts(unit)
                        && int(providers[p].flexibility())==flexibility;
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
inline std::vector<long long> allocateFeedingDemand(const std::vector<std::array<int,3>>& demand,
    const std::vector<FeedingProvider>& providers) {
    return allocateFeedingDemand<std::array<int,3>>(demand,providers);
}

}
