// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Building.h"
#include "BuildingType.h"
using std::shared_ptr;
void GameGUI::requestBuildingConstruction(Building &building)
{
	if (globalContainer->isViewingGame() || building.owner->teamNumber != localTeamNo)
		return;
	if (building.constructionResultState==Building::REPAIR || building.constructionResultState==Building::UPGRADE)
	{
		const int workers=defaultAssign.getDefaultAssignedUnits(building.getConstructionOriginTypeNum());
		orderQueue.push_back(std::make_shared<OrderCancelConstruction>(building.gid,workers));
	}
	else if ((building.constructionResultState == Building::NO_CONSTRUCTION) &&
			 (building.buildingState == Building::ALIVE))
	{
		repairAndUpgradeBuilding(&building, true, true);
	}
}
void GameGUI::requestBuildingDestruction(Building &building)
{
	if (globalContainer->isViewingGame() || building.owner->teamNumber != localTeamNo)
		return;
	if (building.buildingState == Building::WAITING_FOR_DESTRUCTION)
	{
		orderQueue.push_back(shared_ptr<Order>(new OrderCancelDelete(building.gid)));
	}
	else if (building.buildingState == Building::ALIVE)
	{
		orderQueue.push_back(shared_ptr<Order>(new OrderDelete(building.gid)));
	}
}
