/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "building_type.h"
#include "Game.h"
#include "Map.h"
#include "team.h"
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
		upgrade[i].clear();
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
			upgrade[ui].remove(building);

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
