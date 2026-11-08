// SPDX-License-Identifier: GPL-3.0-or-later
// Live-game forms of the AI rule helpers. They read the Game, so they live here
// rather than in AIRules.h / AIRuleOrders.h, which most AI code includes.
#include "AIRuleOrders.h"
#include "Game.h"
#include "Team.h"

namespace AIRules
{
bool usefulWithoutTraining(const Game& game,int type)
{
    const auto& index=game.buildingCapabilities();
    for(unsigned value=0;value<static_cast<unsigned>(AIPlanning::BuildingIntent::Count);++value)
    {
        const auto intent=static_cast<AIPlanning::BuildingIntent>(value);
        if(AIPlanning::BuildingCapabilityIndex::trainingAbility(intent)>=0
            || intent==AIPlanning::BuildingIntent::TrainConstruction) continue;
        if(index.available(type,intent,game.gameHeader)) return true;
    }
    return false;
}
bool usefulBuilding(const Game& game,int type)
{
    if(type<0 || size_t(type)>=game.buildingsTypes.size()) return false;
    const auto* placement=game.buildingsTypes.get(type);
    if(!placement->runtimeAvailable) return false;
    const int completed=placement->isBuildingSite ? placement->nextLevel : type;
    const auto& index=game.buildingCapabilities();
    bool recognized=false;
    for(unsigned value=0;value<static_cast<unsigned>(AIPlanning::BuildingIntent::Count);++value)
    {
        const auto intent=static_cast<AIPlanning::BuildingIntent>(value);
        if(!index.matches(completed,intent)) continue;
        recognized=true;
        if(index.available(completed,intent,game.gameHeader)) return true;
    }
    return !recognized;
}
std::shared_ptr<Order> createOrder(const Game& game,int team,int x,int y,
    int type,int workers,int futureWorkers)
{
    const auto* placement=game.buildingsTypes.get(type);
    const auto* completed=placement->isBuildingSite ? game.buildingsTypes.get(placement->nextLevel) : placement;
    return std::make_shared<OrderCreate>(team,x,y,type,
        std::clamp(workers,0,placement->semantics.assignmentLimit),
        std::clamp(futureWorkers,0,completed->semantics.assignmentLimit));
}

std::shared_ptr<Order> constructionOrder(const Game& game,const Building& building,
    int workers,int futureWorkers)
{
    const bool repair=building.type->isBuildingSite ? building.constructionResultState==Building::REPAIR
        : building.hp<building.getEffectiveMaxHp();
    const int target=building.type->isBuildingSite ? building.typeNum
        : repair ? building.type->prevLevel : building.type->nextLevel;
    const auto* placement=target>=0 ? game.buildingsTypes.get(target) : building.type;
    // Repair restores the origin even when its site is shared with another
    // lineage whose normal forward completion is a different variant.
    const int origin=building.type->isBuildingSite ? building.getConstructionOriginTypeNum() : building.typeNum;
    const auto* completed=repair && origin>=0 ? game.buildingsTypes.get(origin)
        : placement->isBuildingSite ? game.buildingsTypes.get(placement->nextLevel) : placement;
    return std::make_shared<OrderConstruction>(building.gid,
        std::clamp(workers,0,placement->semantics.assignmentLimit),
        std::clamp(futureWorkers,0,completed->semantics.assignmentLimit));
}

bool permittedQueuedOrder(Game& game, Order& order)
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
        return !building || !building->runtime->attractsRole(2)
            || building->runtime->attractsRole(0) || building->runtime->attractsRole(1);
    }
    return true;
}
}
