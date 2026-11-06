#include "map/claims/ClaimMixedPatterns.h"
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

void Unit::releaseActiveMaterialClaim(ResourceClaims::ReleaseReason reason)
{
    if(!activeMaterialClaim)return;
    if(materialClaimTask==std::numeric_limits<Uint64>::max())throw std::overflow_error("material claim task exhausted");
    const auto identity=ResourceClaims::unitOwner(gid,scriptIdentity);
    if(const auto* probe=owner->map->resourceClaimProbe())probe->liveRelease(*this,reason);
    owner->map->cancelClaimRoute(identity);
    owner->map->releaseMaterialClaim(identity);
    activeMaterialClaim.reset();++materialClaimTask;
    if(owner->map->materialClaimPolicy()==ResourceClaims::ActiveClaimPolicy::Experimental)materialClaimPolicyState.probe=UINT32_MAX;
}

std::optional<ResourceClaims::ReleaseReason> Unit::activeMaterialClaimInvalidity()
{
    using R=ResourceClaims::ReleaseReason;
    if(!activeMaterialClaim)return R::Other;
    const auto& c=*activeMaterialClaim;auto& map=*owner->map;
    // Same checks and short-circuit order as the former single predicate;
    // only the reported category depends on which check failed first.
    if(isDead || hp<UNIT_HP_DEATH_THRESHOLD || medical!=MED_FREE || !attachedBuilding || activity!=ACT_FILLING
        || c.material!=destinationPurpose || !canCarryMaterial(c.material) || !performance[HARVEST])return R::UnitStateChanged;
    if(!canFulfillClaimWithoutTravel(*this,c.access))return R::AccessInvalid;
    if(displacement!=DIS_GOING_TO_RESOURCE && displacement!=DIS_HARVESTING)return R::UnitStateChanged;
    if(c.source>=map.cellCount() || c.access>=map.cellCount())return R::AccessInvalid;
    if(map.getResource(c.source).type!=c.resource || !map.materialAmountAtSlot(c.source,c.material)
        || !map.materialAmountAtSlot(c.access,c.material))return R::ResourceDepleted;
    if(!map.materialDestinationValidSlot(owner->teamNumber,c.material,movementAccess(),
            int(c.access%map.getW()),int(c.access/map.getW()),false,nullptr))return R::AccessInvalid;
    const auto& yield=map.resourceRegistry().yields(static_cast<ResourceId>(c.resource))[c.material];
    if(yield.consumption!=ResourceConsumption::Infinite && map.materialAmountAtSlot(c.source,c.material)<=c.reserve)return R::ResourceDepleted;
    return std::nullopt;
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
        || !map.materialDestinationValidSlot(owner->teamNumber,destinationPurpose,unit.movementAccess(),
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
        // Optional filters, applied after deduplication and before any claim
        // resolution or commit. Both are inert (no reads, no state) by default.
        using Filter=ResourceClaims::Probe::SelectionFilter;
        const auto* filterObserver=map.resourceClaimProbe();
        const auto components=config.terrainComponentPrefilter?map.claimTerrainComponents(accessClass(movementAccess())):nullptr;
        const unsigned margin=config.refreshSwitchMarginPermille;
        const bool marginActive=margin && activeMaterialClaim;
        const Uint32 heldAccess=marginActive?activeMaterialClaim->access:UINT32_MAX;
        const Uint64 heldDistance=marginActive?Uint64(map.warpDistMax(posX,posY,int(heldAccess%map.getW()),int(heldAccess/map.getW()))):0;
        bool marginRejected=false;
        // Distance cap relative to the nearest destination (integer only).
        const unsigned distanceFactor=nearestValid?config.maxClaimDistanceFactorPermille:0;
        const Uint64 distanceLimit=distanceFactor?Uint64(distanceFactor)*Uint64(map.warpDistMax(posX,posY,int(nx),int(ny)))
            +Uint64(1000)*ResourceClaims::ClaimPolicyConfig::ClaimDistanceSlackTiles:0;
        const auto filtered=[&](Uint32 access,unsigned x,unsigned y) {
            if(components && !ResourceClaims::terrainComponentMayReach(*components,posX,posY,access)) {
                if(filterObserver)filterObserver->selectionFilter(Filter::TerrainComponent);
                return true;
            }
            if(distanceFactor && Uint64(map.warpDistMax(posX,posY,int(x),int(y)))*1000>distanceLimit) {
                if(filterObserver)filterObserver->policyExtension(ResourceClaims::Probe::PolicyExtension::DistanceFactorRejection);
                return true;
            }
            // Refresh hysteresis: the held access stays eligible; another must
            // be strictly closer by the configured fraction (integer only).
            if(marginActive && access!=heldAccess &&
                !(Uint64(map.warpDistMax(posX,posY,int(x),int(y)))*1000<Uint64(1000-margin)*heldDistance)) {
                marginRejected=true;
                if(filterObserver)filterObserver->selectionFilter(Filter::RefreshMargin);
                return true;
            }
            return false;
        };
        // Nothing sufficiently closer: keep the held claim and its route.
        const auto keptByMargin=[&] {
            if(!marginRejected || !activeMaterialClaim)return false;
            if(filterObserver)filterObserver->selectionFilter(Filter::RefreshMarginKept);
            return true;
        };
        if(config.sourceVersion==5) {
            const auto nearest=nearestValid?Uint32(map.coordToIndex(nx,ny)):UINT32_MAX;
            const unsigned frame=config.patternFrame==ResourceClaims::ClaimPolicyConfig::PatternFrame::NearestD4?
                ResourceClaims::Mixed::frame(map.getW(),map.getH(),origin,nearest):0;
            const auto probes=ResourceClaims::Mixed::view(config.pattern,frame);
            const unsigned count=unsigned(probes.x.size());
            const unsigned begin=recovering?recovery.firstProbe:0;
            for(unsigned i=begin;i<count;++i) {
                const auto tag=probes.tag[i];
                if((tag&1) && !nearestValid)continue;
                const int ox=(tag&1)?nx:posX,oy=(tag&1)?ny:posY;
                const auto probe=Uint32(map.coordToIndex(ox+probes.x[i],oy+probes.y[i]));
                const unsigned x=(tag&2)?probe%map.getW():destinations->x[probe];
                const unsigned y=(tag&2)?probe/map.getW():destinations->y[probe];
                if(x>=unsigned(map.getW()) || y>=unsigned(map.getH()))continue;
                const auto access=Uint32(map.coordToIndex(x,y));
                if(visited[access]==epoch)continue;
                visited[access]=epoch;
                if(filtered(access,x,y))continue;
                if((!recovery.mayRoute || recovery.rejects(access)) && map.warpDistMax(posX,posY,x,y)>1)continue;
                const auto resolved=resolveClaim(*this,access,&farmMemo);
                if(!resolved || !map.materialClaimAvailable(ResourceClaims::unitOwner(gid,scriptIdentity),
                    resolved->claim,resolved->capacity,resolved->infinite))continue;
                const auto oldTask=materialClaimTask;
                if(!commitResolvedClaim(*this,*resolved))return false;
                if(oldTask!=materialClaimTask || materialClaimPolicyState.probe==UINT32_MAX) {
                    materialClaimPolicyState.probe=probe;
                    map.finishClaimRecoverySelection(*this,i,count);
                }
                return true;
            }
            if(keptByMargin())return true;
            if(recovery.mayRoute && !activeMaterialClaim)map.finishClaimRecoverySelection(*this,count,count);
            return false;
        }
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
        const unsigned candidateCount=config.recoveryCandidateCount(probes.x.size());
        Uint32 bestAccess=UINT32_MAX,bestProbe=UINT32_MAX;unsigned bestScore=UINT_MAX,bestOrdinal=0;
        std::optional<ResolvedClaim> bestResolved;
        // Dispatch once. Raw=false instantiation contains no raw-probe branch
        // or counter work in the per-pattern loop and retains XY-only ordering.
        const auto search=[&]<bool Raw,bool Record>() {
            Uint64 coordinates=0,xyChecks=0,rawAttempts=0,rawChecks=0,rawImprovements=0;
            const auto consider=[&]<bool Physical>(Uint32 access,unsigned x,unsigned y,Uint32 probe,unsigned i) {
                if(visited[access]==epoch)return false;
                visited[access]=epoch;
                if constexpr(Record){if constexpr(Physical)++rawChecks;else ++xyChecks;}
                if(filtered(access,x,y))return false;
                // A soft failed-route exclusion cannot forbid a pickup already
                // within reach. Deduplicate first: this eligibility is constant for
                // every probe leading to the same access during one selection.
                if(config.usesRecovery() && (!recovery.mayRoute || recovery.rejects(access)) &&
                    map.warpDistMax(posX,posY,x,y)>1)return false;
                const unsigned dx=std::max(0,std::abs(delta(posX,x,map.getW()))-1),dy=std::max(0,std::abs(delta(posY,y,map.getH()))-1);
                const unsigned score=10*std::max(dx,dy)+4*std::min(dx,dy);
                // Only a strictly better geometric candidate can replace the held
                // best. Reject dominated accesses before any connected-farm flood;
                // first-available exits below, so its evaluation order is unchanged.
                if(bestAccess!=UINT32_MAX && score>=bestScore)return false;
                const auto resolved=resolveClaim(*this,access,&farmMemo);if(!resolved)return false;
                if(!map.materialClaimAvailable(ResourceClaims::unitOwner(gid,scriptIdentity),resolved->claim,resolved->capacity,resolved->infinite))return false;
                if constexpr(Record && Physical)++rawImprovements;
                bestAccess=access;bestProbe=probe;bestScore=score;bestResolved=*resolved;bestOrdinal=Raw?2*i+unsigned(Physical):i;
                return true;
            };
            const std::size_t firstCandidate=recovering?recovery.firstProbe:0;
            for(std::size_t i=Raw?firstCandidate/2:firstCandidate;i<probes.x.size();++i) {
                const auto tag=probes.origin[i];if(tag && !nearestValid)continue;
                const int ox=tag==0?posX:tag==1?nx:posX+nearDx/2;
                const int oy=tag==0?posY:tag==1?ny:posY+nearDy/2;
                const int probeX=ox+probes.x[i],probeY=oy+probes.y[i];
                const auto probe=Uint32(map.coordToIndex(probeX,probeY));
                if constexpr(Record)++coordinates;
                const auto x=destinations->x[probe],y=destinations->y[probe];
                if((!Raw || 2*i>=firstCandidate) && x<map.getW() && y<map.getH() && consider.template operator()<false>(Uint32(map.coordToIndex(x,y)),x,y,probe,unsigned(i)) && firstAvailable)break;
                if constexpr(Raw) {
                    // ClosestGeometric scans both candidates; FirstAvailable
                    // reaches this only when XY did not yield its winner.
                    if constexpr(Record)++rawAttempts;
                    if(consider.template operator()<true>(probe,unsigned(probeX)&(map.getW()-1),unsigned(probeY)&(map.getH()-1),probe,unsigned(i)) && firstAvailable)break;
                }
            }
            if constexpr(Record)if(const auto* observer=map.resourceClaimProbe())
                observer->rawResourceProbes(destinationPurpose,coordinates,xyChecks,rawAttempts,rawChecks,rawImprovements);
        };
        if(config.directResourceProbes) {
            const auto* observer=map.resourceClaimProbe();
            if(observer && observer->observesDecisionOutcomes())search.template operator()<true,true>();
            else search.template operator()<true,false>();
        } else search.template operator()<false,false>();
        if(bestAccess==UINT32_MAX) {
            if(keptByMargin())return true;
            if(config.usesRecovery() && recovery.mayRoute && !activeMaterialClaim)
                map.finishClaimRecoverySelection(*this,candidateCount,candidateCount);
            return false;
        }
        const auto oldTask=materialClaimTask;
        if(!commitResolvedClaim(*this,*bestResolved))return false;
        if(config.directResourceProbes && (bestOrdinal&1))
            if(const auto* observer=map.resourceClaimProbe();observer && observer->observesDecisionOutcomes())
                observer->rawResourceProbes(destinationPurpose,0,0,0,0,0,1);
        if(oldTask!=materialClaimTask || materialClaimPolicyState.probe==UINT32_MAX) {
            materialClaimPolicyState.probe=bestProbe;
            if(config.usesRecovery())map.finishClaimRecoverySelection(*this,bestOrdinal,candidateCount);
        }
        return true;
    }
    for(std::size_t i=0;i<pattern.x.size();++i) {
        const auto probe=map.coordToIndex(posX+pattern.x[i],posY+pattern.y[i]);
        const auto x=destinations->x[probe],y=destinations->y[probe];
        if(x>=unsigned(map.getW()) || y>=unsigned(map.getH()))continue;
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
    releaseInvalidActiveMaterialClaim();
    const auto access=Uint32(map.coordToIndex(x,y));
    if(activeMaterialClaim) {
        if(activeMaterialClaim->access==access)return true;
        if(map.materialClaimPolicy()!=ResourceClaims::ActiveClaimPolicy::Experimental ||
            !map.materialClaimPolicyConfig().opportunisticAdjacentPickup)return false;
        const auto held=activeMaterialClaim->access;
        // Finish an already reachable reservation first. Otherwise exchange the
        // distant claim only if adjacent stock can be reserved transactionally.
        if(map.warpDistMax(posX,posY,int(held%map.getW()),int(held/map.getW()))<=1 ||
            map.warpDistMax(posX,posY,int(access%map.getW()),int(access/map.getW()))>1)return false;
    }
    return acquireMaterialClaimAt(access);
}

bool Unit::takeActiveMaterialClaim()
{
    if(const auto invalid=activeMaterialClaimInvalidity()){releaseActiveMaterialClaim(*invalid);return false;}
    const auto claim=*activeMaterialClaim;
    if(owner->map->coordToIndex(posX+dx,posY+dy)!=claim.access){releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::PickupMismatch);return false;}
    // The reservation is no longer outstanding when the stock mutation checks
    // other owners' remaining capacity. The copied source never changes.
    const auto* probe=owner->map->resourceClaimProbe();
    if(probe)probe->liveEvent(*this,ResourceClaims::Probe::LiveEvent::HarvestBegin);
    releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Harvest);
    const bool taken=owner->map->takeClaimedMaterial(claim,owner->me);
    if(taken && owner->map->materialClaimPolicy()==ResourceClaims::ActiveClaimPolicy::Experimental &&
        owner->map->materialClaimPolicyConfig().usesRecovery())owner->map->claimRecoveryHarvested(*this);
    if(probe)probe->liveEvent(*this,taken?ResourceClaims::Probe::LiveEvent::Harvested:ResourceClaims::Probe::LiveEvent::HarvestFailed);
    return taken;
}

void Unit::handleActiveMaterialMovement()
{
    auto& map=*owner->map;
    const auto* observer=map.resourceClaimProbe();
    if(observer && !observer->observesDecisionOutcomes())observer=nullptr;
    std::optional<ResourceClaims::Probe::DecisionOutcome> outcome;
    const bool experimental=map.materialClaimPolicy()==ResourceClaims::ActiveClaimPolicy::Experimental;
    const auto& config=map.materialClaimPolicyConfig();
    auto& cadence=materialClaimPolicyState;
    const bool recoveryEnabled=experimental && config.usesRecovery();
    using F=ResourceClaims::ClaimPolicyConfig::Fallback;
    const bool gradientFallback=experimental && config.fallback==F::Gradient;
    const auto fallback=[&](unsigned failures) {
        cadence.fallbackCount=failures;
        // The movement itself follows the finished decision outcome below.
        if(gradientFallback){if(observer)outcome->fallback="gradient";return;}
        const bool release=config.fallback==F::ReleaseJob || (config.fallback==F::BoundedWaitRelease && failures>=config.fallbackAfterDecisions);
        if(const auto* probe=map.resourceClaimProbe())probe->liveEvent(*this,release?ResourceClaims::Probe::LiveEvent::FallbackRelease:
            config.fallback==F::Roam?ResourceClaims::Probe::LiveEvent::FallbackRoam:ResourceClaims::Probe::LiveEvent::FallbackWait);
        if(observer)outcome->fallback=release?"release_job":config.fallback==F::Roam?"roam":"wait";
        if(release) {setNeedToRecheckMedical(true);stopAttachedForBuilding(false);movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;}
        else if(config.fallback==F::Roam)movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
    };
    if(experimental) {
        if(cadence.decisionOrdinal==UINT64_MAX)throw std::overflow_error("material claim decision ordinal exhausted");
        ++cadence.decisionOrdinal;
    }
    releaseInvalidActiveMaterialClaim();
    if(observer)outcome=observer->beginDecisionOutcome(*this,false);
    const bool refresh=experimental && activeMaterialClaim && config.destinationRefreshJ
        && cadence.decisionOrdinal-cadence.lastGoalAttempt>=config.destinationRefreshJ;
    if(!activeMaterialClaim || refresh) {
        std::optional<GradientFieldKey> key;
        const auto* field=map.getMaterialGradientSlot(owner->teamNumber,destinationPurpose,movementAccess(),false,nullptr,&key);
        // Claims consume the material field even when selection finds only
        // fully reserved stock. Keep adaptive refresh demand alive while these
        // workers wait for growth or a different available destination.
        if(key)map.gradientMovementDemand(*key);
        if(experimental)cadence.lastGoalAttempt=cadence.decisionOrdinal;
        if(field) {
            const auto token=map.observeMaterialClaimDecision(*this,field);const auto previousTask=materialClaimTask;
            if(observer){outcome=observer->beginDecisionOutcome(*this,false);outcome->token=token;outcome->attempted=true;}
            const bool selected=selectActiveMaterialClaim(field);
            if(observer)outcome->selected=selected;
            if(refresh)if(const auto* probe=map.resourceClaimProbe())probe->liveEvent(*this,!selected?ResourceClaims::Probe::LiveEvent::GoalRefreshFailed:
                previousTask==materialClaimTask?ResourceClaims::Probe::LiveEvent::GoalRefreshSame:ResourceClaims::Probe::LiveEvent::GoalRefreshChanged);
        }
        else if(observer) {observer->missingDecision(*owner->game,*this,nullptr,UINT32_MAX,0,true);outcome->noAttempt="missing_field";}
        // Failed refresh leaves the old valid reservation and route untouched.
    }
    if(observer)observer->selectedDecisionOutcome(*outcome,*this);
    dx=dy=0;direction=UNIT_DIRECTION_NONE;movement=MOV_GOING_DX_DY;
    if(!activeMaterialClaim) {
        setValidTarget(false);
        if(observer)outcome->fallbackReason="no_resource";
        if(experimental) {
            if(recoveryEnabled) {
                // Route admission cooldown is not evidence that stock is absent.
                if(map.beginClaimRecoverySelection(*this).mayRoute)
                    fallback(map.recordClaimFailure(*this,ResourceClaims::ClaimFailureKind::NoResource));
                else if(observer){outcome->fallbackReason="cooldown";outcome->noAttempt="cooldown";}
            } else fallback(cadence.fallbackCount==UINT32_MAX?UINT32_MAX:cadence.fallbackCount+1);
        }
        if(observer)observer->finishDecisionOutcome(*outcome,*this);
        // Also during route-admission cooldown: the worker never idles here.
        if(gradientFallback) {
            if(const auto* probe=map.resourceClaimProbe())probe->policyExtension(ResourceClaims::Probe::PolicyExtension::FallbackGradient);
            handleClassicMaterialMovement();
        }
        return;
    }
    if(experimental && !recoveryEnabled)cadence.fallbackCount=0;
    int stepX=0,stepY=0;
    auto result=ResourceClaims::StepResult::Failed;
    if(experimental && config.usePrimaryGradient)result=map.nextClaimGradientStep(*this,&stepX,&stepY);
    if(result==ResourceClaims::StepResult::Failed)result=map.nextClaimRouteStep(*this,&stepX,&stepY);
    // Pursuit while routed (moving or traffic-blocked) is gradient-relevant;
    // a computing route already took its own gradient step or is unrelated.
    if(experimental && config.claimGradientDemand && result!=ResourceClaims::StepResult::Failed && result!=ResourceClaims::StepResult::Arrived)
        map.claimPursuitGradientSignal(*this,!map.claimRouteComputing(*this),result==ResourceClaims::StepResult::Ready);
    if(observer)outcome->route=result==ResourceClaims::StepResult::Ready?"ready":result==ResourceClaims::StepResult::Pending?"pending":result==ResourceClaims::StepResult::Arrived?"arrived":"failed";
    if(result==ResourceClaims::StepResult::Ready) {
        dx=Sint8(stepX);dy=Sint8(stepY);directionFromDxDy();
    } else if(result==ResourceClaims::StepResult::Failed) {
        if(observer)outcome->fallbackReason="path_failed";
        const auto failures=recoveryEnabled?map.recordClaimFailure(*this,ResourceClaims::ClaimFailureKind::PathFailed):0;
        releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::RouteFailed);
        if(recoveryEnabled && failures)fallback(failures);
    }
    if(observer)observer->finishDecisionOutcome(*outcome,*this);
}
