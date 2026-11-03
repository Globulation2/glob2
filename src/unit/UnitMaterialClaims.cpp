// SPDX-License-Identifier: GPL-3.0-or-later
#include "Unit.h"
#include "Map.h"
#include "Game.h"
#include "Building.h"
#include "map/claims/ResourceClaimPilotPatterns.h"
#include "map/claims/ResourceClaimProbe.h"
#include "map/claims/ClaimRecovery.h"
#include <limits>
#include <climits>
#include <stdexcept>

namespace
{
bool canFulfillClaimWithoutTravel(Unit& unit,Uint32 access)
{
    if(unit.performance[WALK] || unit.performance[SWIM] || unit.performance[FLY])return true;
    auto& map=*unit.owner->map;
    // Stationary transport is supported by hiring when pickup and consumer
    // both touch the unit. Farm stock may be remote; only its access matters.
    return access<map.cellCount() && unit.attachedBuilding &&
        map.warpDistMax(unit.posX,unit.posY,int(access%map.getW()),int(access/map.getW()))<=1 &&
        map.doesUnitTouchBuilding(&unit,unit.attachedBuilding->gid).has_value();
}
}

void Unit::releaseActiveMaterialClaim()
{
    if(!activeMaterialClaim)return;
    if(materialClaimTask==std::numeric_limits<Uint64>::max())throw std::overflow_error("material claim task exhausted");
    const auto identity=ResourceClaims::unitOwner(gid,scriptIdentity);
    if(const auto* probe=owner->map->resourceClaimProbe())probe->liveEvent(*this,ResourceClaims::Probe::LiveEvent::Released);
    owner->map->cancelClaimRoute(identity);
    owner->map->releaseMaterialClaim(identity);
    activeMaterialClaim.reset();++materialClaimTask;
    if(owner->map->materialClaimPolicy()==ResourceClaims::ActiveClaimPolicy::Experimental)materialClaimPolicyState.probe=UINT32_MAX;
}

bool Unit::activeMaterialClaimValid()
{
    if(!activeMaterialClaim)return false;
    const auto& c=*activeMaterialClaim;auto& map=*owner->map;
    if(isDead || hp<UNIT_HP_DEATH_THRESHOLD || medical!=MED_FREE || !attachedBuilding || activity!=ACT_FILLING
        || c.material!=destinationPurpose || !canCarryMaterial(c.material) || !performance[HARVEST]
        || !canFulfillClaimWithoutTravel(*this,c.access)
        || (displacement!=DIS_GOING_TO_RESOURCE && displacement!=DIS_HARVESTING)
        || c.source>=map.cellCount() || c.access>=map.cellCount()
        || map.getResource(c.source).type!=c.resource || !map.materialAmountAtSlot(c.source,c.material)
        || !map.materialAmountAtSlot(c.access,c.material)
        || !map.materialDestinationValidSlot(owner->teamNumber,c.material,movementClass(),
            int(c.access%map.getW()),int(c.access/map.getW()),false,nullptr))return false;
    const auto& yield=map.resourceRegistry().yields(static_cast<ResourceId>(c.resource))[c.material];
    if(yield.consumption!=ResourceConsumption::Infinite && map.materialAmountAtSlot(c.source,c.material)<=c.reserve)return false;
    return true;
}

namespace
{
struct ResolvedClaim { ResourceClaims::Claim claim; Uint32 capacity; bool infinite; };
// This memo is valid for one uninterrupted selection only: owner, material,
// position, stock and ledger cannot change until the selected claim commits.
// Cache source choice (including exhaustion), never per-access eligibility.
struct FarmClaimDecisionMemo
{
    std::vector<Uint32> stamps,sources;
    Uint32 epoch=0;
    void begin(std::size_t cells) {
        if(stamps.size()!=cells){stamps.assign(cells,0);sources.resize(cells);epoch=0;}
        if(++epoch==0){std::fill(stamps.begin(),stamps.end(),0);epoch=1;}
    }
    bool contains(Uint32 access) const {return stamps[access]==epoch;}
    void remember(const std::vector<std::size_t>& component,std::optional<std::size_t> source) {
        for(const auto cell:component){stamps[cell]=epoch;sources[cell]=source?Uint32(*source):UINT32_MAX;}
    }
};
std::optional<ResolvedClaim> resolveClaim(Unit& unit,Uint32 access,FarmClaimDecisionMemo* memo=nullptr)
{
    auto* owner=unit.owner;auto& map=*owner->map;
    const auto destinationPurpose=unit.destinationPurpose;
    const auto gid=unit.gid;const auto scriptIdentity=unit.scriptIdentity;
    const int posX=unit.posX,posY=unit.posY;
    if(unit.isDead || unit.hp<UNIT_HP_DEATH_THRESHOLD || unit.medical!=Unit::MED_FREE || !unit.attachedBuilding || unit.activity!=Unit::ACT_FILLING
        || !unit.canCarryMaterial(destinationPurpose) || !unit.performance[HARVEST])return std::nullopt;
    if(!map.activeClaimsEnabled() || access>=map.cellCount() || !validMaterial(destinationPurpose)
        || !canFulfillClaimWithoutTravel(unit,access)
        || !map.materialAmountAtSlot(access,destinationPurpose)
        || !map.materialDestinationValidSlot(owner->teamNumber,destinationPurpose,unit.movementClass(),
            int(access%map.getW()),int(access/map.getW()),false,nullptr))return std::nullopt;
    const auto identity=ResourceClaims::unitOwner(gid,scriptIdentity);
    auto source=access;
    const auto& initial=map.resourceRegistry().yields(static_cast<ResourceId>(map.getResource(access).type))[destinationPurpose];
    const bool exclusive=initial.consumption==ResourceConsumption::All || initial.destroysDeposit;
    const bool pooled=!exclusive && map.farmAreasEnabled() && map.isFarmArea(int(access%map.getW()),int(access/map.getW()),owner->me)
        && map.isFarmableResourceByIndex(map.getResource(access).type);
    if(pooled) {
        if(memo && memo->contains(access)) {
            source=memo->sources[access];
            if(source==UINT32_MAX)return std::nullopt;
        } else {
            const auto selected=map.pickFarmHarvestTileSlot(posX,posY,destinationPurpose,owner->me,access,identity);
            // All early eligibility guards above precede the flood. Its queue is
            // cleared at entry and contains this entire nonempty component even
            // when every surplus unit is claimed. Copy before any other picker.
            if(memo)memo->remember(map.farmFloodQueue,selected);
            if(!selected)return std::nullopt;
            source=Uint32(*selected);
        }
    }
    const auto& yield=map.resourceRegistry().yields(static_cast<ResourceId>(map.getResource(source).type))[destinationPurpose];
    const auto amount=map.materialAmountAtSlot(source,destinationPurpose);
    const unsigned reserve=pooled && yield.consumption!=ResourceConsumption::Infinite && !owner->game->gameHeader.isResourceGrowthDisabled()?yield.seedReserve:0;
    const ResourceClaims::Claim claim{source,access,Uint8(destinationPurpose),map.getResource(source).type,exclusive,Uint16(reserve)};
    return ResolvedClaim{claim,amount>reserve?amount-reserve:0,yield.consumption==ResourceConsumption::Infinite};
}
// The owner cannot mutate stock, eligibility or ledger between selection and
// commit. Keep the resolved winner, while the ledger still checks availability
// transactionally. Direct callers resolve immediately before this same commit.
bool commitResolvedClaim(Unit& unit,const ResolvedClaim& resolved)
{
    auto& map=*unit.owner->map;const auto& claim=resolved.claim;
    const auto identity=ResourceClaims::unitOwner(unit.gid,unit.scriptIdentity);
    if(unit.activeMaterialClaim && *unit.activeMaterialClaim==claim)return true;
    if(unit.materialClaimTask>=std::numeric_limits<Uint64>::max()-1)throw std::overflow_error("material claim task exhausted");
    if(!map.acquireMaterialClaim(identity,claim,resolved.capacity,resolved.infinite))return false;
    if(unit.activeMaterialClaim)if(const auto* probe=map.resourceClaimProbe())probe->liveEvent(unit,ResourceClaims::Probe::LiveEvent::Retargeted);
    map.cancelClaimRoute(identity);
    unit.activeMaterialClaim=claim;++unit.materialClaimTask;
    if(const auto* probe=map.resourceClaimProbe())probe->liveEvent(unit,ResourceClaims::Probe::LiveEvent::Acquired);
    if(map.materialClaimPolicy()==ResourceClaims::ActiveClaimPolicy::Experimental)unit.materialClaimPolicyState.probe=Uint32(map.coordToIndex(unit.posX,unit.posY));
    unit.targetX=int(claim.access%map.getW());unit.targetY=int(claim.access/map.getW());unit.setValidTarget(true);
    return true;
}
}
bool Unit::acquireMaterialClaimAt(Uint32 access)
{
    const auto resolved=resolveClaim(*this,access);
    return resolved && commitResolvedClaim(*this,*resolved);
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
    if(map.materialClaimPolicy()==ResourceClaims::ActiveClaimPolicy::Experimental) {
        thread_local FarmClaimDecisionMemo farmMemo;
        farmMemo.begin(map.cellCount());
        const auto& config=map.materialClaimPolicyConfig();
        const auto recovery=config.usesRecovery()?map.beginClaimRecoverySelection(*this):ResourceClaims::ClaimRecoverySelection{};
        const bool recovering=recovery.recovering && !activeMaterialClaim;
        const bool firstAvailable=recovering || config.selection==ResourceClaims::ClaimPolicyConfig::Selection::FirstAvailable;
        const auto origin=Uint32(map.coordToIndex(posX,posY));
        const auto nx=destinations->x[origin],ny=destinations->y[origin];
        const bool nearestValid=nx<map.getW() && ny<map.getH();
        const auto delta=[](int a,int b,int size){return ((b-a+size/2)&(size-1))-size/2;};
        const int nearDx=nearestValid?delta(posX,nx,map.getW()):0,nearDy=nearestValid?delta(posY,ny,map.getH()):0;
        unsigned octant=0;
        if(nearDx || nearDy) {
            if(std::abs(nearDx)>=2*std::abs(nearDy))octant=nearDx>=0?0:4;
            else if(std::abs(nearDy)>=2*std::abs(nearDx))octant=nearDy>=0?2:6;
            else octant=nearDx>=0?(nearDy>=0?1:7):(nearDy>=0?3:5);
        }
        const auto found=std::find_if(ResourceClaims::Experiment::pilotPatterns().begin(),ResourceClaims::Experiment::pilotPatterns().end(),
            [&](const auto& candidate){return config.pattern==candidate.name;});
        if(found==ResourceClaims::Experiment::pilotPatterns().end())throw std::logic_error("configured material claim pattern missing");
        const auto probes=found->viewForDirection(octant);
        Uint32 bestAccess=UINT32_MAX,bestProbe=UINT32_MAX;unsigned bestScore=UINT_MAX,bestOrdinal=0;
        std::optional<ResolvedClaim> bestResolved;
        for(std::size_t i=recovering?recovery.firstProbe:0;i<probes.x.size();++i) {
            const auto tag=probes.origin[i];if(tag && !nearestValid)continue;
            const int ox=tag==0?posX:tag==1?nx:posX+nearDx/2;
            const int oy=tag==0?posY:tag==1?ny:posY+nearDy/2;
            const auto probe=Uint32(map.coordToIndex(ox+probes.x[i],oy+probes.y[i]));
            const auto x=destinations->x[probe],y=destinations->y[probe];
            if(x>=map.getW() || y>=map.getH())continue;
            const auto access=Uint32(map.coordToIndex(x,y));
            if(visited[access]==epoch)continue;
            visited[access]=epoch;
            // A soft failed-route exclusion cannot forbid a pickup already
            // within reach. Deduplicate first: this eligibility is constant for
            // every probe leading to the same access during one selection.
            if(config.usesRecovery() && (!recovery.mayRoute || recovery.rejects(access)) &&
                map.warpDistMax(posX,posY,x,y)>1)continue;
            const unsigned dx=std::max(0,std::abs(delta(posX,x,map.getW()))-1),dy=std::max(0,std::abs(delta(posY,y,map.getH()))-1);
            const unsigned score=10*std::max(dx,dy)+4*std::min(dx,dy);
            // Only a strictly better geometric candidate can replace the held
            // best. Reject dominated accesses before any connected-farm flood;
            // first-available exits below, so its evaluation order is unchanged.
            if(bestAccess!=UINT32_MAX && score>=bestScore)continue;
            const auto resolved=resolveClaim(*this,access,&farmMemo);if(!resolved)continue;
            if(!map.materialClaimLedger().available(ResourceClaims::unitOwner(gid,scriptIdentity),resolved->claim,resolved->capacity,resolved->infinite))continue;
            bestAccess=access;bestProbe=probe;bestScore=score;bestResolved=*resolved;bestOrdinal=unsigned(i);
            if(firstAvailable)break;
        }
        if(bestAccess==UINT32_MAX) {
            if(config.usesRecovery() && recovery.mayRoute && !activeMaterialClaim)
                map.finishClaimRecoverySelection(*this,unsigned(probes.x.size()),unsigned(probes.x.size()));
            return false;
        }
        const auto oldTask=materialClaimTask;
        if(!commitResolvedClaim(*this,*bestResolved))return false;
        if(oldTask!=materialClaimTask || materialClaimPolicyState.probe==UINT32_MAX) {
            materialClaimPolicyState.probe=bestProbe;
            if(config.usesRecovery())map.finishClaimRecoverySelection(*this,bestOrdinal,unsigned(probes.x.size()));
        }
        return true;
    }
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
    const auto* probe=owner->map->resourceClaimProbe();
    if(probe)probe->liveEvent(*this,ResourceClaims::Probe::LiveEvent::HarvestBegin);
    releaseActiveMaterialClaim();
    const bool taken=owner->map->takeClaimedMaterial(claim,owner->me);
    if(taken && owner->map->materialClaimPolicy()==ResourceClaims::ActiveClaimPolicy::Experimental &&
        owner->map->materialClaimPolicyConfig().usesRecovery())owner->map->claimRecoveryHarvested(*this);
    if(probe)probe->liveEvent(*this,taken?ResourceClaims::Probe::LiveEvent::Harvested:ResourceClaims::Probe::LiveEvent::HarvestFailed);
    return taken;
}

void Unit::handleActiveMaterialMovement()
{
    auto& map=*owner->map;
    const bool experimental=map.materialClaimPolicy()==ResourceClaims::ActiveClaimPolicy::Experimental;
    const auto& config=map.materialClaimPolicyConfig();
    auto& cadence=materialClaimPolicyState;
    const bool recoveryEnabled=experimental && config.usesRecovery();
    const auto fallback=[&](unsigned failures) {
        cadence.fallbackCount=failures;
        using F=ResourceClaims::ClaimPolicyConfig::Fallback;
        const bool release=config.fallback==F::ReleaseJob || (config.fallback==F::BoundedWaitRelease && failures>=config.fallbackAfterDecisions);
        if(const auto* probe=map.resourceClaimProbe())probe->liveEvent(*this,release?ResourceClaims::Probe::LiveEvent::FallbackRelease:
            config.fallback==F::Roam?ResourceClaims::Probe::LiveEvent::FallbackRoam:ResourceClaims::Probe::LiveEvent::FallbackWait);
        if(release) {setNeedToRecheckMedical(true);stopAttachedForBuilding(false);movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;}
        else if(config.fallback==F::Roam)movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
    };
    if(experimental) {
        if(cadence.decisionOrdinal==UINT64_MAX)throw std::overflow_error("material claim decision ordinal exhausted");
        ++cadence.decisionOrdinal;
    }
    if(activeMaterialClaim && !activeMaterialClaimValid())releaseActiveMaterialClaim();
    const bool refresh=experimental && activeMaterialClaim && config.destinationRefreshJ
        && cadence.decisionOrdinal-cadence.lastGoalAttempt>=config.destinationRefreshJ;
    if(!activeMaterialClaim || refresh) {
        std::optional<GradientFieldKey> key;
        const auto* field=map.getMaterialGradientSlot(owner->teamNumber,destinationPurpose,movementClass(),false,nullptr,&key);
        // Claims consume the material field even when selection finds only
        // fully reserved stock. Keep adaptive refresh demand alive while these
        // workers wait for growth or a different available destination.
        if(key)map.gradientMovementDemand(*key);
        if(experimental)cadence.lastGoalAttempt=cadence.decisionOrdinal;
        if(field) {
            map.observeMaterialClaimDecision(*this,field);const auto previousTask=materialClaimTask;
            const bool selected=selectActiveMaterialClaim(field);
            if(refresh)if(const auto* probe=map.resourceClaimProbe())probe->liveEvent(*this,!selected?ResourceClaims::Probe::LiveEvent::GoalRefreshFailed:
                previousTask==materialClaimTask?ResourceClaims::Probe::LiveEvent::GoalRefreshSame:ResourceClaims::Probe::LiveEvent::GoalRefreshChanged);
        }
        // Failed refresh leaves the old valid reservation and route untouched.
    }
    dx=dy=0;direction=UNIT_DIRECTION_NONE;movement=MOV_GOING_DX_DY;
    if(!activeMaterialClaim) {
        setValidTarget(false);
        if(experimental) {
            if(recoveryEnabled) {
                // Route admission cooldown is not evidence that stock is absent.
                if(map.beginClaimRecoverySelection(*this).mayRoute)
                    fallback(map.recordClaimFailure(*this,ResourceClaims::ClaimFailureKind::NoResource));
            } else fallback(cadence.fallbackCount==UINT32_MAX?UINT32_MAX:cadence.fallbackCount+1);
        }
        return;
    }
    if(experimental && !recoveryEnabled)cadence.fallbackCount=0;
    int stepX=0,stepY=0;
    auto result=ResourceClaims::StepResult::Failed;
    if(experimental && config.usePrimaryGradient)result=map.nextClaimGradientStep(*this,&stepX,&stepY);
    if(result==ResourceClaims::StepResult::Failed)result=map.nextClaimRouteStep(*this,&stepX,&stepY);
    if(result==ResourceClaims::StepResult::Ready) {
        dx=Sint8(stepX);dy=Sint8(stepY);directionFromDxDy();
    } else if(result==ResourceClaims::StepResult::Failed) {
        const auto failures=recoveryEnabled?map.recordClaimFailure(*this,ResourceClaims::ClaimFailureKind::PathFailed):0;
        releaseActiveMaterialClaim();
        if(recoveryEnabled && failures)fallback(failures);
    }
}
