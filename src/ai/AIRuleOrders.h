// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AIRules.h"
#include "TeamCapacity.h"
class Game;
#include "Order.h"
#include "Building.h"
#include "ai/observation/AIWorldView.h"
#include <algorithm>
#include <memory>

namespace AIRules
{
std::shared_ptr<Order> createOrder(const Game& game,int team,int x,int y,
    int type,int workers,int futureWorkers);
std::shared_ptr<Order> constructionOrder(const Game& game,const Building& building,
    int workers,int futureWorkers);
// Planning gates prevent new unavailable work. This additional guard drains
// orders restored from older saves, whose controllers did not have those gates.
// It carries no saved state and leaves the standard queue untouched.
bool permittedQueuedOrder(Game& game, Order& order);
// Selection-time qualification uses captured capabilities and entity values.
// It deliberately mirrors the owner guard; no live rule/query fallback exists.
inline bool permittedQueuedOrder(const AIEngine::AIWorldView& world, Order& order)
{
    if(order.getOrderType()==ORDER_CREATE) {
        const int type=static_cast<const OrderCreate&>(order).typeNum;
        if(type<0 || !world.catalog || size_t(type)>=world.catalog->size()) return false;
        const auto& kind=world.catalog->at(type);
        return kind.available && (kind.capabilityMask || !kind.rawCapabilityMask);
    }
    if(world.rules.upgradesDisabled && order.getOrderType()==ORDER_CONSTRUCTION) {
        const auto gid=static_cast<const OrderConstruction&>(order).gid;
        if(gid>=Building::MAX_COUNT*TEAM_MAX_COUNT) return false;
        const auto* building=world.buildingAtSlot(gid);
        return building && (world.catalog->at(building->typeNum).site ||
            (world.catalog->at(building->typeNum).semantics.repairable && building->hp<building->maxHp));
    }
    if(world.rules.peaceful && order.getOrderType()==ORDER_MODIFY_SWARM)
        return static_cast<const OrderModifySwarm&>(order).ratio[WARRIOR]==0;
    if(world.rules.peaceful && order.getOrderType()==ORDER_MOVE_FLAG) {
        const auto gid=static_cast<const OrderMoveFlag&>(order).gid;
        if(gid>=Building::MAX_COUNT*TEAM_MAX_COUNT) return false;
        const auto* building=world.buildingAtSlot(gid);
        if(!building) return true;
        const auto& kind=world.catalog->at(building->typeNum);
        return !kind.resolvedType.attractsRole(2) || kind.resolvedType.attractsRole(0) || kind.resolvedType.attractsRole(1);
    }
    return true;
}

}
