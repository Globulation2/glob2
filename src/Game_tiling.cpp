// SPDX-License-Identifier: GPL-3.0-or-later
// Repeating a map at game setup: see MapTiling.h.
#include "Game.h"
#include "Building.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include "MapTiling.h"
#include "Team.h"
#include "Unit.h"
#include "UnitType.h"

#include <vector>

namespace
{
	struct BuildingTemplate
	{
		int x, y, typeNum, hp, maxUnitWorking, maxUnitWorkingFuture, priority, unitStayRange;
		Sint32 ressources[MAX_NB_RESSOURCES];
		Sint32 ratio[NB_UNIT_TYPE];
	};

	struct UnitTemplate
	{
		int x, y, typeNum, hp;
		Sint32 level[NB_ABILITY];
	};

	struct ColonyTemplate
	{
		std::vector<BuildingTemplate> buildings;
		std::vector<UnitTemplate> units;
	};
}

bool Game::tileForPlay(int rx, int ry, int teamCount, int coloniesPerTeam)
{
	const int mapTeams = mapHeader.getNumberOfTeams();
	const int total = MapTiling::colonyCount(mapTeams, rx, ry);
	if (teamCount < 1)
		teamCount = std::min<int>(Team::MAX_COUNT, total);
	if (mapTeams < 1 || rx < 1 || ry < 1 || teamCount > Team::MAX_COUNT || teamCount > total)
		return false;
	if (map.getW() * rx > MapTiling::MAX_MAP_SIDE || map.getH() * ry > MapTiling::MAX_MAP_SIDE)
		return false;
	const MapHeader tiledHeader = MapTiling::tiledHeader(mapHeader, rx, ry, teamCount);

	// every colony of the map as it was loaded
	std::vector<ColonyTemplate> colonies(mapTeams);
	for (int t = 0; t < mapTeams; t++)
	{
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			const Building* b = teams[t]->myBuildings[i];
			if (!b || b->buildingState != Building::ALIVE)
				continue;
			BuildingTemplate bt = { b->posX, b->posY, b->typeNum, b->hp, b->maxUnitWorking, b->getMaxUnitWorkingFuture(), b->priority, b->unitStayRange, {}, {} };
			for (int r = 0; r < MAX_NB_RESSOURCES; r++)
				bt.ressources[r] = b->ressources[r];
			for (int u = 0; u < NB_UNIT_TYPE; u++)
				bt.ratio[u] = b->ratio[u];
			colonies[t].buildings.push_back(bt);
		}
		for (int i = 0; i < Unit::MAX_COUNT; i++)
		{
			const Unit* u = teams[t]->myUnits[i];
			if (!u || u->isDead)
				continue;
			// units inside a building are not on the map and are not carried over
			if (map.getGroundUnit(u->posX, u->posY) != u->gid && map.getAirUnit(u->posX, u->posY) != u->gid)
				continue;
			UnitTemplate ut = { u->posX, u->posY, u->typeNum, u->hp, {} };
			for (int a = 0; a < NB_ABILITY; a++)
				ut.level[a] = u->level[a];
			colonies[t].units.push_back(ut);
		}
	}

	const int w0 = map.getW(), h0 = map.getH();
	while (mapHeader.getNumberOfTeams() > 0)
		removeTeam(TEAM_POS_END);
	map.tile(rx, ry);
	map.setGame(this);
	for (int k = 0; k < teamCount; k++)
	{
		addTeam(TEAM_POS_END);
		teams[k]->setBaseTeam(&tiledHeader.getBaseTeam(k));
		mapHeader.getBaseTeam(k) = tiledHeader.getBaseTeam(k);
	}

	int n = 0;
	for (int j = 0; j < ry; j++)
		for (int i = 0; i < rx; i++)
			for (int t = 0; t < mapTeams; t++, n++)
			{
				const int k = MapTiling::teamForColony(n, total, teamCount, coloniesPerTeam);
				if (k < 0)
					continue;
				const int dx = i * w0, dy = j * h0;
				for (const BuildingTemplate& bt : colonies[t].buildings)
				{
					Building* b = addBuilding(bt.x + dx, bt.y + dy, bt.typeNum, k, bt.maxUnitWorking, bt.maxUnitWorkingFuture);
					if (!b)
						continue;
					b->hp = bt.hp;
					b->priority = bt.priority;
					b->unitStayRange = bt.unitStayRange;
					for (int r = 0; r < MAX_NB_RESSOURCES; r++)
						b->ressources[r] = bt.ressources[r];
					for (int u = 0; u < NB_UNIT_TYPE; u++)
						b->ratio[u] = bt.ratio[u];
					if (!teams[k]->startPosSet && b->type->unitProductionTime)
					{
						teams[k]->startPosX = b->posX;
						teams[k]->startPosY = b->posY;
						teams[k]->startPosSet = 1;
					}
				}
				for (const UnitTemplate& ut : colonies[t].units)
				{
					Unit* u = addUnit(ut.x + dx, ut.y + dy, k, ut.typeNum, ut.level[WALK], 0, 0, 0);
					if (!u)
						continue;
					for (int a = 0; a < NB_ABILITY; a++)
					{
						u->level[a] = ut.level[a];
						u->performance[a] = teams[k]->race.getUnitType(ut.typeNum, ut.level[a])->performance[a];
					}
					u->hp = ut.hp;
				}
			}

	for (int k = 0; k < teamCount; k++)
	{
		// addBuilding already listed the virtual buildings; createLists insists on filling that list itself
		teams[k]->virtualBuildings.clear();
		teams[k]->createLists();
	}
	regenerateDiscoveryMap();
	return true;
}
