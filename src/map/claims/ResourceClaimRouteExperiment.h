// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ClaimRouteSearch.h"
#include "ResourceClaimPatternEvaluation.h"
#include "ResourceClaimExposure.h"
#include "MapInternal.h"
#include <chrono>
#include <map>
#include <set>

namespace ResourceClaims::Experiment
{
// Temporary bounded frozen-world experiment; no cadence, traffic prediction or
// pending-first-step conclusions can be drawn from these static routes.
struct RouteMeasurement
{
    const char* status = "unreachable";
    Uint64 cost = Oracle::Unreachable, ns = 0;
    Uint32 expansions = 0, continuationSteps = 0;
    ExposureProxy::Path exposure;
};
class StaticRoutes
{
    MapState::View view;
    const Oracle::Query& query;
    const Oracle::Result& oracle;
    std::span<const Uint16> values;
    RouteSearch search;
    const ExposureProxy* exposure=nullptr;
    std::map<Uint32, RouteMeasurement> directCache, prefixCache;
    std::map<std::pair<Uint32,Uint32>,RouteMeasurement> continuationCache;
    std::vector<Uint32> successors;
    std::vector<unsigned> successorCosts;
    std::vector<bool> successorRead;
    unsigned minimumStep=std::numeric_limits<unsigned>::max();
    unsigned separation(Uint32 a,Uint32 b) const
    {
        unsigned dx=unsigned(std::abs(int(a%view.width)-int(b%view.width)));
        unsigned dy=unsigned(std::abs(int(a/view.width)-int(b/view.width)));
        return std::max(std::min(dx,unsigned(view.width)-dx),std::min(dy,unsigned(view.height)-dy));
    }
    template<class Goal,class Heuristic>
    RouteMeasurement route(Goal goal,Heuristic heuristic)
    {
        RouteMeasurement measurement;
        const auto start=std::chrono::steady_clock::now();
        const auto result=search.find(field::Grid(view.width,view.height),query.origin,UINT32_MAX,
            [&](Uint32 i){return routePassable(view,i,query.route);},
            [&](Uint32 i,bool diagonal){return routeEntryCost(view,i,query.route,diagonal);},
            goal,heuristic,ExpansionLimit);
        measurement.ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        measurement.expansions=result.expansions;
        if(result.status==RouteStatus::ExpansionLimit)measurement.status="expansion_limit";
        else if(result.status==RouteStatus::Found){
            measurement.status="reached";measurement.cost=result.cost;
            if(exposure) {
                measurement.exposure.endpoint=query.origin;auto previous=query.origin;
                for(const auto cell:result.cells) {
                    const bool diagonal=cell%view.width!=previous%view.width && cell/view.width!=previous/view.width;
                    exposure->enter(measurement.exposure,cell,routeEntryCost(view,cell,query.route,diagonal));previous=cell;
                }
            }
        }
        else if(result.status==RouteStatus::CostLimit)measurement.status="cost_limit";
        return measurement;
    }
public:
    static constexpr unsigned OutcomeLimit=32, ExpansionLimit=4096, ContinuationLimit=4096;
    StaticRoutes(MapState::View view,const Oracle::Query& query,const Oracle::Result& oracle,std::span<const Uint16> values,const ExposureProxy* exposure=nullptr)
        :view(view),query(query),oracle(oracle),values(values),exposure(exposure)
    {
        successors.resize(values.size(),Oracle::NoTile);
        successorCosts.resize(values.size()); successorRead.resize(values.size());
        for(std::size_t rule=0;rule<view.rules->size();++rule) {
            const auto& steps=(*view.rules)[rule].ground[query.route.movement];
            minimumStep=std::min({minimumStep,unsigned(steps.cardinal),unsigned(steps.diagonal)});
        }
    }
    RouteMeasurement direct(Uint32 source)
    {
        if(auto found=directCache.find(source);found!=directCache.end())return found->second;
        const auto row=std::find_if(oracle.candidates.begin(),oracle.candidates.end(),[&](const auto& c){return c.source==source;});
        if(row==oracle.candidates.end())return {};
        // A pooled source is reachable through ANY legal access in its farm.
        std::set<Uint32> goals;
        for(const auto& c:oracle.candidates)if(c.accessEligible &&
            (c.source==source || (row->component!=Oracle::NoTile && c.component==row->component))) {
            const int x=int(c.source%view.width),y=int(c.source/view.width);
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                const auto i=Uint32(view.index(x+dx,y+dy));
                if(i==query.origin || routePassable(view,i,query.route))goals.insert(i);
            }
        }
        // Zero heuristic for arbitrary farm goal sets; otherwise octile-safe
        // minimum-entry Chebyshev distance to the source's surrounding stances.
        const auto result=route([&](Uint32 i){return goals.contains(i);},[&](Uint32 i){
            return row->component!=Oracle::NoTile?0u:(separation(i,source)>0?separation(i,source)-1:0)*minimumStep;});
        directCache[source]=result; return result;
    }
    RouteMeasurement via(Uint32 access,Uint32 probe)
    {
        RouteMeasurement prefix;
        if(auto found=prefixCache.find(probe);found!=prefixCache.end())prefix=found->second;
        else {
            prefix=route([&](Uint32 i){return i==probe;},[&](Uint32 i){return separation(i,probe)*minimumStep;});
            prefixCache[probe]=prefix;
        }
        if(prefix.cost==Oracle::Unreachable)return prefix;
        const auto key=std::pair{access,probe};
        RouteMeasurement tail;
        if(auto found=continuationCache.find(key);found!=continuationCache.end())tail=found->second;
        else {
            const auto start=std::chrono::steady_clock::now();
            Uint32 cell=probe; tail.cost=0; tail.status="contradiction";
            if(exposure)tail.exposure.endpoint=probe;
            while(true) {
                if(separation(cell,access)<=1){tail.status="reached";break;}
                if(tail.continuationSteps>=ContinuationLimit){tail.status="continuation_limit";break;}
                if(values[cell]<=GRADIENT_UNREACHABLE || values[cell]==GRADIENT_AT_GOAL)break;
                if(!successorRead[cell]) {
                    successorRead[cell]=true;
                    int best=-1;
                    for(int d=0;d<8;++d) {
                        const int dx=tabClose[d][0],dy=tabClose[d][1];
                        const auto next=Uint32(view.index(int(cell%view.width)+dx,int(cell/view.width)+dy));
                        if(values[next]<=values[cell])continue;
                        const auto cost=routeEntryCost(view,next,query.route,dx&&dy);
                        const int score=int(values[next])-int(cost);
                        if(score>best){best=score;successors[cell]=next;successorCosts[cell]=cost;}
                    }
                }
                const auto selected=successors[cell];const auto step=successorCosts[cell];
                if(selected==Oracle::NoTile || !routePassable(view,selected,query.route))break;
                cell=selected;tail.cost+=step;++tail.continuationSteps;
                if(exposure)exposure->enter(tail.exposure,cell,step);
            }
            if(std::string_view(tail.status)!="reached")tail.cost=Oracle::Unreachable;
            tail.ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            continuationCache[key]=tail;
        }
        tail.ns+=prefix.ns;tail.expansions=prefix.expansions;
        if(exposure){auto combined=prefix.exposure;combined.append(tail.exposure);tail.exposure=combined;}
        if(tail.cost!=Oracle::Unreachable)tail.cost+=prefix.cost;
        return tail;
    }
};
}
