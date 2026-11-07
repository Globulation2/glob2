// SPDX-License-Identifier: GPL-3.0-or-later
#include "Unit.h"
#include "Map.h"
#include "gradient/MapRouting.h"
#include "Team.h"
#include "Building.h"
#include "map/claims/ClaimPattern.h"
#include "map/claims/ClaimRecovery.h"
#include <limits>
#include <stdexcept>

namespace
{
bool canFulfillClaimWithoutTravel(const Map& map,const Unit& unit,Uint32 access,const Building* building)
{
    if(unit.performance[WALK] || unit.performance[SWIM] || unit.performance[FLY])return true;
    // Stationary transport is supported by hiring when pickup and consumer
    // both touch the unit. Farm stock may be remote; only its access matters.
    return access<map.cellCount() && building &&
        map.warpDistMax(unit.posX,unit.posY,int(access%map.getW()),int(access/map.getW()))<=1 &&
        map.doesPosTouchBuilding(unit.posX,unit.posY,building->gid).has_value();
}
bool canFulfillClaimWithoutTravel(const Unit& unit,Uint32 access)
{return canFulfillClaimWithoutTravel(*unit.owner->map,unit,access,unit.attachedBuilding);}

// The candidate filters every claim search applies, after deduplication and
// before any claim resolution: the terrain prefilter (an access whose stance
// lies in no terrain component the worker is in is certainly unreachable) and
// the distance cap relative to the nearest destination (Policy).
struct CandidateFilter
{
    const ResourceClaims::TerrainComponentLabels* components=nullptr;
    int posX=0,posY=0;
    Uint64 distanceLimit=0; // Zero: no recorded nearest destination, no cap.
    CandidateFilter(const Map& map,int x,int y,bool nearestValid,unsigned nx,unsigned ny,const ResourceClaims::TerrainComponentLabels* labels)
        : components(labels),posX(x),posY(y)
    {
        using namespace ResourceClaims::Policy;
        if(nearestValid)distanceLimit=Uint64(MaxClaimDistanceFactorPermille)*Uint64(map.warpDistMax(x,y,int(nx),int(ny)))
            +Uint64(1000)*ClaimDistanceSlackTiles;
    }
    bool rejects(const Map& map,Uint32 access,unsigned x,unsigned y) const
    {
        if(components && !ResourceClaims::terrainComponentMayReach(*components,posX,posY,access))return true;
        return distanceLimit && Uint64(map.warpDistMax(posX,posY,int(x),int(y)))*1000>distanceLimit;
    }
};
// Per-thread visited stamps over the map, bumped instead of cleared, so one
// selection never checks an access twice however many entries lead to it.
struct VisitedCells
{
    std::vector<Uint32> stamps;
    Uint32 epoch=0;
    void begin(std::size_t cells) {
        if(stamps.size()!=cells){stamps.assign(cells,0);epoch=0;}
        if(++epoch==0){std::fill(stamps.begin(),stamps.end(),0);epoch=1;}
    }
    bool visit(Uint32 cell) { if(stamps[cell]==epoch)return false; stamps[cell]=epoch; return true; }
};
// The access an entry of the learned pattern names, or nullopt off the plane.
std::optional<std::pair<unsigned,unsigned>> patternAccess(const Map& map,const gradient_kernel::OwnedDestinations& destinations,
    const ResourceClaims::Pattern::View& probes,unsigned i,int posX,int posY,bool nearestValid,unsigned nx,unsigned ny)
{
    const auto tag=probes.tag[i];
    if((tag&1) && !nearestValid)return std::nullopt;
    const int ox=(tag&1)?int(nx):posX,oy=(tag&1)?int(ny):posY;
    const auto probe=Uint32(map.coordToIndex(ox+probes.x[i],oy+probes.y[i]));
    const unsigned x=(tag&2)?probe%map.getW():destinations.x[probe];
    const unsigned y=(tag&2)?probe/map.getW():destinations.y[probe];
    if(x>=unsigned(map.getW()) || y>=unsigned(map.getH()))return std::nullopt;
    return std::pair{x,y};
}
}

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
        || c.material!=destinationPurpose || !canCarryMaterial(c.material) || !performance[HARVEST])return false;
    if(!canFulfillClaimWithoutTravel(*this,c.access))return false;
    if(displacement!=DIS_GOING_TO_RESOURCE && displacement!=DIS_HARVESTING)return false;
    if(c.source>=map.cellCount() || c.access>=map.cellCount())return false;
    if(map.getResource(c.source).type!=c.resource || !map.materialAmountAtSlot(c.source,c.material)
        || !map.materialAmountAtSlot(c.access,c.material))return false;
    if(!map.routing().materialDestinationValidSlot(owner->teamNumber,c.material,movementAccess(),
            int(c.access%map.getW()),int(c.access/map.getW()),false,nullptr))return false;
    const auto& yield=map.resourceRegistry().yields(static_cast<ResourceId>(c.resource))[c.material];
    return yield.consumption==ResourceConsumption::Infinite || map.materialAmountAtSlot(c.source,c.material)>c.reserve;
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
// Resolution of a candidate access for material on behalf of building, after
// the caller's unit state checks: shared by selection and the hire-time search.
// It reads the map only; pick(access,owner) chooses a pooled field's source and
// returns it with the flooded component, so the hire search can pass a
// scratch-based flood that never mutates the map.
template<class Pick>
std::optional<ResolvedClaim> resolveClaimFor(const Map& map,const Unit& unit,Uint32 access,int material,const Building* building,
    FarmClaimDecisionMemo* memo,Pick&& pick)
{
    const auto* owner=unit.owner;
    if(!map.activeClaimsEnabled() || access>=map.cellCount() || !validMaterial(material)
        || !canFulfillClaimWithoutTravel(map,unit,access,building)
        || !map.materialAmountAtSlot(access,material)
        || !map.routing().materialDestinationValidSlot(owner->teamNumber,material,unit.movementAccess(),
            int(access%map.getW()),int(access/map.getW()),false,nullptr))return std::nullopt;
    const auto identity=ResourceClaims::unitOwner(unit.gid,unit.scriptIdentity);
    auto source=access;
    const auto terms=map.claimTerms(map.getResource(access).type,material,access,owner->me);
    if(terms.pooled) {
        if(memo && memo->contains(access)) {
            source=memo->sources[access];
            if(source==UINT32_MAX)return std::nullopt;
        } else {
            const auto& [selected,component]=pick(access,identity);
            // All early eligibility guards above precede the flood. Its queue is
            // cleared at entry and contains this entire nonempty component even
            // when every surplus unit is claimed. Copy before any other picker.
            if(memo)memo->remember(component,selected);
            if(!selected)return std::nullopt;
            source=Uint32(*selected);
        }
    }
    // A pooled source is a connected farm tile of the same field, so its terms
    // differ from the access tile's only by its own yield's seed floor.
    const auto sourceTerms=map.claimTerms(map.getResource(source).type,material,access,owner->me);
    const auto amount=map.materialAmountAtSlot(source,material);
    const ResourceClaims::Claim claim{source,access,Uint8(material),map.getResource(source).type,terms.exclusive,sourceTerms.reserve};
    return ResolvedClaim{claim,amount>sourceTerms.reserve?amount-sourceTerms.reserve:0u,sourceTerms.infinite};
}
std::optional<ResolvedClaim> resolveClaim(Unit& unit,Uint32 access,FarmClaimDecisionMemo* memo=nullptr)
{
    if(unit.isDead || unit.hp<UNIT_HP_DEATH_THRESHOLD || unit.medical!=Unit::MED_FREE || !unit.attachedBuilding || unit.activity!=Unit::ACT_FILLING
        || !unit.canCarryMaterial(unit.destinationPurpose) || !unit.performance[HARVEST])return std::nullopt;
    auto& map=*unit.owner->map;
    const int x=unit.posX,y=unit.posY,material=unit.destinationPurpose;const auto team=unit.owner->me;
    return resolveClaimFor(map,unit,access,material,unit.attachedBuilding,memo,[&](Uint32 entry,ResourceClaims::Owner owner) {
        const auto selected=map.pickFarmHarvestTileSlot(x,y,material,team,entry,owner);
        return std::pair<std::optional<size_t>,const std::vector<size_t>&>{selected,map.farmFloodQueue};
    });
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
    map.cancelClaimRoute(identity);
    unit.activeMaterialClaim=claim;++unit.materialClaimTask;
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
    const auto destinations=map.routing().materialGradientDestinations(field);
    if(!destinations)return false;
    // One owner-thread scratch plane, reused across units. Repeated probes of
    // the same destination must not repeat claim checks or pooled farm floods.
    thread_local VisitedCells visited;
    thread_local FarmClaimDecisionMemo farmMemo;
    visited.begin(map.cellCount());
    farmMemo.begin(map.cellCount());
    // A worker recovering from failed routes resumes the pattern after the
    // entry that led to the failure, and skips accesses recently found
    // unreachable unless they are already within pickup range.
    const auto recovery=map.beginClaimRecoverySelection(*this);
    const bool recovering=recovery.recovering && !activeMaterialClaim;
    const auto origin=Uint32(map.coordToIndex(posX,posY));
    const auto nx=destinations->x[origin],ny=destinations->y[origin];
    const bool nearestValid=nx<map.getW() && ny<map.getH();
    const auto components=map.claimTerrainComponents(accessClass(movementAccess()));
    const CandidateFilter filter(map,posX,posY,nearestValid,nx,ny,components.get());
    const auto nearest=nearestValid?Uint32(map.coordToIndex(nx,ny)):UINT32_MAX;
    const auto probes=ResourceClaims::Pattern::view(ResourceClaims::Pattern::frame(map.getW(),map.getH(),origin,nearest));
    const unsigned count=unsigned(probes.x.size());
    // First available wins: the learned order already ranks candidates.
    for(unsigned i=recovering?recovery.firstProbe:0;i<count;++i) {
        const auto cell=patternAccess(map,*destinations,probes,i,posX,posY,nearestValid,nx,ny);
        if(!cell)continue;
        const auto [x,y]=*cell;
        const auto access=Uint32(map.coordToIndex(x,y));
        if(!visited.visit(access) || filter.rejects(map,access,x,y))continue;
        if((!recovery.mayRoute || recovery.rejects(access)) && map.warpDistMax(posX,posY,x,y)>1)continue;
        const auto resolved=resolveClaim(*this,access,&farmMemo);
        if(!resolved || !map.materialClaimAvailable(ResourceClaims::unitOwner(gid,scriptIdentity),
            resolved->claim,resolved->capacity,resolved->infinite))continue;
        const auto oldTask=materialClaimTask;
        if(!commitResolvedClaim(*this,*resolved))return false;
        // A refresh that re-finds the held claim keeps the recovery cursor.
        if(oldTask!=materialClaimTask)map.finishClaimRecoverySelection(*this,i,count);
        return true;
    }
    if(recovery.mayRoute && !activeMaterialClaim)map.finishClaimRecoverySelection(*this,count,count);
    return false;
}

ResourceClaims::HireTarget Unit::searchHireClaimTarget(const Map& map,int material,const gradient_kernel::OwnedDestinations& destinations,
    const ResourceClaims::TerrainComponentLabels* components) const
{
    // Mirrors selectActiveMaterialClaim for this worker as if it were hired
    // now: no held claim and no recovery history, the same pattern frame,
    // filters, resolution and ledger availability, but no commit.
    using ResourceClaims::HireTarget;
    HireTarget result;
    if(!(performance[WALK] || performance[SWIM] || performance[FLY]))return result;
    result.state=HireTarget::State::None;
    thread_local VisitedCells visited;
    thread_local FarmClaimDecisionMemo farmMemo;
    thread_local Map::FarmFloodScratch scratch;
    visited.begin(map.cellCount());
    farmMemo.begin(map.cellCount());
    const auto origin=Uint32(map.coordToIndex(posX,posY));
    const auto nx=destinations.x[origin],ny=destinations.y[origin];
    const bool nearestValid=nx<map.getW() && ny<map.getH();
    const CandidateFilter filter(map,posX,posY,nearestValid,nx,ny,components);
    const auto nearest=nearestValid?Uint32(map.coordToIndex(nx,ny)):UINT32_MAX;
    const auto probes=ResourceClaims::Pattern::view(ResourceClaims::Pattern::frame(map.getW(),map.getH(),origin,nearest));
    const auto identity=ResourceClaims::unitOwner(gid,scriptIdentity);
    for(unsigned i=0;i<unsigned(probes.x.size());++i) {
        const auto cell=patternAccess(map,destinations,probes,i,posX,posY,nearestValid,nx,ny);
        if(!cell)continue;
        const auto [x,y]=*cell;
        const auto access=Uint32(map.coordToIndex(x,y));
        if(!visited.visit(access) || filter.rejects(map,access,x,y))continue;
        const auto resolved=resolveClaimFor(map,*this,access,material,nullptr,&farmMemo,[&](Uint32 entry,ResourceClaims::Owner owner) {
            const auto selected=map.pickFarmHarvestTileSlotIn(scratch,posX,posY,material,this->owner->me,entry,owner);
            return std::pair<std::optional<size_t>,const std::vector<size_t>&>{selected,scratch.queue};
        });
        if(resolved && map.materialClaimAvailable(identity,resolved->claim,resolved->capacity,resolved->infinite)) {
            result.state=HireTarget::State::Found;result.claim=resolved->claim;
            result.capacity=resolved->capacity;result.infinite=resolved->infinite;
            return result;
        }
    }
    return result;
}

bool Unit::materialClaimHireAllowed(const Building& building,int material)
{
    using ResourceClaims::HireTarget;
    auto& map=*owner->map;
    if(!map.activeClaimsEnabled())return true;
    if(!validMaterial(material) || isDead || hp<UNIT_HP_DEATH_THRESHOLD || medical!=MED_FREE || !performance[HARVEST])return true;
    // Claim resolution needs cargo room for the material: a worker carrying
    // another material can never claim it, and would only be released again.
    if(!canCarryMaterial(material))return false;
    // As handleMovementGoingToResource: supplier fetches never consult claims.
    if(projectsSupplierFetch(building,material))return true;
    const auto& index=map.claimHireTargetIndex();
    const HireTarget* target=index.current(owner->teamNumber,owner->ctx.tick())?index.find(gid,material):nullptr;
    HireTarget local;
    if(!target) {
        // Not in this phase's pass (a building that joined the call lists
        // during the phase, or a direct caller): search on the owner.
        if(const auto* field=map.routing().getMaterialGradientSlot(owner->teamNumber,material,movementAccess(),false,nullptr))
            if(const auto destinations=map.routing().materialGradientDestinations(field)) {
                const auto components=map.claimTerrainComponents(accessClass(movementAccess()));
                local=searchHireClaimTarget(map,material,*destinations,components.get());
            }
        target=&local;
    }
    if(target->state==HireTarget::State::Unchecked)return true;
    if(target->state==HireTarget::State::None)return false;
    // Cheap owner recheck of the precomputed target: job swaps earlier in this
    // phase are the only ledger changes since the pass. Two hires may share
    // one last free source; the claim step then decides.
    return map.materialClaimAvailable(ResourceClaims::unitOwner(gid,scriptIdentity),target->claim,target->capacity,target->infinite);
}

bool Unit::projectsSupplierFetch(const Building& building,int material)
{
    auto& map=*owner->map;
    const auto* field=map.routing().getMaterialGradientSlot(owner->teamNumber,material,movementAccess(),building.fetchesFromMarkets(),&building);
    Sint32 x=0,y=0;
    return field && map.routing().projectMaterialDestination(field,owner->teamNumber,material,movementAccess(),posX,posY,&x,&y,
        building.fetchesFromMarkets(),&building) && !map.materialAmountAtSlot(map.coordToIndex(x,y),material);
}

bool Unit::prepareActiveMaterialPickup(int x,int y)
{
    auto& map=*owner->map;
    if(!map.activeClaimsEnabled())return true;
    releaseInvalidActiveMaterialClaim();
    const auto access=Uint32(map.coordToIndex(x,y));
    if(activeMaterialClaim) {
        if(activeMaterialClaim->access==access)return true;
        const auto held=activeMaterialClaim->access;
        // Finish an already reachable reservation first. Otherwise exchange the
        // distant claim only if adjacent stock can be reserved transactionally;
        // other workers' reservations stay protected by the ledger.
        if(map.warpDistMax(posX,posY,int(held%map.getW()),int(held/map.getW()))<=1 ||
            map.warpDistMax(posX,posY,int(access%map.getW()),int(access/map.getW()))>1)return false;
    }
    return acquireMaterialClaimAt(access);
}

bool Unit::takeActiveMaterialClaim()
{
    if(!activeMaterialClaimValid()){releaseActiveMaterialClaim();return false;}
    const auto claim=*activeMaterialClaim;
    if(owner->map->coordToIndex(posX+dx,posY+dy)!=claim.access){releaseActiveMaterialClaim();return false;}
    // The reservation is no longer outstanding when the stock mutation checks
    // other owners' remaining capacity. The copied source never changes.
    releaseActiveMaterialClaim();
    const bool taken=owner->map->takeClaimedMaterial(claim,owner->me);
    if(taken)owner->map->claimRecoveryHarvested(*this);
    return taken;
}

bool Unit::usesMaterialClaimPickup() const
{
    return owner->map->activeClaimsEnabled() && activeMaterialClaim.has_value();
}

bool Unit::materialClaimDecision()
{
    auto& map=*owner->map;
    // A held claim keeps the worker in claim mode until it is released or
    // harvested; an invalid claim is released first, exactly as claim
    // movement would, and the worker is tested afresh.
    releaseInvalidActiveMaterialClaim();
    if(activeMaterialClaim)return true;
    // The contention test reads the target projected by this worker's last
    // ordinary decision; without one there is nothing to be crowded about.
    return validTarget() && map.claimContentionOthers(*this,destinationPurpose,targetX,targetY)+1
        >=ResourceClaims::Policy::ContentionMinWorkers;
}

void Unit::handleActiveMaterialMovement()
{
    auto& map=*owner->map;
    auto& cadence=materialClaimPolicyState;
    // A crowded worker with nothing to claim gives up its job, as an idle
    // worker would, rather than waiting on stock that other workers hold:
    // waiting idles labour, while the freed worker can be hired for work it
    // can actually do (hiring checks that, see materialClaimHireAllowed).
    const auto releaseJob=[&] {
        setNeedToRecheckMedical(true);stopAttachedForBuilding(false);movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
    };
    if(cadence.decisionOrdinal==UINT64_MAX)throw std::overflow_error("material claim decision ordinal exhausted");
    ++cadence.decisionOrdinal;
    releaseInvalidActiveMaterialClaim();
    const bool refresh=activeMaterialClaim
        && cadence.decisionOrdinal-cadence.lastGoalAttempt>=ResourceClaims::Policy::DestinationRefreshDecisions;
    if(!activeMaterialClaim || refresh) {
        std::optional<GradientFieldKey> key;
        const auto* field=map.routing().getMaterialGradientSlot(owner->teamNumber,destinationPurpose,movementAccess(),false,nullptr,&key);
        // Claims consume the material field even when selection finds only
        // fully reserved stock. Keep adaptive refresh demand alive while these
        // workers wait for growth or a different available destination.
        if(key)map.routing().gradientMovementDemand(*key);
        cadence.lastGoalAttempt=cadence.decisionOrdinal;
        // A failed refresh leaves the old valid reservation and route untouched.
        if(field)selectActiveMaterialClaim(field);
    }
    dx=dy=0;direction=UNIT_DIRECTION_NONE;movement=MOV_GOING_DX_DY;
    if(!activeMaterialClaim) {
        setValidTarget(false);
        // Route admission cooldown is not evidence that stock is absent.
        if(map.beginClaimRecoverySelection(*this).mayRoute) {
            map.recordClaimFailure(*this,ResourceClaims::ClaimFailureKind::NoResource);
            releaseJob();
        }
        return;
    }
    int stepX=0,stepY=0;
    const auto result=map.nextClaimRouteStep(*this,&stepX,&stepY);
    // Pursuit while routed (moving or traffic-blocked) is gradient-relevant;
    // a computing route already took its own gradient step or is unrelated.
    if(result!=ResourceClaims::StepResult::Failed && result!=ResourceClaims::StepResult::Arrived)
        map.claimPursuitGradientSignal(*this,!map.claimRouteComputing(*this),result==ResourceClaims::StepResult::Ready);
    if(result==ResourceClaims::StepResult::Ready) {
        dx=Sint8(stepX);dy=Sint8(stepY);directionFromDxDy();
    } else if(result==ResourceClaims::StepResult::Failed) {
        const auto failures=map.recordClaimFailure(*this,ResourceClaims::ClaimFailureKind::PathFailed);
        releaseActiveMaterialClaim();
        if(failures)releaseJob();
    }
}
