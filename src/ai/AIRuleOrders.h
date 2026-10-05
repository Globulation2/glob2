// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AIRules.h"
#include "Game.h"
#include "Order.h"
#include "Building.h"
#include <algorithm>
#include <memory>

namespace AIRules
{
inline std::shared_ptr<Order> createOrder(const Game& game,int team,int x,int y,
    int type,int workers,int futureWorkers)
{
    const auto* placement=game.buildingsTypes.get(type);
    const auto* completed=placement->isBuildingSite ? game.buildingsTypes.get(placement->nextLevel) : placement;
    return std::make_shared<OrderCreate>(team,x,y,type,
        std::clamp(workers,0,placement->semantics.assignmentLimit),
        std::clamp(futureWorkers,0,completed->semantics.assignmentLimit));
}

inline std::shared_ptr<Order> constructionOrder(const Game& game,const Building& building,
    int workers,int futureWorkers)
{
    const int target=building.hp<building.getEffectiveMaxHp() ? building.type->prevLevel : building.type->nextLevel;
    const auto* placement=target>=0 ? game.buildingsTypes.get(target) : building.type;
    const auto* completed=placement->isBuildingSite ? game.buildingsTypes.get(placement->nextLevel) : placement;
    return std::make_shared<OrderConstruction>(building.gid,
        std::clamp(workers,0,placement->semantics.assignmentLimit),
        std::clamp(futureWorkers,0,completed->semantics.assignmentLimit));
}

// Planning gates prevent new unavailable work. This additional guard drains
// orders restored from older saves, whose controllers did not have those gates.
// It carries no saved state and leaves the standard queue untouched.
inline bool permittedQueuedOrder(Game& game, Order& order)
{
    const auto& rules=game.gameHeader;
    if(order.getOrderType()==ORDER_CREATE)
    {
        const auto& create=static_cast<const OrderCreate&>(order);
        return usefulBuilding(game,create.typeNum);
    }
    if(rules.isUnitUpgradesDisabled() && order.getOrderType()==ORDER_CONSTRUCTION)
    {
        const auto gid=static_cast<const OrderConstruction&>(order).gid;
        if(gid>=Building::MAX_COUNT*Team::MAX_COUNT) return false;
        auto* team=game.teams[Building::GIDtoTeam(gid)];
        auto* building=team ? team->myBuildings[Building::GIDtoID(gid)] : nullptr;
        return building && (building->type->isBuildingSite
            || (building->type->semantics.repairable && building->hp<building->getEffectiveMaxHp()));
    }
    if(rules.isPeacefulModeEnabled() && order.getOrderType()==ORDER_MODIFY_SWARM)
        return static_cast<const OrderModifySwarm&>(order).ratio[WARRIOR]==0;
    if(rules.isPeacefulModeEnabled() && order.getOrderType()==ORDER_MOVE_FLAG)
    {
        const auto gid=static_cast<const OrderMoveFlag&>(order).gid;
        if(gid>=Building::MAX_COUNT*Team::MAX_COUNT) return false;
        auto* team=game.teams[Building::GIDtoTeam(gid)];
        auto* building=team ? team->myBuildings[Building::GIDtoID(gid)] : nullptr;
        return !building || !building->type->zonable[WARRIOR]
            || building->type->zonable[WORKER] || building->type->zonable[EXPLORER];
    }
    return true;
}
}
