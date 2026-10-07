// SPDX-License-Identifier: GPL-3.0-or-later
#include "CortexSnapshotQueries.h"
namespace Cortex {
int maxBuildLevel(const AIEngine::AIWorldView& world,const AIEngine::TeamView& team)
{
 int level=0;for(const auto* unit:world.unitSlots(team.number))if(unit&&unit->performance[BUILD])level=std::max(level,unit->constructionLevel);return level;
}
int finishedType(const AIEngine::AIWorldView& world,const std::string& key)
{
 for(std::size_t i=0;i<world.catalog->size();++i)if(world.catalog->at(i).key==key)return world.catalog->at(i).site?world.catalog->at(i).next:int(i);return -1;
}
bool checkRoomForBuilding(const AIEngine::AIWorldView& world,int x,int y,const BuildingType* type,int team,const PlanningIntent& intents)
{
 if(type->isVirtual){const auto nx=world.normalizeX(x),ny=world.normalizeY(y);for(const auto ref:world.teams[team].virtualBuildings)if(const auto* b=world.building(ref))if(plannedX(intents,*b)==nx&&plannedY(intents,*b)==ny)return false;return true;}
 for(int dy=0;dy<type->height;++dy)for(int dx=0;dx<type->width;++dx)if(!isHardSpaceForBuildingAt(world,world.tileIndex(x+dx,y+dy),0xffff,true))return false;
 for(int dy=0;dy<type->height;++dy)for(int dx=0;dx<type->width;++dx)if(world.visibilityAt(world.tileIndex(x+dx,y+dy)).discovered&world.teams[team].mask)return true;
 return false;
}
bool permittedQueuedOrder(const AIEngine::AIWorldView& world, Order& order)
{
    if(order.getOrderType()==ORDER_CREATE)
    {
        const int type=static_cast<const OrderCreate&>(order).typeNum;
        if(type<0 || type>=int(world.catalog->size()))return false;
        const auto& kind=world.catalog->at(type);
        return kind.available && (kind.capabilityMask || !kind.rawCapabilityMask);
    }
    if(world.rules.upgradesDisabled && order.getOrderType()==ORDER_CONSTRUCTION)
    {
        const auto* building=world.buildingAtSlot(static_cast<const OrderConstruction&>(order).gid);
        return building && (world.catalog->at(building->typeNum).site ||
            (world.catalog->at(building->typeNum).semantics.repairable && building->hp<building->maxHp));
    }
    if(world.rules.peaceful && order.getOrderType()==ORDER_MODIFY_SWARM)
        return static_cast<const OrderModifySwarm&>(order).ratio[WARRIOR]==0;
    if(world.rules.peaceful && order.getOrderType()==ORDER_MOVE_FLAG)
    {
        const auto* building=world.buildingAtSlot(static_cast<const OrderMoveFlag&>(order).gid);
        if(!building)return true;
        const auto& kind=world.catalog->at(building->typeNum);
        return !kind.zonable[WARRIOR] || kind.zonable[WORKER] || kind.zonable[EXPLORER];
    }
    return true;
}
}
