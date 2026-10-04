// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AIRules.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Building.h"

namespace AIRules
{
// Planning gates prevent new unavailable work. This additional guard drains
// orders restored from older saves, whose controllers did not have those gates.
// It carries no saved state and leaves the standard queue untouched.
inline bool permittedQueuedOrder(Game& game, Order& order)
{
    const auto& rules=game.gameHeader;
    if(order.getOrderType()==ORDER_CREATE)
    {
        const auto& create=static_cast<const OrderCreate&>(order);
        const auto* type=globalContainer->buildingsTypes.get(create.typeNum);
        return usefulBuilding(rules,type->shortTypeNum);
    }
    if(rules.isUnitUpgradesDisabled() && order.getOrderType()==ORDER_CONSTRUCTION)
    {
        const auto gid=static_cast<const OrderConstruction&>(order).gid;
        auto* team=game.teams[Building::GIDtoTeam(gid)];
        auto* building=team ? team->myBuildings[Building::GIDtoID(gid)] : nullptr;
        return building && (building->type->isBuildingSite || building->hp<building->getEffectiveMaxHp());
    }
    if(rules.isPeacefulModeEnabled() && order.getOrderType()==ORDER_MODIFY_SWARM)
        return static_cast<const OrderModifySwarm&>(order).ratio[WARRIOR]==0;
    if(rules.isPeacefulModeEnabled() && order.getOrderType()==ORDER_MOVE_FLAG)
    {
        const auto gid=static_cast<const OrderMoveFlag&>(order).gid;
        auto* team=game.teams[Building::GIDtoTeam(gid)];
        auto* building=team ? team->myBuildings[Building::GIDtoID(gid)] : nullptr;
        return !building || building->type->shortTypeNum!=IntBuildingType::WAR_FLAG;
    }
    return true;
}
}
