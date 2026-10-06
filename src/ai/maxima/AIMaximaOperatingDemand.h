// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <climits>
#include "AIMaximaFoodLedger.h"

namespace AIMaxima
{
// Immutable recipe projection for cold planning; ticks includes completion.
struct ProductionRecipeModel
{
    int ticks[3]{};
    int costs[3][8]{};
    int packetSize[8]{1,1,1,1,1,1,1,1};
};
inline std::array<int,8> productionPacketCeiling(const ProductionRecipeModel& model,const int ratios[3])
{
    long long cycle=0;
    std::array<long long,8> weightedCost{};
    for(int unit=0;unit<3;++unit)if(model.ticks[unit]>0) {
        const int weight=std::clamp(ratios[unit],0,32767);
        cycle+=static_cast<long long>(weight)*model.ticks[unit];
        for(int resource=0;resource<8;++resource)
            weightedCost[resource]+=static_cast<long long>(weight)*model.costs[unit][resource];
    }
    std::array<int,8> result{};
    // Catalog bounds (3 classes,32767 ratio,1e6 cost/duration/packet) keep both
    // products below1e17. Divide after costing: a slow expensive job must not
    // disappear because its jobs/tick rounds below fixed-point precision.
    if(cycle)for(int resource=0;resource<8;++resource)
        result[resource]=int(std::min<long long>(INT_MAX,
            weightedCost[resource]*AIMaximaFoodLedger::RateScale /
            (cycle*std::max(1,model.packetSize[resource]))));

    return result;
}

struct OperatingClaim
{
    std::array<int,8> total{},production{};
};
// Necessities retain their recurring claim even during temporary shortages.
// Discretionary production shares the *requested* carrier budget left after
// those services, rather than reserving every recipe's mechanical ceiling.
inline OperatingClaim operatingClaim(const std::array<int,8>& independent,
    const std::array<int,8>& production,int requestedCarriers,const std::array<int,8>& trips)
{
    constexpr long long scale=AIMaximaFoodLedger::RateScale;
    auto addWork=[](long long total,long long addition){return std::min(LLONG_MAX/4,total+addition);};
    long long otherWork=0,productionWork=0;
    for(int resource=0;resource<8;++resource) {
        const long long trip=std::max(1,trips[resource]);
        otherWork=addWork(otherWork,static_cast<long long>(std::max(0,independent[resource]))*trip);
        productionWork=addWork(productionWork,static_cast<long long>(std::max(0,production[resource]))*trip);
    }
    // Authoring currently bounds staffing at1024. The live order policy is
    // stricter; this helper also accepts standalone catalog test fixtures.
    const long long remaining=std::max(0LL,scale*std::clamp(requestedCarriers,0,1024)-otherWork);
    OperatingClaim result;
    for(int resource=0;resource<8;++resource) {
        const long long mechanical=std::max(0,production[resource]);
        const long long operating=requestedCarriers<0 || productionWork<=remaining
            ? mechanical : mechanical*remaining/std::max(1LL,productionWork);
        result.production[resource]=int(operating);
        result.total[resource]=int(std::min<long long>(INT_MAX,std::max(0,independent[resource])+operating));
    }
    return result;
}
}
