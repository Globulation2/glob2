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
World::World(const AIEngine::AIWorldView& view)
    : source(view),map{view},buildingsTypes{*view.catalog},gameHeader{view.rules},
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
        unitStorage.push_back({unit.type,unit.x,unit.y,unit.hp,unit.hungry,
            unit.activity,unit.medical,unit.constructionLevel,unit.underAttack,unit.levels,unit.performance});
        teams[unit.team]->myUnits[::Unit::GIDtoID(unit.identity.gid)]=&unitStorage.back();
    }
    buildingStorage.reserve(view.buildings.size());
    for(const auto& building:view.buildings)
    {
        WorldBuilding local;
        local.source=&building;local.owner=teams[building.team];local.type=buildingsTypes.get(building.type);
        local.gid=building.identity.gid;local.typeNum=building.type;
        local.posX=building.x;local.posY=building.y;local.buildingState=building.state;
        local.constructionResultState=building.construction;local.hp=building.hp;
        local.maxUnitInside=building.maxInside;local.maxUnitWorking=building.workers;
        local.priority=building.priority;local.unitStayRange=building.range;local.minLevelToFlag=building.minimumLevel;
        local.seenByMask=building.seenBy;local.underAttackTimer=building.underAttack;
        local.resources=building.resources;local.ratio=building.ratios;
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
        for(const auto* building:teams[team]->virtualBuildings)
            if(building->posX==map.normalizeX(x)&&building->posY==map.normalizeY(y))return false;
        return true;
    }
    bool discovered=false;
    for(int dy=0;dy<type->height;++dy)for(int dx=0;dx<type->width;++dx)
    {
        const auto tile=source.tile(x+dx,y+dy);
        if(!map.isHardSpaceForBuilding(x+dx,y+dy)||tile.groundUnit!=0xffff)return false;
        discovered|=map.isMapDiscovered(x+dx,y+dy,teams[team]->me);
    }
    return discovered;
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
        return building && (world.source.catalog->at(building->type).site ||
            (world.source.catalog->at(building->type).semantics.repairable && building->hp<building->maxHp));
    }
    if(world.source.rules.peaceful && order.getOrderType()==ORDER_MODIFY_SWARM)
        return static_cast<const OrderModifySwarm&>(order).ratio[WARRIOR]==0;
    if(world.source.rules.peaceful && order.getOrderType()==ORDER_MOVE_FLAG)
    {
        const auto* building=world.source.buildingAtSlot(static_cast<const OrderMoveFlag&>(order).gid);
        if(!building)return true;
        const auto& kind=world.source.catalog->at(building->type);
        return !kind.zonable[WARRIOR] || kind.zonable[WORKER] || kind.zonable[EXPLORER];
    }
    return true;
}
}
