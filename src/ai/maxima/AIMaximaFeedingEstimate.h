// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AIMaximaBuildings.h"
#include "AIMaximaFoodLedger.h"
#include "Race.h"
#include <array>
#include <climits>

namespace AIMaxima
{
// A nominal operating plan, not a sample of temporary staffing or empty stock.
// Each physical building receives this allowance once across all its recipes.
struct FeedingPlan
{
    int carriers=0;
    int oneWayTravelTicks=0;
    int handlingTicks=0;
    int ticksPerMeal=1;
    bool feeding=true;
    bool training=true;
    bool projectiles=true;
    unsigned productionMask=7;
};
struct FeedingEstimate
{
    static constexpr long long Scale=AIMaximaFoodLedger::RateScale;
    long long visitsPerTick=0; // fixed point Scale
    long long haulingWorkerTicks=0; // fixed point Scale workers
    long long projectileRate=0; // fixed point Scale shots/tick
    int supportedUnits=0;
    std::array<int,8> resources{}; // stock units, not carried packets
    std::array<int,8> resourcePackets{}; // natural-source/hauling denominations
    std::array<int,NB_UNIT_TYPE> productionRates{}; // per 1000 ticks
    std::array<int,AIMaximaBuildings::RoleCount> services{}; // per 1000 ticks
};

// Rebuilt only when immutable catalog/strategy/rules profiles are initialized.
// The bounded temporary array cannot allocate during a building observation.
inline FeedingEstimate estimateFeeding(const BuildingType& type,const FeedingPlan& plan)
{
    using namespace AIMaximaBuildings;
    struct Flow {
        unsigned roles=0;
        long long rate=0;
        std::array<int,8> cost{};
        int productionClass=-1;
        int outputMultiplier=1;
        long long seatTicks=0;
    };
    constexpr int MaxFlows=2+NB_UNIT_TYPE*NB_ABILITY+NB_UNIT_TYPE+1;
    std::array<Flow,MaxFlows> flows{};
    int flowCount=0,seatFlows=0;
    const auto& s=type.semantics;
    auto available=[&](const auto& spec){return spec.enabled && (spec.unitMask&s.admittedUnitMask) && type.maxUnitInside>0;};
    auto service=[&](unsigned roles,const auto& spec,bool holdExit) {
        if(!available(spec))return;
        auto& f=flows[flowCount++];f.roles=roles;
        f.seatTicks=serviceTicks(type,spec.duration)+
            static_cast<long long>(std::max(0,plan.oneWayTravelTicks))*(holdExit?2:1);
        std::copy_n(spec.cost.begin(),8,f.cost.begin());++seatFlows;
    };
    if(plan.feeding)service(roleBit(Feeding),s.feeding,s.feeding.holdAdmissionUntilExit);
    service(roleBit(Healing),s.healing,s.healing.holdAdmissionUntilExit);
    auto trainingRole=[](const auto& spec,int ability,int unit) {
        unsigned roles=0;
        if(unit==WORKER && spec.constructionLevel>0)roles|=roleBit(ConstructionTraining);
        if(spec.targetLevel>0) {
            if(ability==WALK)roles|=roleBit(WalkTraining);
            if(ability==SWIM)roles|=roleBit(SwimTraining);
            if(ability==ATTACK_SPEED || ability==ATTACK_STRENGTH)roles|=roleBit(CombatTraining);
        }
        return roles;
    };
    // Nominal trainees start at ability/construction level zero. A bundle is
    // formed for one recipient class at a time, using the same learnability
    // and improvement predicates as Unit::needsTraining. A worker-only course
    // can never inflate the cost or duration of a warrior's parallel visit.
    if(plan.training && type.maxUnitInside>0)for(int unit=0;unit<NB_UNIT_TYPE;++unit) {
        if(!(s.admittedUnitMask&(1u<<unit)))continue;
        Flow bundle;
        for(int ability=0;ability<NB_ABILITY;++ability) {
            const auto& spec=s.training[ability];
            if(!spec.enabled || !(spec.unitMask&(1u<<unit)) || !Race::unitTypes[unit][3].performance[ability] ||
                !(spec.targetLevel>0 || (unit==WORKER && spec.constructionLevel>0)))continue;
            const int speed=std::max(1,type.insideSpeed/std::max(1,spec.targetLevel));
            const long long ticks=(static_cast<long long>(spec.duration+1)*UNIT_DELTA_QUANTUM+speed-1)/speed;
            if(s.trainingInParallel) {
                bundle.roles|=trainingRole(spec,ability,unit);
                bundle.seatTicks=std::max(bundle.seatTicks,ticks);
                for(int resource=0;resource<8;++resource)bundle.cost[resource]+=spec.cost[resource];
            } else {
                auto& f=flows[flowCount++];f.roles=trainingRole(spec,ability,unit);
                f.seatTicks=ticks+std::max(0,plan.oneWayTravelTicks);
                std::copy_n(spec.cost.begin(),8,f.cost.begin());++seatFlows;
            }
        }
        if(s.trainingInParallel && bundle.seatTicks>0) {
            bundle.seatTicks+=std::max(0,plan.oneWayTravelTicks);
            flows[flowCount++]=bundle;++seatFlows;
        }
    }
    for(int i=0;i<flowCount;++i)
        flows[i].rate=FeedingEstimate::Scale*type.maxUnitInside/std::max(1LL,flows[i].seatTicks*seatFlows);

    // The nominal mix gives each allowed output one job, in equal proportions.
    // Recipes share one clock: the mean cost is divided by the mean complete
    // job duration, not the mean of independent inverse durations. Completion
    // itself takes a tick, exactly as Building::stepProduction does.
    long long productionCycle=0;
    for(int unit=0;unit<NB_UNIT_TYPE;++unit)if((plan.productionMask&(1u<<unit)) && s.production.recipes[unit].enabled)
        productionCycle+=static_cast<long long>(s.production.recipes[unit].duration)+1;
    for(int unit=0;unit<NB_UNIT_TYPE;++unit)if((plan.productionMask&(1u<<unit)) && s.production.recipes[unit].enabled) {
        auto& f=flows[flowCount++];f.roles=roleBit(Production);f.productionClass=unit;
        f.rate=FeedingEstimate::Scale/std::max(1LL,productionCycle);
        std::copy_n(s.production.recipes[unit].cost.begin(),8,f.cost.begin());
    }
    if(plan.projectiles && type.shootingRange>0 && type.shootRhythm>0) {
        auto& f=flows[flowCount++];f.roles=roleBit(ProjectileDefense);
        f.outputMultiplier=std::max(1,type.multiplierStoneToBullets);
        f.rate=FeedingEstimate::Scale*type.shootRhythm/(65536LL*f.outputMultiplier);
        if(s.ammunitionResource>=0 && s.ammunitionResource<8)f.cost[s.ammunitionResource]=s.ammunitionCost;
    }
    const long long trip=std::max(1LL,2LL*std::max(0,plan.oneWayTravelTicks)+std::max(0,plan.handlingTicks));
    auto product=[](long long a,long long b){return b>0 && a>(LLONG_MAX/4)/b ? LLONG_MAX/4 : a*b;};
    auto sum=[](long long a,long long b){return a>LLONG_MAX/4-b ? LLONG_MAX/4 : a+b;};
    auto hauling=[&](long long demand,int resource) {
        const int packet=std::max(1,type.multiplierResource[resource]);
        return sum(product(demand/packet,trip),product(demand%packet,trip)/packet);
    };
    long long requested=0;
    for(int i=0;i<flowCount;++i)for(int resource=0;resource<8;++resource)
        requested=sum(requested,hauling(flows[i].rate*flows[i].cost[resource],resource));
    const long long budget=FeedingEstimate::Scale*std::max(0,plan.carriers);
    // Scale is always <=1. The common product fits directly; the bounded
    // bitwise fallback also accepts large helper-test budgets without UB or
    // nonportable wider integer types (including browser builds).
    auto scaled=[](long long value,long long numerator,long long denominator) {
        if(!numerator)return 0LL;
        if(value<=LLONG_MAX/numerator)return value*numerator/denominator;
        long long quotient=0,remainder=0;
        for(int bit=62;bit>=0;--bit) {
            quotient*=2;
            if(remainder>=denominator-remainder){remainder-=denominator-remainder;++quotient;}
            else remainder*=2;
            if((value>>bit)&1) {
                if(remainder>=denominator-numerator){remainder-=denominator-numerator;++quotient;}
                else remainder+=numerator;
            }
        }
        return quotient;
    };
    FeedingEstimate result;
    for(int i=0;i<flowCount;++i) {
        auto f=flows[i];
        const bool usesCarrier=std::any_of(f.cost.begin(),f.cost.end(),[](int n){return n>0;});
        if(usesCarrier && requested>budget)f.rate=scaled(f.rate,budget,requested);
        const auto output=f.rate*f.outputMultiplier;
        if(f.roles&roleBit(Feeding))result.visitsPerTick+=output;
        if(f.roles&roleBit(ProjectileDefense))result.projectileRate+=output;
        if(f.productionClass>=0)result.productionRates[f.productionClass]=int(output*1000/FeedingEstimate::Scale);
        for(int role=0;role<RoleCount;++role)if(f.roles&roleBit(role))
            result.services[role]+=int(std::min<long long>(1000000,output*1000/FeedingEstimate::Scale));
        for(int resource=0;resource<8;++resource) {
            const auto demand=f.rate*f.cost[resource];
            result.resources[resource]=int(std::min<long long>(INT_MAX,static_cast<long long>(result.resources[resource])+demand));
            result.haulingWorkerTicks=sum(result.haulingWorkerTicks,hauling(demand,resource));
        }
    }
    for(int resource=0;resource<8;++resource)
        result.resourcePackets[resource]=result.resources[resource]/std::max(1,type.multiplierResource[resource]);
    result.supportedUnits=int(std::min<long long>(1000000,result.visitsPerTick*std::max(1,plan.ticksPerMeal)/FeedingEstimate::Scale));
    return result;
}
}
