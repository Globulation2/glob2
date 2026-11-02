// SPDX-License-Identifier: GPL-3.0-or-later
#include "Unit.h"
#include "Map.h"
#include "Game.h"
#include "Building.h"
#include "map/claims/ResourceClaimPilotPatterns.h"
#include <limits>
#include <stdexcept>

void Unit::releaseActiveMaterialClaim()
{
    if(!activeMaterialClaim)return;
    if(materialClaimTask==std::numeric_limits<Uint64>::max())throw std::overflow_error("material claim task exhausted");
    const auto identity=ResourceClaims::unitOwner(gid,scriptIdentity);
    owner->map->cancelClaimRoute(identity);
    owner->map->releaseMaterialClaim(identity);
    activeMaterialClaim.reset();++materialClaimTask;
}

bool Unit::activeMaterialClaimValid()
{
    if(!activeMaterialClaim)return false;
    const auto& c=*activeMaterialClaim;auto& map=*owner->map;
    if(isDead || hp<UNIT_HP_DEATH_THRESHOLD || medical!=MED_FREE || !attachedBuilding || activity!=ACT_FILLING
        || c.material!=destinationPurpose || !canCarryMaterial(c.material) || !performance[HARVEST]
        || (!performance[WALK] && !performance[SWIM] && !performance[FLY])
        || (displacement!=DIS_GOING_TO_RESOURCE && displacement!=DIS_HARVESTING)
        || c.source>=map.cellCount() || c.access>=map.cellCount()
        || map.getResource(c.source).type!=c.resource || !map.materialAmountAtSlot(c.source,c.material)
        || !map.materialAmountAtSlot(c.access,c.material)
        || map.isForbidden(int(c.access%map.getW()),int(c.access/map.getW()),owner->me))return false;
    const auto& yield=map.resourceRegistry().yields(static_cast<ResourceId>(c.resource))[c.material];
    if(yield.consumption!=ResourceConsumption::Infinite && map.materialAmountAtSlot(c.source,c.material)<=c.reserve)return false;
    return true;
}

bool Unit::acquireMaterialClaimAt(Uint32 access)
{
    auto& map=*owner->map;
    if(isDead || hp<UNIT_HP_DEATH_THRESHOLD || medical!=MED_FREE || !attachedBuilding || activity!=ACT_FILLING
        || !canCarryMaterial(destinationPurpose) || !performance[HARVEST])return false;
    if(!map.activeClaimsEnabled() || access>=map.cellCount() || !validMaterial(destinationPurpose)
        || !map.materialAmountAtSlot(access,destinationPurpose)
        || !map.materialDestinationValidSlot(owner->teamNumber,destinationPurpose,movementClass(),
            int(access%map.getW()),int(access/map.getW()),false,nullptr))return false;
    const auto identity=ResourceClaims::unitOwner(gid,scriptIdentity);
    auto source=access;
    const auto& initial=map.resourceRegistry().yields(static_cast<ResourceId>(map.getResource(access).type))[destinationPurpose];
    const bool exclusive=initial.consumption==ResourceConsumption::All || initial.destroysDeposit;
    const bool pooled=!exclusive && map.farmAreasEnabled() && map.isFarmArea(int(access%map.getW()),int(access/map.getW()),owner->me)
        && map.isFarmableResourceByIndex(map.getResource(access).type);
    if(pooled) {
        const auto selected=map.pickFarmHarvestTileSlot(posX,posY,destinationPurpose,owner->me,access,identity);
        if(!selected)return false;
        source=Uint32(*selected);
    }
    const auto& yield=map.resourceRegistry().yields(static_cast<ResourceId>(map.getResource(source).type))[destinationPurpose];
    const auto amount=map.materialAmountAtSlot(source,destinationPurpose);
    const unsigned reserve=pooled && yield.consumption!=ResourceConsumption::Infinite && !owner->game->gameHeader.isResourceGrowthDisabled()?yield.seedReserve:0;
    const ResourceClaims::Claim claim{source,access,Uint8(destinationPurpose),map.getResource(source).type,exclusive,Uint16(reserve)};
    if(activeMaterialClaim && *activeMaterialClaim==claim)return true;
    if(materialClaimTask>=std::numeric_limits<Uint64>::max()-1)throw std::overflow_error("material claim task exhausted");
    if(!map.acquireMaterialClaim(identity,claim,amount>reserve?amount-reserve:0,yield.consumption==ResourceConsumption::Infinite))return false;
    map.cancelClaimRoute(identity);
    activeMaterialClaim=claim;++materialClaimTask;
    targetX=int(access%map.getW());targetY=int(access/map.getW());setValidTarget(true);
    return true;
}

bool Unit::selectActiveMaterialClaim(const Uint16* field)
{
    auto& map=*owner->map;
    const auto destinations=map.materialGradientDestinations(field);
    if(!destinations)return false;
    // Temporary named baseline. Policy tournament winners are selected later.
    static const auto pattern=[] {
        for(const auto& p:ResourceClaims::Experiment::pilotPatterns())
            if(std::string_view(p.name)=="spiral:k16:unit:s1:every:forward:d0:seed1")return p.viewForDirection(0);
        throw std::logic_error("prototype claim pattern missing");
    }();
    // One owner-thread scratch plane, reused across units. Repeated probes of
    // the same destination must not repeat claim checks or pooled farm floods.
    thread_local std::vector<Uint32> visited;
    thread_local Uint32 epoch=0;
    if(visited.size()!=map.cellCount()){visited.assign(map.cellCount(),0);epoch=0;}
    if(++epoch==0){std::fill(visited.begin(),visited.end(),0);epoch=1;}
    for(std::size_t i=0;i<pattern.x.size();++i) {
        const auto probe=map.coordToIndex(posX+pattern.x[i],posY+pattern.y[i]);
        const auto x=destinations->x[probe],y=destinations->y[probe];
        if(x>=map.getW() || y>=map.getH())continue;
        const auto access=Uint32(map.coordToIndex(x,y));
        if(visited[access]==epoch)continue;
        visited[access]=epoch;
        if(acquireMaterialClaimAt(access))return true;
    }
    return false;
}

bool Unit::prepareActiveMaterialPickup(int x,int y)
{
    auto& map=*owner->map;
    if(!map.activeClaimsEnabled())return true;
    if(activeMaterialClaim && !activeMaterialClaimValid())releaseActiveMaterialClaim();
    const auto access=Uint32(map.coordToIndex(x,y));
    if(!activeMaterialClaim && !acquireMaterialClaimAt(access))return false;
    // Sticky claim: passing another source cannot steal its reserved material.
    return activeMaterialClaim->access==access;
}

bool Unit::takeActiveMaterialClaim()
{
    if(!activeMaterialClaimValid()){releaseActiveMaterialClaim();return false;}
    const auto claim=*activeMaterialClaim;
    if(owner->map->coordToIndex(posX+dx,posY+dy)!=claim.access){releaseActiveMaterialClaim();return false;}
    // The reservation is no longer outstanding when the stock mutation checks
    // other owners' remaining capacity. The copied source never changes.
    releaseActiveMaterialClaim();
    return owner->map->takeClaimedMaterial(claim,owner->me);
}

void Unit::handleActiveMaterialMovement()
{
    auto& map=*owner->map;
    if(activeMaterialClaim && !activeMaterialClaimValid())releaseActiveMaterialClaim();
    if(!activeMaterialClaim) {
        const auto* field=map.getMaterialGradientSlot(owner->teamNumber,destinationPurpose,movementClass(),false,nullptr);
        if(field) {map.observeMaterialClaimDecision(*this,field);selectActiveMaterialClaim(field);}
    }
    dx=dy=0;direction=UNIT_DIRECTION_NONE;movement=MOV_GOING_DX_DY;
    if(!activeMaterialClaim){setValidTarget(false);return;} // Stationary normal-cadence retry.
    int stepX=0,stepY=0;
    const auto result=map.nextClaimRouteStep(*this,&stepX,&stepY);
    if(result==ResourceClaims::StepResult::Ready) {
        dx=Sint8(stepX);dy=Sint8(stepY);directionFromDxDy();
    } else if(result==ResourceClaims::StepResult::Failed)releaseActiveMaterialClaim();
}
