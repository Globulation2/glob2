// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Building.h"
#include "BuildingType.h"
using std::shared_ptr;
void GameGUI::requestBuildingConstruction(const SceneBuildingPanel &building)
{
	if (globalContainer->isViewingGame() || building.owner().number != localTeamNo)
		return;
	if (building.state().constructionResultState==Building::REPAIR || building.state().constructionResultState==Building::UPGRADE)
	{
		const int workers=defaultAssign.getDefaultAssignedUnits(drawnScene(), building.state().constructionOriginTypeNum);
		enqueueOrder(std::make_shared<OrderCancelConstruction>(building.state().gid,workers));
	}
	else if ((building.state().constructionResultState == Building::NO_CONSTRUCTION) &&
			 (building.state().buildingState == Building::ALIVE))
	{
		repairAndUpgradeBuilding(&building, true, true);
	}
}
void GameGUI::requestBuildingDestruction(const SceneBuildingPanel &building)
{
	if (globalContainer->isViewingGame() || building.owner().number != localTeamNo)
		return;
	if (building.state().buildingState == Building::WAITING_FOR_DESTRUCTION)
	{
		enqueueOrder(shared_ptr<Order>(new OrderCancelDelete(building.state().gid)));
	}
	else if (building.state().buildingState == Building::ALIVE)
	{
		enqueueOrder(shared_ptr<Order>(new OrderDelete(building.state().gid)));
	}
}
