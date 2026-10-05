// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BuildingType.h"
#include "Map.h"
#include "Team.h"
#include "Unit.h"
#include <algorithm>

void Team::createLists(void)
{
	// Setup helpers can register buildings before requesting a complete rebuild.
	// Rebuild only static capability lists; active service and staffing queues
	// retain their scheduling state.
	stockSuppliers.clear();
	directStockSuppliers.clear();
	combatFlags.clear();
	swarms.clear();
	turrets.clear();
	virtualBuildings.clear();
	clearingFlags.clear();
	canExchange.clear();
	for (int i=0; i<Building::MAX_COUNT; ++i)
		if (Building* building=myBuildings[i])
		{
			addToStaticAbilitiesLists(building);
			building->update();
		}
}


void Team::clearLists(void)
{
	for (int i=0; i<NB_ABILITY; i++)
		canUpgrade[i].clear();
	canFeedUnit.clear();
	canHealUnit.clear();
	canExchange.clear();
	buildingsWaitingForDestruction.clear();
	buildingsToBeDestroyed.clear();
	buildingsTryToBuildingSiteRoom.clear();
	buildingsNeedingUnits.clear();
	stockSuppliers.clear();
	directStockSuppliers.clear();
	combatFlags.clear();
	swarms.clear();
	turrets.clear();
	virtualBuildings.clear();
	// clearMem deletes every building next; a clearing flag left listed here would
	// dangle (and be stepped and saved) for the rest of the game.
	clearingFlags.clear();
}




void Team::clearMap(void)
{
	assert(map);

	for (int i=0; i<Unit::MAX_COUNT; ++i)
	{
		if (myUnits[i])
		{
			if (myUnits[i]->performance[FLY])
			{
				map->setAirUnit(myUnits[i]->posX, myUnits[i]->posY, NOGUID);
			}
			else
			{
				map->setGroundUnit(myUnits[i]->posX, myUnits[i]->posY, NOGUID);
			}
		}
	}

	for (int i=0; i<Building::MAX_COUNT; ++i)
	{
		if (myBuildings[i])
		{
			if (!myBuildings[i]->type->isVirtual)
			{
				map->setBuilding(myBuildings[i]->posX, myBuildings[i]->posY, myBuildings[i]->type->width, myBuildings[i]->type->height, NOGBID);
			}
		}
	}

}




void Team::clearMem(void)
{
	for (int i=0; i<Unit::MAX_COUNT; ++i)
	{
		if (myUnits[i])
		{
			delete myUnits[i];
			myUnits[i] = NULL;
		}
	}
	for (int i=0; i<Building::MAX_COUNT; ++i)
	{
		if (myBuildings[i])
		{
			delete myBuildings[i];
			myBuildings[i] = NULL;
		}
	}
}




void Team::removeFromAbilitiesLists(Building *building)
{
	if (building->type->runtimeSuppliesStock) map->invalidateSupplierLocations();
	building->cancelProduction();
	stockSuppliers.remove(building);
	directStockSuppliers.remove(building);
	combatFlags.remove(building);
	for (int ui=0; ui<NB_ABILITY; ui++)
		if (building->type->upgrade[ui])
			canUpgrade[ui].remove(building);

	if (building->type->canFeedUnit)
		canFeedUnit.remove(building);
	if (building->type->canHealUnit)
		canHealUnit.remove(building);
	if (building->type->canExchange)
		canExchange.remove(building);

	if (building->type->semantics.production.enabledUnitMask)
		swarms.remove(building);
	if (building->type->shootingRange)
		turrets.remove(building);

	if (building->type->zonable[WORKER])
		clearingFlags.remove(building);

	if (building->type->isVirtual)
		virtualBuildings.remove(building);
}




void Team::addToStaticAbilitiesLists(Building *building)
{
	if (building->type->runtimeSuppliesDirectStock && std::find(directStockSuppliers.begin(),directStockSuppliers.end(),building)==directStockSuppliers.end()) directStockSuppliers.push_back(building);
	if (building->type->zonable[WARRIOR] && std::find(combatFlags.begin(),combatFlags.end(),building)==combatFlags.end()) combatFlags.push_back(building);
	if (building->type->runtimeSuppliesStock) map->invalidateSupplierLocations();
	if (building->type->runtimeSuppliesStock && building->buildingState == Building::ALIVE &&
		std::find(stockSuppliers.begin(), stockSuppliers.end(), building) == stockSuppliers.end())
		stockSuppliers.push_back(building);
	if (building->type->canExchange)
		if (std::find(canExchange.begin(), canExchange.end(), building) == canExchange.end()) canExchange.push_back(building);

	if (building->type->semantics.production.enabledUnitMask)
		if (std::find(swarms.begin(), swarms.end(), building) == swarms.end()) swarms.push_back(building);

	if (building->type->shootingRange)
		if (std::find(turrets.begin(), turrets.end(), building) == turrets.end()) turrets.push_back(building);

	if (building->type->zonable[WORKER])
		if (std::find(clearingFlags.begin(), clearingFlags.end(), building) == clearingFlags.end()) clearingFlags.push_back(building);
;
	if (building->type->isVirtual)
		if (std::find(virtualBuildings.begin(), virtualBuildings.end(), building) == virtualBuildings.end()) virtualBuildings.push_back(building);
}
