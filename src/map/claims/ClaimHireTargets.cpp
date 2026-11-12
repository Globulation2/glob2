// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComputeExecutor.h"
#include "ClaimHireTargets.h"
#include "Map.h"
#include "gradient/MapRouting.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "HiringPlanner.h"
#include <algorithm>
#include <map>

namespace ResourceClaims
{
const HireTarget* HireTargetIndex::find(Uint16 gid,int material) const
{
    const auto wanted=key(gid,material);
    const auto at=std::lower_bound(requests.begin(),requests.end(),wanted,[](const Request& r,Uint64 k){return r.key<k;});
    return at!=requests.end() && at->key==wanted?&results[std::size_t(at-requests.begin())]:nullptr;
}
}

namespace
{
// Same free-candidate filter as Building::hireMaterialUnit for a fetch job;
// the search itself also needs a mobile harvester that can carry material.
bool searchable(const Unit* u,const Team& team,int material)
{
    return u && !u->isDead && u->owner==&team && u->hasCapability(UnitRuntimeTraits::Transport) && u->performance[BUILD]
        && u->activity==Unit::ACT_RANDOM && u->medical==Unit::MED_FREE && u->performance[HARVEST]
        && !u->hasCarriedMaterial(material) && u->canCarryMaterial(material)
        && (u->performance[WALK] || u->performance[SWIM] || u->performance[FLY]);
}
}

void Map::prepareClaimHireTargets(int teamNumber)
{
    using namespace ResourceClaims;
    if(!activeClaimsEnabled() || !game)return;
    auto& index=claimHireTargets;
    index.reset(teamNumber,game->stepCounter);
    Team& team=*game->teams[teamNumber];
    // Every fetch suggestion the hiring phase can examine this tick: the
    // published shortlists of the buildings currently needing units.
    for(const auto& [priority,buildings]:team.buildingsNeedingUnits)
        for(const Building* b:buildings) {
            const auto* lists=game->hiringCandidates(*b);
            if(!lists)continue;
            for(const auto& list:lists->jobs) {
                if(list.key.job!=Hiring::Job::Fetch || !validMaterial(list.key.material))continue;
                for(const auto& suggestion:list.candidates) {
                    const Unit* u=game->resolveUnit(suggestion.unit);
                    if(!searchable(u,team,list.key.material))continue;
                    index.requests.push_back({HireTargetIndex::key(u->gid,list.key.material),u->gid,std::uint8_t(list.key.material)});
                }
            }
        }
    std::sort(index.requests.begin(),index.requests.end(),[](const auto& a,const auto& b){return a.key<b.key;});
    index.requests.erase(std::unique(index.requests.begin(),index.requests.end(),[](const auto& a,const auto& b){return a.key==b.key;}),index.requests.end());
    index.results.assign(index.requests.size(),{});
    if(index.requests.empty())return;
    // Owner thread: resolve every published field, destination plane and
    // terrain component table the searches read, so workers only borrow
    // immutable shared state. Requesting a missing field here is the same
    // owner request the worker's own first decision would make.
    struct Inputs { std::shared_ptr<const gradient_kernel::OwnedDestinations> destinations; std::shared_ptr<const TerrainComponentLabels> components; };
    std::map<std::pair<int,int>,Inputs> inputs;
    std::vector<const Inputs*> perRequest(index.requests.size(),nullptr);
    for(std::size_t i=0;i<index.requests.size();++i) {
        const Unit* u=team.myUnits[Unit::GIDtoID(index.requests[i].gid)];
        const int movement=accessClass(u->movementAccess());
        auto [at,inserted]=inputs.try_emplace({int(index.requests[i].material),movement});
        if(inserted) {
            if(const auto* field=routing().getMaterialGradientSlot(teamNumber,index.requests[i].material,u->movementAccess(),false,nullptr))
                at->second.destinations=routing().materialGradientDestinations(field);
            at->second.components=claimTerrainComponents(movement);
        }
        perRequest[i]=&at->second;
    }
    // Workers see the map and units only through const references.
    const Map& frozen=*this;
    const auto search=[&](std::size_t i) {
        const Unit* u=team.myUnits[Unit::GIDtoID(index.requests[i].gid)];
        const auto* input=perRequest[i];
        if(input->destinations)index.results[i]=u->searchHireClaimTarget(frozen,index.requests[i].material,*input->destinations,
            input->components.get());
    };
    // Each search is a pure function of the frozen state and its own key, so
    // the serial and parallel forms give identical results. A search costs
    // about a microsecond; below this many requests the dispatch would cost
    // the owner more than the searches themselves.
    if(index.requests.size()<HireTargetIndex::parallelSearches)for(std::size_t i=0;i<index.requests.size();++i)search(i);
    else compute.run(index.requests.size(),search,{ComputeProducer::Claims,game->stepCounter,game->stepCounter});
}
