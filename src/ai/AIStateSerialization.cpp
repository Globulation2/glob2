// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIStateSerialization.h"
#include "Building.h"
#include "Game.h"
#include "Team.h"

namespace AIStateSerialization
{
void normalizeLegacyOrderStaffing(Game& game, Order& order, Sint32 versionMinor)
{
    if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG) return;
    const BuildingType* placement = nullptr;
    if (order.getOrderType() == ORDER_CREATE)
    {
        auto& create = static_cast<OrderCreate&>(order);
        if (create.typeNum < 0 || std::size_t(create.typeNum) >= game.buildingsTypes.size()
            || create.unitWorking < 0 || create.unitWorking > MAX_BUILDING_WORKER_REQUEST
            || create.unitWorkingFuture < 0 || create.unitWorkingFuture > MAX_BUILDING_WORKER_REQUEST) return;
        placement = game.buildingsTypes.get(create.typeNum);
        const auto* completed = placement->isBuildingSite && placement->nextLevel >= 0
            ? game.buildingsTypes.get(placement->nextLevel) : placement;
        create.unitWorking = std::min(create.unitWorking, placement->semantics.assignmentLimit);
        create.unitWorkingFuture = std::min(create.unitWorkingFuture, completed->semantics.assignmentLimit);
    }
    else if (order.getOrderType() == ORDER_CONSTRUCTION)
    {
        auto& construction = static_cast<OrderConstruction&>(order);
        if (construction.gid >= Building::MAX_COUNT * Team::MAX_COUNT
            || construction.unitWorking > MAX_BUILDING_WORKER_REQUEST || construction.unitWorkingFuture > MAX_BUILDING_WORKER_REQUEST) return;
        const auto* team = game.teams[Building::GIDtoTeam(construction.gid)];
        const auto* building = team ? team->myBuildings[Building::GIDtoID(construction.gid)] : nullptr;
        if (!building) return;
        const bool repair = building->type->isBuildingSite ? building->constructionResultState==Building::REPAIR
            : building->hp < building->getEffectiveMaxHp();
        const int target = building->type->isBuildingSite ? building->typeNum
            : repair ? building->type->prevLevel : building->type->nextLevel;
        if (target < 0 || std::size_t(target) >= game.buildingsTypes.size()) return;
        placement = game.buildingsTypes.get(target);
        const int origin = building->type->isBuildingSite ? building->getConstructionOriginTypeNum() : building->typeNum;
        const auto* completed = repair && origin >= 0 && std::size_t(origin)<game.buildingsTypes.size()
            ? game.buildingsTypes.get(origin) : placement->isBuildingSite && placement->nextLevel >= 0
            ? game.buildingsTypes.get(placement->nextLevel) : placement;
        construction.unitWorking = std::min(construction.unitWorking, Uint32(placement->semantics.assignmentLimit));
        construction.unitWorkingFuture = std::min(construction.unitWorkingFuture, Uint32(completed->semantics.assignmentLimit));
    }
}
}
