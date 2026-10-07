// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/observation/AIWorldView.h"
#include "BuildingCapabilities.h"
#include "Building.h"
#include "Unit.h"
#include "Order.h"
#include <algorithm>

// These functions operate directly on canonical captured records. They retain
// no entity storage, relationships or observation lease.
namespace AIEngine::ObservationQueries
{
inline const TerrainProperties& terrain(const AIEngine::AIWorldView& world,std::size_t index)
{return world.terrain->properties(world.terrainAt(index).type);}
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
    const auto t=world.terrainAt(index);const auto& r=world.resourceAt(index);
    const auto& o=world.occupancyAt(index);const auto& a=world.areasAt(index);const auto v=world.visibilityAt(index);
    AIEngine::TileView result;
    result.terrain=t.type;result.legacyTerrain=t.legacy;
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
    return (unit.performance[ATTACK_STRENGTH]+unit.experienceLevel)*world.configuration->getGlassCannonScale();
}
inline bool needsTraining(const AIEngine::UnitView& unit, const BuildingTrainingSpec& training, int ability)
{
    return training.enabled && unit.canLearn[ability] && (training.unitMask&(1u<<unit.typeNum))
        && (unit.level[ability]<training.targetLevel
            || (unit.typeNum==WORKER && unit.constructionLevel<training.constructionLevel));
}
inline int maxBuildLevel(const AIEngine::AIWorldView& world, unsigned team)
{
    int result=0;
    for(const auto& unit:world.units)
        if(unit.team==int(team) && unit.performance[BUILD]!=0)
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
        return !type.zonable[WARRIOR] || type.zonable[WORKER] || type.zonable[EXPLORER];
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
            || !world.terrain->properties(world.terrainAt(index).type).buildable) return false;
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
            || !world.terrain->properties(world.terrainAt(index).type).buildable) return false;
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
