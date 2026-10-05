// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <vector>

namespace AIMaxima
{
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
