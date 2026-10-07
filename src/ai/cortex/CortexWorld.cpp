// SPDX-License-Identifier: GPL-3.0-or-later
#include "CortexWorld.h"
#include "Team.h"

namespace Cortex
{
int WorldTeam::maxBuildLevel() const
{
    int level=0;
    for(const auto* unit:myUnits)if(unit && unit->performance[BUILD])
        level=std::max(level, unit->constructionLevel);
    return level;
}
World::World(const AIEngine::AIWorldView& view, QueryScratch* scratch)
    : source(view),map{view,scratch},buildingsTypes{*view.catalog},gameHeader{view.rules},
      stepCounter(view.tick),totalPrestige(view.totalPrestige),buildProjects(view.buildProjects)
{
    teamStorage.reserve(view.teams.size());
    for(const auto& team:view.teams)
    {
        WorldTeam local;
        local.game=this;local.teamNumber=team.number;local.prestige=team.prestige;
        local.startPosX=team.startX;local.startPosY=team.startY;
        local.me=team.mask;local.allies=team.allies;local.enemies=team.enemies;
        local.isAlive=team.alive;local.stats.source=&team;
        teamStorage.push_back(std::move(local));
    }
    for(auto& team:teamStorage)teams.push_back(&team);
    unitStorage.reserve(view.units.size());
    for(const auto& unit:view.units)
    {
        WorldUnit local{unit.typeNum,unit.posX,unit.posY,unit.hp,unit.hungry,
            unit.activity,unit.medical,unit.constructionLevel,unit.underAttackTimer,{},{}};
        std::copy_n(unit.level, NB_ABILITY, local.level.begin());
        std::copy_n(unit.performance, NB_ABILITY, local.performance.begin());
        unitStorage.push_back(local);
        teams[unit.team]->myUnits[::Unit::GIDtoID(unit.identity.gid)]=&unitStorage.back();
    }
    buildingStorage.reserve(view.buildings.size());
    for(const auto& building:view.buildings)
    {
        WorldBuilding local;
        local.source=&building;local.owner=teams[building.team];local.type=buildingsTypes.get(building.typeNum);
        local.gid=building.identity.gid;local.typeNum=building.typeNum;
        local.posX=building.posX;local.posY=building.posY;local.buildingState=building.buildingState;
        local.constructionResultState=building.constructionResultState;local.hp=building.hp;
        local.maxUnitInside=building.maxUnitInside;local.maxUnitWorking=building.maxUnitWorking;
        local.priority=building.priority;local.unitStayRange=building.unitStayRange;local.minLevelToFlag=building.minLevelToFlag;
        local.seenByMask=building.seenByMask;local.underAttackTimer=building.underAttackTimer;
        std::copy_n(view.buildingResources(building).data(),MAX_NB_RESOURCES,local.resources.begin());
        std::copy_n(building.ratio,NB_UNIT_TYPE,local.ratio.begin());
        auto resolve=[&](UnitRef ref)->WorldUnit* {
            const auto* unit=view.unit(ref);
            return unit ? teams[unit->team]->myUnits[::Unit::GIDtoID(ref.gid)] : nullptr;
        };
        for(const auto ref:view.occupants(building))if(auto* unit=resolve(ref))local.unitsInside.push_back(unit);
        for(const auto ref:view.workers(building))if(auto* unit=resolve(ref))local.unitsWorking.push_back(unit);
        buildingStorage.push_back(std::move(local));
        teams[building.team]->myBuildings[::Building::GIDtoID(building.identity.gid)]=&buildingStorage.back();
    }
    for(const auto& team:view.teams)
        for(const auto ref:team.virtualBuildings)
            if(const auto* building=view.building(ref))
                teams[team.number]->virtualBuildings.push_back(teams[team.number]->myBuildings[::Building::GIDtoID(ref.gid)]);
}
bool World::checkRoomForBuilding(int x,int y,const BuildingType* type,int team) const
{
    if(type->isVirtual)
    {
        const auto nx=map.normalizeX(x),ny=map.normalizeY(y);
        for(const auto* building:teams[team]->virtualBuildings)
            if(building->posX==nx&&building->posY==ny)return false;
        return true;
    }
    for(int dy=0;dy<type->height;++dy)for(int dx=0;dx<type->width;++dx)
        if(!map.isHardSpaceForBuildingAt(source.tileIndex(x+dx,y+dy),0xffff,true))return false;
    // Discovery cannot bypass any legality check. Once the whole footprint is
    // legal, the first discovered tile satisfies the original placement rule.
    for(int dy=0;dy<type->height;++dy)for(int dx=0;dx<type->width;++dx)
        if(source.visibilityAt(source.tileIndex(x+dx,y+dy)).discovered&teams[team]->me)return true;
    return false;
}
bool permittedQueuedOrder(const World& world, Order& order)
{
    if(order.getOrderType()==ORDER_CREATE)
    {
        const int type=static_cast<const OrderCreate&>(order).typeNum;
        if(type<0 || type>=int(world.source.catalog->size()))return false;
        const auto& kind=world.source.catalog->at(type);
        return kind.available && (kind.capabilityMask || !kind.rawCapabilityMask);
    }
    if(world.source.rules.upgradesDisabled && order.getOrderType()==ORDER_CONSTRUCTION)
    {
        const auto* building=world.source.buildingAtSlot(static_cast<const OrderConstruction&>(order).gid);
        return building && (world.source.catalog->at(building->typeNum).site ||
            (world.source.catalog->at(building->typeNum).semantics.repairable && building->hp<building->maxHp));
    }
    if(world.source.rules.peaceful && order.getOrderType()==ORDER_MODIFY_SWARM)
        return static_cast<const OrderModifySwarm&>(order).ratio[WARRIOR]==0;
    if(world.source.rules.peaceful && order.getOrderType()==ORDER_MOVE_FLAG)
    {
        const auto* building=world.source.buildingAtSlot(static_cast<const OrderMoveFlag&>(order).gid);
        if(!building)return true;
        const auto& kind=world.source.catalog->at(building->typeNum);
        return !kind.zonable[WARRIOR] || kind.zonable[WORKER] || kind.zonable[EXPLORER];
    }
    return true;
}
}
