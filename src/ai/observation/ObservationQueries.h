// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/observation/AIWorldView.h"
#include "BuildingCapabilities.h"
#include "Building.h"
#include "Unit.h"
#include "Order.h"
#include <algorithm>
#include <climits>

// These functions operate directly on canonical captured records. They retain
// no entity storage, relationships or observation lease.
namespace AIEngine::ObservationQueries
{
inline const TerrainProperties& terrain(const AIEngine::AIWorldView& world,std::size_t index)
{return world.terrainPropertiesAt(index);}
inline const TerrainProperties& terrain(const AIEngine::AIWorldView& world,int x,int y)
{return terrain(world,world.tileIndex(x,y));}
inline bool resourceTakeable(const AIEngine::AIWorldView& world,int x,int y,int material)
{return MapState::hasMaterialSlot(world.state(),world.tileIndex(x,y),material);}
inline bool discovered(const AIEngine::AIWorldView& world,int x,int y,Uint32 mask)
{return (world.visibilityAt(world.tileIndex(x,y)).discovered&mask)!=0;}
inline bool visible(const AIEngine::AIWorldView& world,int x,int y,Uint32 mask)
{return (world.visibilityAt(world.tileIndex(x,y)).visible&mask)!=0;}
inline bool forbidden(const AIEngine::AIWorldView& world,int x,int y,Uint32 mask)
{return (world.areasAt(world.tileIndex(x,y)).forbidden&mask)!=0;}
inline bool clearing(const AIEngine::AIWorldView& world,int x,int y,Uint32 mask)
{return (world.areasAt(world.tileIndex(x,y)).clear&mask)!=0;}
inline bool farmed(const AIEngine::AIWorldView& world,int x,int y,Uint32 mask)
{return (world.areasAt(world.tileIndex(x,y)).farm&mask)!=0;}
inline int distanceMax(const AIEngine::AIWorldView& world,int x,int y,int a,int b)
{
    const int dx=std::abs(world.normalizeX(x)-world.normalizeX(a)),dy=std::abs(world.normalizeY(y)-world.normalizeY(b));
    return std::max(std::min(dx,world.width-dx),std::min(dy,world.height-dy));
}
inline AIEngine::TileView spatialTile(const AIEngine::AIWorldView& world,int x,int y)
{
    const auto index=world.tileIndex(x,y);
    const auto& r=world.resourceAt(index);
    const auto& o=world.occupancyAt(index);const auto& a=world.areasAt(index);const auto v=world.visibilityAt(index);
    AIEngine::TileView result;
    result.cellRule=world.cellRuleAt(index);
    result.resource=r.resource;result.fertility=r.fertility;result.resourcesMayGrow=r.mayGrow;
    result.building=o.building;result.groundUnit=o.groundUnit;result.airUnit=o.airUnit;result.immobileUnit=o.immobileUnit;
    result.forbidden=a.forbidden;result.guard=a.guard;result.clear=a.clear;result.farm=a.farm;
    result.discovered=v.discovered;result.visible=v.visible;
    return result;
}
inline const BuildingType& buildingType(const AIEngine::AIWorldView& world, const AIEngine::BuildingView& building)
{
    return world.catalog->at(building.typeNum).resolvedType;
}
inline int constructionCompletionType(const AIEngine::AIWorldView& world, const AIEngine::BuildingView& building)
{
    return building.constructionResultState==::Building::REPAIR
        ? building.constructionOriginTypeNum : buildingType(world,building).nextLevel;
}
inline int realAttackStrength(const AIEngine::AIWorldView& world, const AIEngine::UnitView& unit)
{
    return int(std::clamp<Sint64>((Sint64(unit.performance[ATTACK_STRENGTH])+unit.experienceLevel)
        *world.configuration->getGlassCannonScale(), 0, INT_MAX));
}
// Preserve each controller's default head-count calibration while scaling
// altered food clocks and the recipient's share of scarce feeding seats.
// Called once per planning pass, outside entity scans.
template<class Counts> inline int normalizedFoodPopulation(const AIEngine::AIWorldView& world,const Counts& counts)
{
    constexpr long long precision=65536;
    long long demand=0;
    for(unsigned id=0;id<world.unitTypeCount();++id) {
        const auto& traits=world.unitTraits(id);
        if(!counts[id] || !traits.hungerRate)continue;
        // Old count-based controllers never adjusted their calibration for a
        // saved Race food clock. Keep that policy for migrated definitions.
        const int plannerHungerRate=traits.has(UnitRuntimeTraits::LegacyPerformancePolicies) ? 425 : traits.hungerRate;
        const long long relative=static_cast<long long>(plannerHungerRate)*150000*256*precision
            /(static_cast<long long>(traits.foodCapacity)*425*traits.feedingSpeedQ8);
        demand+=static_cast<long long>(counts[id])*relative;
    }
    return int(std::min<long long>(INT_MAX,(demand+precision-1)/precision));
}
inline bool matchesStrategyUnitRole(const AIEngine::AIWorldView& world,const AIEngine::UnitView& unit,unsigned role)
{
    // The three built-ins name strategy ledger roles; recipient identity can
    // differ. Enabled behaviors must have effective clocks to fulfill the role.
    const bool mobile=unit.performance[FLY]>0 || unit.performance[WALK]>0 || unit.performance[SWIM]>0;
    switch(role) {
        case WORKER:
            return mobile && (unit.capabilityFlags&UnitRuntimeTraits::Transport)
                && unit.performance[BUILD]>0 && unit.performance[HARVEST]>0
                && (!(unit.capabilityFlags&UnitRuntimeTraits::ExtendedCargo) || world.unitTraits(unit.typeNum).cargoCapacity>0);
        case EXPLORER:
            return mobile && (unit.capabilityFlags&UnitRuntimeTraits::Explore);
        case WARRIOR:
            // A stationary melee unit still contributes actual combat strength.
            return (unit.capabilityFlags&UnitRuntimeTraits::Melee) && unit.performance[ATTACK_SPEED]>0
                && realAttackStrength(world,unit)>0;
        default:
            return unit.typeNum==int(role);
    }
}
inline bool matchesConstructionRole(const AIEngine::AIWorldView& world,const AIEngine::UnitView& unit)
{
    // Existing construction jobs gather/deliver packets before building; the
    // engine still requires transport eligibility as well as Construct.
    return (unit.capabilityFlags&UnitRuntimeTraits::Construct) && matchesStrategyUnitRole(world,unit,WORKER);
}
// Planning-only projection: ability levels are independent, so a recipient can
// retain one clock while training another. Nonmonotonic tables need not have a
// single uniform level with every clock active. Compute the potential maxima
// before scanning live entities; actual role/training checks still use caches.
inline bool definitionCanServeStrategyRole(const AIEngine::AIWorldView& world,unsigned id,unsigned role)
{
    const auto& traits=world.unitTraits(id);
    AIEngine::UnitView probe;
    probe.typeNum=id;probe.capabilityFlags=traits.flags;probe.experienceLevel=0;
    std::fill(std::begin(probe.performance),std::end(probe.performance),0);
    for(const auto& level:world.unitCatalog().levels(id))
        for(const int ability:{WALK,SWIM,FLY,BUILD,HARVEST,ATTACK_SPEED,ATTACK_STRENGTH})
            probe.performance[ability]=std::max(probe.performance[ability],level.performance[ability]);
    if(!traits.has(UnitRuntimeTraits::LegacyPerformancePolicies)) {
        if(!traits.has(UnitRuntimeTraits::Walk))probe.performance[WALK]=0;
        if(!traits.has(UnitRuntimeTraits::Swim))probe.performance[SWIM]=0;
        if(!traits.has(UnitRuntimeTraits::Fly))probe.performance[FLY]=0;
    }
    return matchesStrategyUnitRole(world,probe,role);
}
inline bool definitionCanConstruct(const AIEngine::AIWorldView& world,unsigned id)
{
    return world.unitTraits(id).has(UnitRuntimeTraits::Construct) && definitionCanServeStrategyRole(world,id,WORKER);
}
inline AIPlanning::BuildingIntent trainingIntent(int ability)
{
    using I=AIPlanning::BuildingIntent;
    constexpr I intents[]={I::Count,I::Count,I::Count,I::TrainWalk,I::TrainSwim,I::TrainFly,
        I::TrainBuild,I::TrainHarvest,I::TrainAttackSpeed,I::TrainAttackStrength,
        I::TrainAirAttack,I::TrainBombing,I::TrainCreateWood,I::TrainCreateWheat,
        I::TrainCreateAlgae,I::TrainArmor,I::TrainHealth};
    static_assert(std::size(intents)==NB_ABILITY);
    return intents[ability];
}
// Includes qualification-only grants attached to otherwise inactive abilities.
// The immutable capability table resolves admission, keyed recipient selectors
// and enabled ability policies; live canLearn and levels are checked separately.
inline Uint32 usableTrainingAbilities(const AIEngine::AIWorldView& world,int type,unsigned id)
{
    const auto& semantics=world.catalog->at(type).resolvedType.semantics;
    const auto& traits=world.unitTraits(id);
    Uint32 result=0;
    for(int ability=0;ability<NB_ABILITY;++ability) {
        const auto& grant=semantics.training[ability];
        if((trainingIntent(ability)!=AIPlanning::BuildingIntent::Count
                && world.capabilities().matches(type,trainingIntent(ability),id))
            || (grant.enabled && grant.constructionLevel>0
                && traits.has(UnitRuntimeTraits::LearnConstruction)
                && (traits.learnableMask&(1u<<ability))
                && world.capabilities().matches(type,AIPlanning::BuildingIntent::TrainConstruction,id)
                && semantics.admittedUnits.matches(id,semantics.admittedUnitMask)
                && grant.units.matches(id,grant.unitMask)))result|=1u<<ability;
    }
    return result;
}
struct WorkerTrainingProjection
{
    std::vector<Uint8> labourProviders;
    std::vector<int> constructionLevels;
    std::vector<std::vector<Uint32>> courseMasks;
};
inline WorkerTrainingProjection workerTrainingProjection(const AIEngine::AIWorldView& world,bool swimming)
{
    using I=AIPlanning::BuildingIntent;
    WorkerTrainingProjection result{std::vector<Uint8>(world.catalog->size()),
        std::vector<int>(world.catalog->size()),std::vector<std::vector<Uint32>>(world.catalog->size())};
    std::vector<unsigned> recipients;
    std::vector<Uint8> carriers(world.unitTypeCount()),builders(world.unitTypeCount());
    for(unsigned id=0;id<world.unitTypeCount();++id) {
        const auto& definition=world.unitCatalog().definition(id);
        if(!definition.requiredExperiment.empty() && !world.configuration->getExperiments().has(definition.requiredExperiment))continue;
        carriers[id]=definitionCanServeStrategyRole(world,id,WORKER);
        builders[id]=carriers[id] && world.unitTraits(id).has(UnitRuntimeTraits::Construct);
        if(carriers[id] || builders[id])recipients.push_back(id);
    }
    std::vector<Uint8> candidates(world.catalog->size());
    for(const auto intent:{I::TrainWalk,I::TrainBuild,I::TrainHarvest,I::TrainSwim,I::TrainFly,I::TrainConstruction}) {
        if(intent==I::TrainSwim && !swimming)continue;
        for(const int type:world.capabilities().providers(intent))candidates[type]=1;
    }
    // Resolve each candidate once before the entity census. Rows allocate only
    // for training providers; live units use direct type/recipient indexing.
    for(unsigned type=0;type<candidates.size();++type)if(candidates[type]) {
        auto& masks=result.courseMasks[type];masks.resize(world.unitTypeCount());
        for(const unsigned id:recipients) {
            auto mask=usableTrainingAbilities(world,type,id);
            const bool builder=builders[id];
            const auto& grants=world.catalog->at(type).resolvedType.semantics.training;
            for(int ability=0;ability<NB_ABILITY;++ability)if(mask&(1u<<ability)) {
                const auto intent=trainingIntent(ability);
                const bool trainsAbility=intent!=I::Count && world.capabilities().matches(type,intent,id);
                const bool qualifies=builder && grants[ability].constructionLevel>0
                    && world.capabilities().matches(type,I::TrainConstruction,id);
                if(!trainsAbility && !qualifies)mask&=~(1u<<ability);
                if(qualifies)result.constructionLevels[type]=std::max(result.constructionLevels[type],grants[ability].constructionLevel);
                if(carriers[id] && (qualifies || (trainsAbility && (ability==WALK || ability==BUILD || ability==HARVEST || ability==FLY || (swimming && ability==SWIM)))))
                    result.labourProviders[type]=1;
            }
            masks[id]=mask;
        }
    }
    return result;
}
inline bool needsTraining(const AIEngine::UnitView& unit, const BuildingTrainingSpec& training, int ability)
{
    return training.enabled && unit.canLearn[ability] && training.units.matches(unit.typeNum,training.unitMask)
        && (unit.level[ability]<training.targetLevel
            || ((unit.capabilityFlags&UnitRuntimeTraits::LearnConstruction) && unit.constructionLevel<training.constructionLevel));
}
inline int maxBuildLevel(const AIEngine::AIWorldView& world, unsigned team)
{
    int result=0;
    for(const auto& unit:world.units)
        if(unit.team==int(team) && (unit.capabilityFlags&UnitRuntimeTraits::Construct)
            && matchesStrategyUnitRole(world,unit,WORKER))
            result=std::max(result,int(unit.constructionLevel));
    return result;
}
inline bool buildingProvides(const AIEngine::AIWorldView& world, int type, AIPlanning::BuildingIntent intent)
{
    if(type<0 || std::size_t(type)>=world.catalog->size()) return false;
    const auto& definition=world.catalog->at(type);
    const int completed=definition.site ? definition.next : type;
    return completed>=0 && (world.catalog->at(completed).rawCapabilityMask&(Uint64(1)<<unsigned(intent)));
}

inline bool available(const AIEngine::AIWorldView& world,int type,AIPlanning::BuildingIntent intent,int unit=-1)
{
    const auto& rules=*world.configuration;
    if(!world.capabilities().matches(type,intent,unit)
        || !AIPlanning::BuildingCapabilityIndex::allowed(intent,rules)) return false;
    const auto& definition=world.catalog->at(type).resolvedType;
    if(!definition.requiredExperiment.empty() && !rules.getExperiments().has(definition.requiredExperiment)) return false;
    if(intent==AIPlanning::BuildingIntent::ExchangeResources) {
        const auto& market=definition.semantics.market;
        return market.interTeamFruitExchange || market.suppliesDirectStock
            || (market.suppliesStock && (market.suppliesStockExperiment.empty()
                || rules.getExperiments().has(market.suppliesStockExperiment)));
    }
    return true;
}
inline bool available(const AIEngine::AIWorldView& world,const AIPlanning::BuildingCandidate& candidate,
    AIPlanning::BuildingIntent intent,int unit=-1)
{
    if(candidate.placementType<0 || std::size_t(candidate.placementType)>=world.catalog->size()) return false;
    const auto& placement=world.catalog->at(candidate.placementType).resolvedType;
    return placement.semantics.placeable
        && (placement.isBuildingSite ? placement.nextLevel : candidate.placementType)==candidate.completedType
        && (placement.requiredExperiment.empty() || world.configuration->getExperiments().has(placement.requiredExperiment))
        && available(world,candidate.completedType,intent,unit);
}
inline bool usefulWithoutTraining(const AIEngine::AIWorldView& world,int type)
{
    if(type<0 || std::size_t(type)>=world.catalog->size()) return false;
    const auto& kind=world.catalog->at(type);
    if(kind.site) return false;
    for(unsigned value=0;value<unsigned(AIPlanning::BuildingIntent::Count);++value)
    {
        const auto intent=static_cast<AIPlanning::BuildingIntent>(value);
        if(AIPlanning::BuildingCapabilityIndex::trainingAbility(intent)>=0
            || intent==AIPlanning::BuildingIntent::TrainConstruction) continue;
        if(available(world,type,intent)) return true;
    }
    return false;
}
inline bool usefulBuilding(const AIEngine::AIWorldView& world,int type)
{
    if(type<0 || std::size_t(type)>=world.catalog->size()) return false;
    const auto& placement=world.catalog->at(type);
    if(!placement.available) return false;
    const auto& completed=world.catalog->at(placement.site ? placement.next : type);
    bool recognized=false;
    for(unsigned value=0;value<unsigned(AIPlanning::BuildingIntent::Count);++value)
    {
        const auto intent=static_cast<AIPlanning::BuildingIntent>(value);
        if(!world.capabilities().matches(placement.site ? placement.next : type,intent)) continue;
        recognized=true;
        if(available(world,placement.site ? placement.next : type,intent)) return true;
    }
    return !recognized;
}
inline std::shared_ptr<Order> createOrder(const AIEngine::AIWorldView& world,int team,int x,int y,
    int type,int workers,int futureWorkers)
{
    const auto& placement=world.catalog->at(type);
    const auto& completed=placement.site ? world.catalog->at(placement.next) : placement;
    return std::make_shared<OrderCreate>(team,x,y,type,
        std::clamp(workers,0,placement.semantics.assignmentLimit),
        std::clamp(futureWorkers,0,completed.semantics.assignmentLimit));
}
inline std::shared_ptr<Order> constructionOrder(const AIEngine::AIWorldView& world,const AIEngine::BuildingView& building,
    int workers,int futureWorkers)
{
    const auto& current=world.catalog->at(building.typeNum);
    const bool repair=current.site ? building.constructionResultState==::Building::REPAIR : building.hp<building.maxHp;
    const int target=current.site ? building.typeNum : repair ? current.previous : current.next;
    const auto& placement=target>=0 ? world.catalog->at(target) : current;
    const int origin=current.site ? building.constructionOriginTypeNum : building.typeNum;
    const auto& completed=repair && origin>=0 ? world.catalog->at(origin)
        : placement.site ? world.catalog->at(placement.next) : placement;
    return std::make_shared<OrderConstruction>(building.gid,
        std::clamp(workers,0,placement.semantics.assignmentLimit),
        std::clamp(futureWorkers,0,completed.semantics.assignmentLimit));
}

inline bool permittedQueuedOrder(const AIEngine::AIWorldView& world,Order& order)
{
    const auto& rules=*world.configuration;
    if(order.getOrderType()==ORDER_CREATE)
        return usefulBuilding(world,static_cast<const OrderCreate&>(order).typeNum);
    if(rules.isUnitUpgradesDisabled() && order.getOrderType()==ORDER_CONSTRUCTION)
    {
        const auto gid=static_cast<const OrderConstruction&>(order).gid;
        const auto* building=world.buildingAtSlot(gid);
        if(!building) return false;
        const auto& type=buildingType(world,*building);
        return type.isBuildingSite || (type.semantics.repairable && building->hp<building->maxHp);
    }
    if(rules.isPeacefulModeEnabled() && order.getOrderType()==ORDER_MODIFY_SWARM)
        return static_cast<const OrderModifySwarm&>(order).ratio[WARRIOR]==0;
    if(rules.isPeacefulModeEnabled() && order.getOrderType()==ORDER_MOVE_FLAG)
    {
        const auto gid=static_cast<const OrderMoveFlag&>(order).gid;
        const auto* building=world.buildingAtSlot(gid);
        if(!building) return true;
        const auto& type=buildingType(world,*building);
        return !(type.runtimeAttractionRoles&4) || (type.runtimeAttractionRoles&3);
    }
    return true;
}

inline bool hardBuildingSpace(const AIEngine::AIWorldView& world,int x,int y,int width=1,int height=1,Uint16 ignore=Uint16(-1))
{
    for(int dy=0;dy<height;++dy) for(int dx=0;dx<width;++dx) {
        const auto index=world.tileIndex(x+dx,y+dy);
        const auto resource=world.resourceAt(index).resource;
        const auto occupancy=world.occupancyAt(index);
        if(resource.type!=NO_RES_TYPE || (occupancy.building!=Uint16(-1) && occupancy.building!=ignore)
            || !world.terrainPropertiesAt(index).buildable) return false;
    }
    return true;
}
inline bool roomForBuilding(const AIEngine::AIWorldView& world,int x,int y,const BuildingType& type,int team,bool checkFow=true)
{
    if(!type.semantics.occupiesGround) {
        const int normalizedX=world.normalizeX(x), normalizedY=world.normalizeY(y);
        for(const auto* building:world.buildingSlots(team))
            if(building && !buildingType(world,*building).semantics.occupiesGround
                && building->posX==normalizedX && building->posY==normalizedY) return false;
        return true;
    }
    bool discovered=false;
    for(int dy=0;dy<type.height;++dy) for(int dx=0;dx<type.width;++dx) {
        const auto index=world.tileIndex(x+dx,y+dy);
        const auto resource=world.resourceAt(index).resource;
        const auto occupancy=world.occupancyAt(index);
        if(resource.type!=NO_RES_TYPE || occupancy.building!=Uint16(-1) || occupancy.groundUnit!=Uint16(-1)
            || !world.terrainPropertiesAt(index).buildable) return false;
        discovered|=(world.visibilityAt(index).discovered&world.teams[team].mask)!=0;
    }
    return !checkFow || discovered;
}

inline std::shared_ptr<Order> missingProductionOrder(const AIEngine::AIWorldView& world,int team,
    const std::array<int,NB_UNIT_TYPE>& desired,int workers,int futureWorkers,const std::vector<int>& pendingPlacements)
{
    const auto& index=world.capabilities();
    unsigned required=0,provided=0;
    for(int unit=0;unit<NB_UNIT_TYPE;++unit)
        if(desired[unit]>0 && AIPlanning::BuildingCapabilityIndex::allowed(static_cast<AIPlanning::BuildingIntent>(unit),*world.configuration))
            required|=1u<<unit;
    if(!required) return {};
    const AIEngine::BuildingView* anchor=nullptr;
    auto includeProvider=[&](int type) {
        if(type<0 || std::size_t(type)>=world.catalog->size()) return;
        const auto& descriptor=world.catalog->at(type);
        if(descriptor.site) type=descriptor.next;
        provided|=unsigned(index.intentMask(type))&((1u<<NB_UNIT_TYPE)-1);
    };
    for(const auto* building:world.buildingSlots(team)) {
        if(!building || building->buildingState!=::Building::ALIVE) continue;
        includeProvider(buildingType(world,*building).isBuildingSite ? constructionCompletionType(world,*building) : building->typeNum);
        if(!anchor || (index.intentMask(building->typeNum)&((1u<<NB_UNIT_TYPE)-1))) anchor=building;
        if((provided&required)==required) return {};
    }
    for(int type:pendingPlacements) includeProvider(type);
    if((provided&required)==required || !anchor) return {};
    for(int unit=0;unit<NB_UNIT_TYPE;++unit) {
        if(!(required&(1u<<unit)) || (provided&(1u<<unit))) continue;
        const auto intent=static_cast<AIPlanning::BuildingIntent>(unit);
        for(const auto& candidate:index.placementsByCost(intent)) {
            const auto& type=world.catalog->at(candidate.placementType).resolvedType;
            int eligible=0;
            for(int level=type.semantics.requiredWorkerLevel;level<NB_UNIT_LEVELS;++level)
                eligible+=world.teams[team].statistics.workersByConstructionLevel[level];
            if(!available(world,candidate,intent) || eligible==0) continue;
            for(int radius=1;radius<=32;++radius) for(int dx=-radius;dx<=radius;++dx) for(int dy=-radius;dy<=radius;++dy) {
                if(std::abs(dx)!=radius && std::abs(dy)!=radius) continue;
                const int x=world.normalizeX(anchor->posX+dx),y=world.normalizeY(anchor->posY+dy);
                if(!(world.visibilityAt(world.tileIndex(x,y)).discovered&world.teams[team].allies)
                    || !roomForBuilding(world,x,y,type,team)) continue;
                return createOrder(world,team,x,y,candidate.placementType,workers,futureWorkers);
            }
        }
    }
    return {};
}

}
