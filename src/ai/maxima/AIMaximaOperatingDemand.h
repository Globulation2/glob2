// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Material.h"
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
    int costs[3][MaterialCount]{};
    int packetSize[MaterialCount]{1,1,1,1,1,1,1,1,1,1,1,1};
};
inline std::array<int,MaterialCount> productionPacketCeiling(const ProductionRecipeModel& model,const int ratios[3])
{
    long long cycle=0;
    std::array<long long,MaterialCount> weightedCost{};
    for(int unit=0;unit<3;++unit)if(model.ticks[unit]>0) {
        const int weight=std::clamp(ratios[unit],0,32767);
        cycle+=static_cast<long long>(weight)*model.ticks[unit];
        for(int resource=0;resource<MaterialCount;++resource)
            weightedCost[resource]+=static_cast<long long>(weight)*model.costs[unit][resource];
    }
    std::array<int,MaterialCount> result{};
    // Catalog bounds (3 classes,32767 ratio,1e6 cost/duration/packet) keep both
    // products below1e17. Divide after costing: a slow expensive job must not
    // disappear because its jobs/tick rounds below fixed-point precision.
    if(cycle)for(int resource=0;resource<MaterialCount;++resource)
        result[resource]=int(std::min<long long>(INT_MAX,
            weightedCost[resource]*AIMaximaFoodLedger::RateScale /
            (cycle*std::max(1,model.packetSize[resource]))));

    return result;
}

struct OperatingClaim
{
    std::array<int,MaterialCount> total{},production{};
};
// Necessities retain their recurring claim even during temporary shortages.
// Discretionary production shares the *requested* carrier budget left after
// those services, rather than reserving every recipe's mechanical ceiling.
inline OperatingClaim operatingClaim(const std::array<int,MaterialCount>& independent,
    const std::array<int,MaterialCount>& production,int requestedCarriers,const std::array<int,MaterialCount>& trips)
{
    constexpr long long scale=AIMaximaFoodLedger::RateScale;
    auto addWork=[](long long total,long long addition){return std::min(LLONG_MAX/4,total+addition);};
    long long otherWork=0,productionWork=0;
    for(int resource=0;resource<MaterialCount;++resource) {
        const long long trip=std::max(1,trips[resource]);
        otherWork=addWork(otherWork,static_cast<long long>(std::max(0,independent[resource]))*trip);
        productionWork=addWork(productionWork,static_cast<long long>(std::max(0,production[resource]))*trip);
    }
    // Authoring currently bounds staffing at1024. The live order policy is
    // stricter; this helper also accepts standalone catalog test fixtures.
    const long long remaining=std::max(0LL,scale*std::clamp(requestedCarriers,0,1024)-otherWork);
    OperatingClaim result;
    for(int resource=0;resource<MaterialCount;++resource) {
        const long long mechanical=std::max(0,production[resource]);
        const long long operating=requestedCarriers<0 || productionWork<=remaining
            ? mechanical : mechanical*remaining/std::max(1LL,productionWork);
        result.production[resource]=int(operating);
        result.total[resource]=int(std::min<long long>(INT_MAX,std::max(0,independent[resource])+operating));
    }
    return result;
}
// The curve prices a combined wheat flow: independent services cannot reuse
// the same cheap source packets that production has already spent.
template<class WheatWork>
inline OperatingClaim operatingClaimWithWheatWork(const std::array<int,MaterialCount>& independent,
    const std::array<int,MaterialCount>& production,int carriers,const std::array<int,MaterialCount>& trips,WheatWork wheatWork)
{
    constexpr int wheat=1;
    constexpr long long scale=AIMaximaFoodLedger::RateScale,limit=LLONG_MAX/4;
    constexpr long long fractionScale=INT_MAX;
    if(carriers<0)return operatingClaim(independent,production,carriers,trips);
    long long other=0,producing=0;
    for(int r=0;r<MaterialCount;++r)if(r!=wheat) {
        other=std::min(limit,other+static_cast<long long>(std::max(0,independent[r]))*std::max(1,trips[r]));
        producing=std::min(limit,producing+static_cast<long long>(std::max(0,production[r]))*std::max(1,trips[r]));
    }
    const auto scaledUp=[](long long value,long long fraction) {
        return (value/fractionScale)*fraction+((value%fractionScale)*fraction+fractionScale-1)/fractionScale;
    };
    const long long budget=scale*std::clamp(carriers,0,1024);
    const auto work=[&](int fraction) {
        const long long q=std::max(0,independent[wheat])+static_cast<long long>(std::max(0,production[wheat]))*fraction/fractionScale;
        return std::min(limit,std::min(limit,other+scaledUp(producing,fraction))+wheatWork(q));
    };
    int low=0,high=int(fractionScale);
    // At most31 iterations; prefix queries never rescan cells. INT_MAX
    // precision can represent one rate unit even for saturated recipes.
    while(low<high) {
        const int middle=low+int((static_cast<long long>(high)-low+1)/2);
        if(work(middle)<=budget)low=middle;else high=middle-1;
    }
    OperatingClaim result;
    for(int r=0;r<MaterialCount;++r) {
        result.production[r]=int(static_cast<long long>(std::max(0,production[r]))*low/fractionScale);
        result.total[r]=int(std::min<long long>(INT_MAX,static_cast<long long>(std::max(0,independent[r]))+result.production[r]));
    }
    return result;
}

}
