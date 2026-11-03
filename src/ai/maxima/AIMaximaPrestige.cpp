// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIMaxima.h"
#include "AIMaximaBuildings.h"
#include "observation/ObservationQueries.h"
#include "Building.h"
#include "Unit.h"
#include "Order.h"
#include <array>
#include <climits>
#include <iterator>
#include <tuple>

namespace AIMaxima
{
using namespace AIMaximaPlacement;
using namespace AIMaximaRuntime;
namespace
{
bool committed(const DevelopmentAction& a)
{ return a.state==ParcelReserved || a.state==CreateIssued || a.state==SiteObserved; }
int currentTotal(const AIEngine::AIWorldView& world)
{
    // The engine refreshes totalPrestige on its 32-tick rules cadence, while
    // individual building completions update team scores immediately. Include
    // those gains when projecting a completion. Retaining a larger cached total
    // is conservative until the next refresh after a prestige building is lost.
    int64_t total=0;
    for(const auto& team:world.teams)total+=team.prestige;
    return int(std::clamp<int64_t>(std::max<int64_t>(world.totalPrestige,total),0,INT_MAX));
}
int completionType(const AIEngine::AIWorldView& world,const AIEngine::BuildingView& b)
{
    if(b.constructionResultState==Building::REPAIR)return b.constructionOriginTypeNum;
    const auto& kind=world.catalog->at(b.typeNum);
    int target=kind.next;
    if(!kind.site && target>=0)target=world.catalog->at(target).next;
    return target;
}
int pendingGain(const AIEngine::AIWorldView& world,const AIEngine::BuildingView& b)
{
    if(b.constructionResultState!=Building::NEW_BUILDING
        && b.constructionResultState!=Building::UPGRADE
        && b.constructionResultState!=Building::REPAIR)return 0;
    const int target=completionType(world,b);
    if(target<0 || size_t(target)>=world.catalog->size())return 0;
    return int(std::clamp<int64_t>(int64_t(world.catalog->at(target).resolvedType.prestige)-world.catalog->at(b.typeNum).resolvedType.prestige,INT_MIN,INT_MAX));
}
int liveGain(const AIEngine::AIWorldView& world,const AIEngine::BuildingView& b)
{ return b.constructionResultState==Building::UPGRADE?pendingGain(world,b):0; }
}
bool Maxima::prestige_enabled(const Context& runtime) const
{ return strategy.prestige.enabled && runtime.observation().prestigeWinCondition; }
int Maxima::prestige_pipeline_allowance(const BuildingProfile& profile,Prestige::PipelineCapacity* capacity) const
{
    Prestige::PipelineCapacity scratch;
    auto& facts=capacity?*capacity:scratch;facts={};
    facts.trainingReserve=std::max(0,labour_plan.trainingReserve);
    facts.crew=std::max({1,strategy.staffing.construction_training_workers,
        strategy.upgrades.level1_workers,strategy.upgrades.level2_workers});
    facts.remainingSteps=prestige_race.finishSteps;
    if(!prestige_enabled(context) || prestige_race.state==Prestige::Hold)return 0;
    // Saved one-pipeline strategies keep their former demand without consulting
    // new capacity estimates. A winning final step never opens parallel work.
    const int maximum=std::clamp(strategy.prestige.max_pipelines,1,4);
    if(maximum==1 || prestige_race.state==Prestige::Finish)return 1;
    const auto* target=profile.atLevel(budget.prestige_target_level);
    if(!target)return 1;
    const auto& world=context.observation();
    const auto construction=[&](const AIEngine::BuildingView& b) {
        return b.buildingState!=Building::DEAD && b.buildingState!=Building::WAITING_FOR_DESTRUCTION
            && (world.catalog->at(b.typeNum).site || b.constructionResultState!=Building::NO_CONSTRUCTION);
    };
    const int selectedRoot=AIMaximaBuildings::lineageRoot(world,profile.buildingType);
    const auto root=[&](const AIEngine::BuildingView& b) {
        return AIMaximaBuildings::lineageRoot(world,b.typeNum)==selectedRoot;
    };
    facts.sampled=true;
    facts.qualifiedIdle=facts.rootAssigned=facts.ordinaryUnmet=facts.unobservedCrews=0;
    std::map<int,int> assigned;
    for(const auto* unit:world.unitsForTeam(context.teamNumber())) {
        if(!unit || unit->isDead || unit->hp<=0 || !(unit->capabilityFlags&UnitRuntimeTraits::Construct)
            || !AIEngine::ObservationQueries::matchesStrategyUnitRole(world,*unit,WORKER)
            || unit->medical==Unit::MED_DAMAGED
            || (!world.configuration->isHungerDisabled() && unit->medical==Unit::MED_HUNGRY)
            || unit->activity==Unit::ACT_UPGRADING
            || (unit->jobPurpose!=UnitJobPurpose::None && unit->jobPurpose!=UnitJobPurpose::Transport))continue;
        const auto* building=world.building(unit->attached);
        const bool builder=unit->activity!=Unit::ACT_RANDOM && building
            && building->team==int(context.teamNumber()) && construction(*building)
            && (unit->jobPurpose==UnitJobPurpose::None || unit->jobPurpose==UnitJobPurpose::Transport);
        if(builder)++assigned[building->gid];
        if(unit->constructionLevel>=target->requiredWorkerLevel) {
            if(unit->activity==Unit::ACT_RANDOM)++facts.qualifiedIdle;
            else if(builder && root(*building))++facts.rootAssigned;
        }
    }
    std::set<int> observed;
    std::map<int,int> requested;
    std::map<std::tuple<int,int,int>,int> observedSites;
    for(const auto* building:world.buildingsForTeam(context.teamNumber())) {
        if(!building || !construction(*building))continue;
        observed.insert(building->gid);
        const auto& kind=world.catalog->at(building->typeNum);
        observedSites[{AIMaximaBuildings::lineageRoot(world,building->typeNum),
            world.normalizeX(building->posX-kind.decLeft),world.normalizeY(building->posY-kind.decTop)}]=building->gid;
        if(!root(*building))requested[building->gid]=std::max({0,building->desiredMaxUnitWorking,building->maxUnitWorking});
    }
    std::set<int> planned;
    for(const auto& [id,action]:development_planner.actions()) {
        if(!committed(action))continue;
        const bool validType=action.buildingType>=0 && size_t(action.buildingType)<world.catalog->size();
        if(validType && AIMaximaBuildings::lineageRoot(world,action.buildingType)==selectedRoot)continue;
        if(action.buildingId>=0 && !planned.insert(action.buildingId).second)continue;
        const auto* building=context.get_building_register().get_building(action.buildingId);
        if(building && building->buildingState==Building::ALIVE && !construction(*building)
            && !context.get_building_register().is_building_upgrading(action.buildingId)) {
            // A fresh completion can precede the planner's next observation.
            // The register lookup above validates generation, so a replacement
            // in the same engine slot cannot release the old project's crew.
            const auto& profiles=collect_building_profiles();
            const auto plan=std::find_if(profiles.begin(),profiles.end(),[&](const auto& p) {
                return p.buildingType==action.buildingType;
            });
            int level=0;
            if(plan!=profiles.end())for(const auto& variant:plan->levels)
                if(variant.completedType==building->typeNum)level=variant.level;
            const bool built=(action.type==BuildStandalone || action.type==BuildCampusMember
                || action.type==UpgradeBuilding) && action.targetLevel>0 && level>=action.targetLevel;
            const bool repaired=action.type==RepairBuilding && level>0 && building->hp>=building->maxHp;
            if(built || repaired)continue;
        }
        if(building && observed.count(building->gid)) {
            if(auto demand=requested.find(building->gid);demand!=requested.end())
                demand->second=std::max(demand->second,action.workers);
            continue;
        }
        // A created site may be visible before the runtime resolves its pending
        // register ID. Match the existing parcel so its crew is not reserved twice.
        if(validType) {
            const auto site=observedSites.find({AIMaximaBuildings::lineageRoot(world,action.buildingType),
                world.normalizeX(action.centerX),world.normalizeY(action.centerY)});
            if(site!=observedSites.end()) {
                if(auto demand=requested.find(site->second);demand!=requested.end())
                    demand->second=std::max(demand->second,action.workers);
                continue;
            }
        }
        facts.unobservedCrews+=std::max(0,action.workers);
    }
    for(const auto& [gid,demand]:requested)facts.ordinaryUnmet+=std::max(0,demand-assigned[gid]);
    const int64_t spare=int64_t(facts.qualifiedIdle)+facts.rootAssigned-facts.trainingReserve
        -facts.ordinaryUnmet-facts.unobservedCrews;
    facts.workerLimit=int(std::clamp<int64_t>(spare/facts.crew,1,maximum));
    facts.materialLimit=maximum;
    std::array<int64_t,MaterialCount> cost{};
    for(const auto& level:profile.levels)if(level.level<=target->level)
        for(int material=0;material<MaterialCount;++material)cost[material]+=level.constructionMaterials[material];
    std::array<int,MaterialCount> sources{};
    sources[materialIndex(MaterialId::Food)]=environment.accessible_corn;
    sources[materialIndex(MaterialId::Wood)]=environment.accessible_wood;
    sources[materialIndex(MaterialId::Stone)]=environment.accessible_stone;
    sources[materialIndex(MaterialId::Algae)]=environment.accessible_algae;
    for(int material=0;material<MaterialCount;++material)if(cost[material]>0 && sources[material]<=0) {
        // Local building stores fund only that building. Additional pipelines
        // need a measured source or enough communal stock for their full chains.
        const int64_t stock=std::max<int64_t>(0,int64_t(context.observedTeam().materials[material])
            -context.observedTeam().reservedMaterials[material]);
        facts.materialLimit=int(std::min<int64_t>(facts.materialLimit,std::max<int64_t>(1,stock/cost[material])));
    }
    return std::min({facts.workerLimit,facts.materialLimit,std::max(1,facts.remainingSteps)});
}
std::vector<Prestige::Standing> Maxima::prestige_standings(const Context& runtime) const
{
    const auto& world=runtime.observation();
    const bool alliesWin=world.prestigeAlliesShareVictory;
    const auto& owner=runtime.observedTeam();
    std::vector<Prestige::Standing> result;
    for(const auto& team:world.teams)
        result.push_back({team.prestige,team.alive,alliesWin
            && bool(owner.allies&team.mask) && bool(team.allies&owner.mask)});
    return result;
}
int Maxima::prestige_gain(int root,int from,int to) const
{
    const auto* target=profile_variant(root,to);
    const auto* origin=profile_variant(root,from);
    if(!target || target->completedType<0)return 0;
    const auto& catalog=*context.observation().catalog;
    return int(std::clamp<int64_t>(int64_t(catalog.at(target->completedType).resolvedType.prestige)
        -(origin && origin->completedType>=0?catalog.at(origin->completedType).resolvedType.prestige:0),INT_MIN,INT_MAX));
}
bool Maxima::prestige_action_allowed(Context& runtime,int root,int from,int to,int excludedAction) const
{
    if(!prestige_enabled(runtime))return true;
    const int gain=prestige_gain(root,from,to);
    if(gain<=0)return true;
    const auto& world=runtime.observation();
    if(!Prestige::safe(prestige_standings(runtime),runtime.teamNumber(),currentTotal(world),world.prestigeToReach,gain))return false;
    int excludedBuilding=-1;
    const auto excluded=development_planner.actions().find(excludedAction);
    if(excluded!=development_planner.actions().end())excludedBuilding=excluded->second.buildingId;
    for(const auto& [id,a]:development_planner.actions())
        if(id!=excludedAction && committed(a) && prestige_gain(a.buildingType,a.fromLevel,a.targetLevel)>0)return false;
    // Include upgrades initiated outside the planner, including older saves.
    const auto* excludedLive=excludedBuilding>=0?runtime.get_building_register().get_building(excludedBuilding):nullptr;
    for(const auto* b:world.buildingsForTeam(runtime.teamNumber()))
        if(b && (!excludedLive || b->identity!=excludedLive->identity) && pendingGain(world,*b)>0)return false;
    return prestige_cancellations.empty();
}
void Maxima::assess_prestige(Context& runtime)
{
    const auto& world=runtime.observation();
    prestige_race={};
    budget.prestige_target_level=0;
    budget.prestige_desired_buildings=0;
    if(!prestige_enabled(runtime))return;
    const auto standings=prestige_standings(runtime);
    const auto& stat=runtime.observedTeam().statistics;
    int pending=0;std::map<int,int> projected;
    for(const auto& [id,a]:development_planner.actions())if(committed(a)) {
        int delta=std::max(0,prestige_gain(a.buildingType,a.fromLevel,a.targetLevel));
        const auto* b=runtime.get_building_register().get_building(a.buildingId);
        // Site observation can precede registration. Match only unresolved new
        // work by its reserved parcel; a stale registered generation stays stale.
        if(!b && a.buildingId<0 && (a.type==BuildStandalone || a.type==BuildCampusMember))
            for(const auto* candidate:world.buildingsForTeam(runtime.teamNumber())) {
                if(!candidate)continue;
                const auto& type=world.catalog->at(candidate->typeNum);
                if(AIMaximaBuildings::lineageRoot(world,candidate->typeNum)==a.buildingType
                    && world.normalizeX(candidate->posX-type.decLeft)==world.normalizeX(a.centerX)
                    && world.normalizeY(candidate->posY-type.decTop)==world.normalizeY(a.centerY)) {
                    b=candidate;break;
                }
            }
        if(b) {
            const auto* target=profile_variant(a.buildingType,a.targetLevel);
            if(b->constructionResultState!=Building::NO_CONSTRUCTION)delta=std::max(0,pendingGain(world,*b));
            else if(target && b->typeNum==target->completedType)delta=0;
            projected[b->gid]=std::max(projected[b->gid],delta);
        } else pending=int(std::min<int64_t>(INT_MAX,int64_t(pending)+delta));
    }
    for(const auto* b:world.buildingsForTeam(runtime.teamNumber()))if(b)
        projected[b->gid]=std::max(projected[b->gid],std::max(0,pendingGain(world,*b)));
    for(const auto& [gid,gain]:projected)
        pending=int(std::min<int64_t>(INT_MAX,int64_t(pending)+gain));
    // Accessible source facts are already measured by the environment collector.
    std::array<int,MaterialCount> sources{};
    sources[materialIndex(MaterialId::Food)]=environment.accessible_corn;
    sources[materialIndex(MaterialId::Wood)]=environment.accessible_wood;
    sources[materialIndex(MaterialId::Stone)]=environment.accessible_stone;
    sources[materialIndex(MaterialId::Algae)]=environment.accessible_algae;
    for(int m=0;m<MaterialCount;++m)sources[m]=std::max(sources[m],int(runtime.observedTeam().materials[m]));
    std::tuple<int,int,int,int> best{INT_MAX,INT_MAX,INT_MAX,INT_MAX};
    int root=-1,to=0,gain=0;bool ready=false;
    for(const auto& profile:collect_building_profiles())for(const auto& target:profile.levels)
    {
        const int delta=prestige_gain(profile.buildingType,target.level-1,target.level);
        if(delta<=0 || !target.available)continue;
        // The first economic policy funds reversible upgrades. New prestige
        // sites have no cancel origin; positive earlier stages or sites can
        // make cancellation itself award prestige and trigger a loss.
        bool reversible=target.level>1;
        for(const auto& level:profile.levels)if(level.level<=target.level) {
            reversible&=level.engineType>=0
                && world.catalog->at(level.engineType).resolvedType.prestige==0;
            if(level.level<target.level)reversible&=level.completedType>=0
                && world.catalog->at(level.completedType).resolvedType.prestige==0;
        }
        if(!reversible)continue;
        int current=0;
        const AIEngine::BuildingView* candidate=nullptr;
        for(const auto& [id,record]:runtime.get_building_register().found()) {
            const auto* b=runtime.get_building_register().get_building(id);
            if(!b)continue;
            for(const auto& level:profile.levels)if(b->typeNum==level.completedType && level.level<target.level
                && (level.level>current || (level.level==current && (!candidate || b->gid<candidate->gid)))) {
                current=level.level;candidate=b;
            }
        }
        bool available=true;int cost=0;
        for(const auto& level:profile.levels)if(level.level>current && level.level<=target.level) {
            available&=level.available;
            for(int m=0;m<MaterialCount;++m) {
                cost+=level.constructionMaterials[m];
                // Private stock can fund this building's next upgrade, but is
                // not a supply source for unrelated buildings or later stages.
                const bool stored=candidate && level.level==current+1
                    && candidate->localMaterials[m]>=level.constructionMaterials[m];
                if(level.constructionMaterials[m]>0 && sources[m]<=0 && !stored)available=false;
            }
        }
        if(!available)continue;
        const int64_t remaining=std::max<int64_t>(0,int64_t(world.prestigeToReach)-currentTotal(world));
        const int64_t closing=((remaining+delta-1)/delta)*delta;
        const auto rank=std::tuple{Prestige::wins(standings,runtime.teamNumber(),closing)?0:1,
            target.level-current,cost,profile.buildingType};
        if(rank>=best)continue;
        best=rank;root=profile.buildingType;to=target.level;gain=delta;
        int trained=0;
        for(int level=std::clamp(target.requiredWorkerLevel,0,NB_UNIT_LEVELS-1);level<NB_UNIT_LEVELS;++level)
            trained+=stat.workersByConstructionLevel[level];
        const bool first=runtime.observedTeam().prestige<=0;
        ready=current==to-1 && trained>=(first?strategy.upgrades.first_prestige_trained_workers:strategy.upgrades.second_prestige_trained_workers)
            && snapshot.population>=strategy.upgrades.level2_population_min
            && (first || snapshot.population>=strategy.upgrades.second_prestige_population_min);
    }
    prestige_race=Prestige::assess(standings,runtime.teamNumber(),currentTotal(world),world.prestigeToReach,
        prestige_enabled(runtime),posture==PostureRecover || severe_food_emergency() || severe_colony_emergency(),gain,
        root>=0 && strategy.upgrades.enabled && !world.configuration->isUnitUpgradesDisabled()
            && snapshot.population>=strategy.economy.school_population_min,ready,pending,
        demands.technology,strategy.prestige.investment_floor,strategy.prestige.urgency_weight,strategy.prestige.finish_priority);
    prestige_race.buildingType=root;
    budget.prestige_target_level=to;
    emit_telemetry(runtime,"prestige_race","\tstate="+std::to_string(prestige_race.state)
        +"\treason="+std::to_string(prestige_race.reason)+"\town="+std::to_string(prestige_race.own)
        +"\tleader="+std::to_string(prestige_race.leader)+"\tremaining="+std::to_string(prestige_race.remaining)
        +"\tpending="+std::to_string(pending)+"\tpriority="+std::to_string(prestige_race.priority));
}
void Maxima::guard_prestige_completions(Context& runtime)
{
    if(!prestige_enabled(runtime))return;
    const auto& world=runtime.observation();
    const auto standings=prestige_standings(runtime);
    const auto safe=[&](int gain){return Prestige::safe(standings,runtime.teamNumber(),currentTotal(world),world.prestigeToReach,gain);};
    const auto unsafeRestoration=[&](const AIEngine::BuildingView& b) {
        const int origin=b.constructionOriginTypeNum;
        if(origin<0 || size_t(origin)>=world.catalog->size())return false;
        const int64_t restored=int64_t(world.catalog->at(origin).resolvedType.prestige)
            -world.catalog->at(b.typeNum).resolvedType.prestige;
        return restored>0 && !Prestige::safe(standings,runtime.teamNumber(),currentTotal(world),
            world.prestigeToReach,restored);
    };
    runtime.reject_queued_orders([&](const Order& queued) {
        const auto* cancel=dynamic_cast<const OrderCancelConstruction*>(&queued);
        const auto* b=cancel?world.buildingAtSlot(cancel->gid):nullptr;
        if(!b || (queued.aiSelectedTarget && queued.aiSelectedTarget->generation!=b->identity.generation)
            || !unsafeRestoration(*b))return false;
        const auto pending=prestige_cancellations.find(b->gid);
        if(pending!=prestige_cancellations.end() && pending->second.generation==b->identity.generation)
            prestige_cancellations.erase(pending);
        director.invalidate();return true;
    });
    // Queued upgrades can wait across observations. Reject an unsafe order before
    // dispatch, even when ordinary development has not reached its next cadence.
    std::set<int> queuedPrestige;
    const auto queuedGain=[&](const Order& queued) {
        const auto* order=dynamic_cast<const OrderConstruction*>(&queued);
        const auto* b=order?world.buildingAtSlot(order->gid):nullptr;
        if(!b || b->hp<b->maxHp || b->constructionResultState==Building::REPAIR)return 0;
        const int target=completionType(world,*b);
        return target<0?0:int(std::clamp<int64_t>(int64_t(world.catalog->at(target).resolvedType.prestige)
            -world.catalog->at(b->typeNum).resolvedType.prestige,INT_MIN,INT_MAX));
    };
    runtime.reject_queued_orders([&](const Order& queued) {
        if(queuedGain(queued)>0)queuedPrestige.insert(static_cast<const OrderConstruction&>(queued).gid);
        return false;
    });
    bool completionCommitted=!prestige_cancellations.empty(),fixedCompletion=false;
    for(const auto* b:world.buildingsForTeam(runtime.teamNumber()))
        if(b && pendingGain(world,*b)>0) {
            completionCommitted=true;
            fixedCompletion|=b->constructionResultState!=Building::UPGRADE;
        }
    for(const auto& [id,record]:runtime.get_building_register().found())
        if(record.awaitingUpgradeExecution && !queuedPrestige.count(record.gid)) {
            const auto* b=runtime.get_building_register().get_building(id);
            if(b && queuedGain(OrderConstruction(b->gid,0,0))>0)completionCommitted=true;
        }
    runtime.reject_queued_orders([&](const Order& queued) {
        const auto* order=dynamic_cast<const OrderConstruction*>(&queued);
        const int gain=queuedGain(queued);
        if(gain<=0)return false;
        if(safe(gain) && !completionCommitted){completionCommitted=true;return false;}
        for(const auto& [key,a]:development_planner.actions()) {
            const auto* target=runtime.get_building_register().get_building(a.buildingId);
            if(target && target->gid==order->gid && a.type==UpgradeBuilding && committed(a))
                development_planner.markInvalidated(key,UpgradeBlocked,0);
        }
        director.invalidate();return true;
    });
    for(auto i=prestige_cancellations.begin();i!=prestige_cancellations.end();) {
        const auto* b=world.buildingAtSlot(i->first);
        if(!b || b->identity.generation!=i->second.generation || liveGain(world,*b)<=0) {
            const auto action=development_planner.actions().find(i->second.actionId);
            // A completed upgrade wins the race against cancellation. Let normal
            // reconciliation record its completion instead of reporting a failure.
            bool completed=false;
            if(b && b->identity.generation==i->second.generation && action!=development_planner.actions().end()) {
                const auto* target=profile_variant(action->second.buildingType,action->second.targetLevel);
                completed=target && b->typeNum==target->completedType;
            }
            if(!completed && i->second.actionId>=0)
                development_planner.markInvalidated(i->second.actionId,b?UpgradeBlocked:DestroyedDuringConstruction,0);
            emit_telemetry(runtime,"prestige_cancel_resolved","\tgid="+std::to_string(i->first));
            i=prestige_cancellations.erase(i);director.invalidate();
        } else if(i->second.acknowledged) {
            // An accepted engine order can still be a no-op; retry only after its receipt.
            i=prestige_cancellations.erase(i);
        } else ++i;
    }
    int keepActive=-1;
    for(const auto* b:world.buildingsForTeam(runtime.teamNumber()))
        if(!fixedCompletion && b && liveGain(world,*b)>0 && safe(liveGain(world,*b)) && !prestige_cancellations.count(b->gid)
            && (keepActive<0 || b->gid<keepActive))keepActive=b->gid;
    for(const auto* b:world.buildingsForTeam(runtime.teamNumber())) {
        if(!b || prestige_cancellations.count(b->gid))continue;
        const int gain=liveGain(world,*b);
        if(gain<=0 || (safe(gain) && b->gid==keepActive))continue;
        const int origin=b->constructionOriginTypeNum;
        if(origin<0 || size_t(origin)>=world.catalog->size())continue;
        if(unsafeRestoration(*b))continue;
        int id=-1;
        for(const auto& [key,record]:runtime.get_building_register().found())
            if(record.gid==b->gid && record.runtimeIdentity==b->identity.generation){id=key;break;}
        int action=-1;
        for(const auto& [key,a]:development_planner.actions())if(a.buildingId==id && committed(a))action=key;
        prestige_cancellations[b->gid]={id,action,b->identity.generation,false};
        runtime.push_urgent_order(std::make_shared<OrderCancelConstruction>(b->gid,0));
        emit_telemetry(runtime,"prestige_cancel_issued","\tgid="+std::to_string(b->gid)
            +"\treason="+(safe(gain)?"parallel_commitment":"unsafe_finish"));
    }
}
}
