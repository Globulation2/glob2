// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Building.h"
#include "BuildingType.h"
using std::shared_ptr;
void GameGUI::requestBuildingConstruction(Building& building)
{
    if (globalContainer->isViewingGame() || building.owner->teamNumber!=localTeamNo) return;
		if (building.constructionResultState==Building::REPAIR)
		{
			int typeNum = building.typeNum; //determines type of updated building
			int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
			orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(building.gid, unitWorking)));
		}
		else if (building.constructionResultState==Building::UPGRADE)
		{
			int typeNum = building.typeNum; //determines type of updated building
			int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum - 1);
			orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(building.gid, unitWorking)));
		}
		else if ((building.constructionResultState==Building::NO_CONSTRUCTION) && (building.buildingState==Building::ALIVE))
		{
			repairAndUpgradeBuilding(&building, true, true);
		}
}
void GameGUI::requestBuildingDestruction(Building& building)
{
    if (globalContainer->isViewingGame() || building.owner->teamNumber!=localTeamNo) return;
		if (building.buildingState==Building::WAITING_FOR_DESTRUCTION)
		{
			orderQueue.push_back(shared_ptr<Order>(new OrderCancelDelete(building.gid)));
		}
		else if (building.buildingState==Building::ALIVE)
		{
			orderQueue.push_back(shared_ptr<Order>(new OrderDelete(building.gid)));
		}
}
