// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BuildingType.h"
#include "Map.h"
#include "Team.h"
#include "Unit.h"

void Team::createLists(void)
{
	assert(swarms.size()==0);
	assert(turrets.size()==0);
	assert(virtualBuildings.size()==0);

	swarms.clear();
	turrets.clear();
	virtualBuildings.clear();

	for (int i=0; i<Building::MAX_COUNT; i++)
		if (myBuildings[i])
	{
		if (myBuildings[i]->type->unitProductionTime)
			swarms.push_back(myBuildings[i]);
		if (myBuildings[i]->type->shootingRange)
			turrets.push_back(myBuildings[i]);
		if (myBuildings[i]->type->isVirtual)
			virtualBuildings.push_back(myBuildings[i]);
		if (myBuildings[i]->type->zonable[WORKER])
			clearingFlags.push_back(myBuildings[i]);
		myBuildings[i]->update();
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
	swarms.clear();
	turrets.clear();
	virtualBuildings.clear();
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
	for (int ui=0; ui<NB_ABILITY; ui++)
		if (building->type->upgrade[ui])
			canUpgrade[ui].remove(building);

	if (building->type->canFeedUnit)
		canFeedUnit.remove(building);
	if (building->type->canHealUnit)
		canHealUnit.remove(building);
	if (building->type->canExchange)
		canExchange.remove(building);

	if (building->type->unitProductionTime)
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
	if (building->type->canExchange)
		canExchange.push_back(building);

	if (building->type->unitProductionTime)
		swarms.push_back(building);

	if (building->type->shootingRange)
		turrets.push_back(building);

	if (building->type->zonable[WORKER])
		clearingFlags.push_back(building);
;
	if (building->type->isVirtual)
		virtualBuildings.push_back(building);
}
