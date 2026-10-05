// SPDX-License-Identifier: GPL-3.0-or-later
// Repeating a map at game setup: see MapTiling.h.
#include "Game.h"
#include "Building.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include "MapTiling.h"
#include "Team.h"
#include "Player.h"
#include "Unit.h"
#include "UnitType.h"

#include <vector>

namespace
{
	// positions are offsets from the colony's anchor, the shortest way round the source torus
	struct BuildingTemplate
	{
		int x, y, typeNum, hp, bullets;
		int maxUnitWorking, maxUnitWorkingFuture, maxUnitInside;
		int priority, unitStayRange, minLevelToFlag, minWorkerLevelToFlag;
		bool explorersRequireBombing;
		Uint32 receiveResourceMask, sendResourceMask;
		Sint32 resources[MAX_NB_RESOURCES];
		Sint32 ratio[NB_UNIT_TYPE];
		bool clearingResources[BASIC_COUNT];
		Building::ConstructionResultState constructionResultState;
		int constructionOriginTypeNum;
		int repairInitialDeficit, repairHealthGranted;
		BuildingResourceCost constructionBudget, constructionReserved;
	};

	struct UnitTemplate
	{
		int x, y, typeNum, hp, hungry, hungriness, experience, experienceLevel, constructionLevel;
		Uint32 fruitMask, fruitCount;
		Unit::Medical medical;
		Sint32 level[NB_ABILITY];
	};

	struct ColonyTemplate
	{
		int anchorX = -1, anchorY = -1;
		std::vector<BuildingTemplate> buildings;
		std::vector<UnitTemplate> units;
	};

	//! Signed offset from a to v on a ring of `size`, taking the shorter way round
	int wrapOffset(int v, int a, int size)
	{
		int d = (v - a) % size;
		if (d < 0)
			d += size;
		if (d >= size / 2)
			d -= size;
		return d;
	}
}

bool Game::tileForPlay(int rx, int ry, int teamCount, int coloniesPerTeam)
{
	// Team numbers, entity ids and scripted areas cannot be meaningfully
	// remapped without rewriting authored scenario code. This is a map setup
	// operation, never a saved-game conversion.
	if (mapHeader.getIsSavedGame() || !mapscript.getMapScript().empty() || !sgslScript.sourceCode.empty())
		return false;
	const int mapTeams = mapHeader.getNumberOfTeams();
	// Map::tile changes dimension shifts, so only bounded powers of two are legal.
	// Check before multiplication and before mutating the loaded game.
	if (mapTeams < 1 || mapTeams > Team::MAX_COUNT || rx < 1 || ry < 1 ||
		(rx & (rx - 1)) || (ry & (ry - 1)) ||
		rx > MapTiling::MAX_MAP_SIDE / map.getW() || ry > MapTiling::MAX_MAP_SIDE / map.getH())
		return false;
	const int total = MapTiling::colonyCount(mapTeams, rx, ry);
	if (teamCount < 1)
		teamCount = std::min<int>(Team::MAX_COUNT, total);
	if (mapTeams < 1 || rx < 1 || ry < 1 || teamCount > Team::MAX_COUNT || teamCount > total)
		return false;
	if (map.getW() * rx > MapTiling::MAX_MAP_SIDE || map.getH() * ry > MapTiling::MAX_MAP_SIDE)
		return false;
	const MapHeader tiledHeader = MapTiling::tiledHeader(mapHeader, rx, ry, teamCount);

	// every colony of the map as it was loaded, each element relative to the colony's anchor:
	// a map wraps, so a unit may stand across the edge from its swarm, and the copy it belongs
	// to is the one that keeps it near that swarm
	const int w0 = map.getW(), h0 = map.getH();
	std::vector<ColonyTemplate> colonies(mapTeams);
	for (int t = 0; t < mapTeams; t++)
	{
		ColonyTemplate& colony = colonies[t];
		// the anchor is the first swarm, else the first building, else the first unit
		for (int pass = 0; pass < 2 && colony.anchorX < 0; pass++)
			for (int i = 0; i < Building::MAX_COUNT && colony.anchorX < 0; i++)
			{
				const Building* b = teams[t]->myBuildings[i];
				if (b && b->buildingState == Building::ALIVE && (pass == 1 || b->type->semantics.production.enabledUnitMask))
				{
					colony.anchorX = b->posX;
					colony.anchorY = b->posY;
				}
			}
		for (int i = 0; i < Unit::MAX_COUNT && colony.anchorX < 0; i++)
			if (teams[t]->myUnits[i] && !teams[t]->myUnits[i]->isDead)
			{
				colony.anchorX = teams[t]->myUnits[i]->posX;
				colony.anchorY = teams[t]->myUnits[i]->posY;
			}
		if (colony.anchorX < 0)
			continue;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			const Building* b = teams[t]->myBuildings[i];
			if (!b || b->buildingState != Building::ALIVE)
				continue;
			BuildingTemplate bt = {wrapOffset(b->posX, colony.anchorX, w0), wrapOffset(b->posY, colony.anchorY, h0),
				b->typeNum, b->hp, b->bullets, b->maxUnitWorking, b->getMaxUnitWorkingFuture(), b->maxUnitInside,
				b->priority, b->unitStayRange, b->minLevelToFlag, b->minWorkerLevelToFlag, b->explorersRequireBombing, b->receiveResourceMask, b->sendResourceMask, {}, {}, {}};
			bt.constructionResultState=b->constructionResultState;
			bt.constructionOriginTypeNum=b->constructionOriginTypeNum;
			bt.repairInitialDeficit=b->repairInitialDeficit; bt.repairHealthGranted=b->repairHealthGranted;
			bt.constructionBudget=b->constructionBudget;
			bt.constructionReserved=b->constructionReserved;
			for (int r = 0; r < MAX_NB_RESOURCES; r++)
				bt.resources[r] = b->resources[r];
			for (int u = 0; u < NB_UNIT_TYPE; u++)
				bt.ratio[u] = b->ratio[u];
			for (int r = 0; r < BASIC_COUNT; r++)
				bt.clearingResources[r] = b->clearingResources[r];
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
			UnitTemplate ut = {wrapOffset(u->posX, colony.anchorX, w0), wrapOffset(u->posY, colony.anchorY, h0),
				u->typeNum, u->hp, u->hungry, u->hungriness, u->experience, u->experienceLevel, u->constructionLevel,
				u->fruitMask, u->fruitCount, u->medical, {}};
			for (int a = 0; a < NB_ABILITY; a++)
				ut.level[a] = u->level[a];
			colonies[t].units.push_back(ut);
		}
	}

	// Refuse the whole conversion if an equal share exceeds the engine's
	// entity slots. A preview must never imply that a truncated base survived.
	std::vector<int> buildingCounts(teamCount), unitCounts(teamCount);
	for (int n = 0; n < total; ++n)
	{
		const int team = MapTiling::teamForColony(n, total, teamCount, coloniesPerTeam);
		if (team < 0) continue;
		buildingCounts[team] += int(colonies[n % mapTeams].buildings.size());
		unitCounts[team] += int(colonies[n % mapTeams].units.size());
		if (buildingCounts[team] > Building::MAX_COUNT || unitCounts[team] > Unit::MAX_COUNT)
			return false;
	}

	// the areas each colony's team painted, by tile of the map as loaded; Map::tile drops the
	// per-team masks, and every copy paints them again for the team its colony is dealt to
	struct PaintedTile
	{
		int x, y;
		bool forbidden, guard, clear, farm;
	};
	std::vector<std::vector<PaintedTile>> painted(mapTeams);
	for (int y = 0; y < h0; y++)
		for (int x = 0; x < w0; x++)
			for (int t = 0; t < mapTeams; t++)
			{
				const Uint32 mask = Team::teamNumberToMask(t);
				const bool forbidden = map.isForbidden(x, y, mask), guard = map.isGuardArea(x, y, mask), clear = map.isClearArea(x, y, mask), farm = map.isFarmArea(x, y, mask);
				if (forbidden || guard || clear || farm)
					painted[t].push_back({wrapOffset(x, colonies[t].anchorX, w0), wrapOffset(y, colonies[t].anchorY, h0), forbidden, guard, clear, farm});
			}

	// Loaded previews may carry local player/AI objects. They point at the
	// old teams, so dispose of them first and write a neutral starting map.
	const Uint32 sourceSeed = gameHeader.getRandomSeed();
	for (auto &player : players)
	{
		delete player;
		player = nullptr;
	}
	gameHeader.reset();
	gameHeader.setRandomSeed(sourceSeed);

	// removeTeam deliberately retains the last editor team. Here all teams
	// are replaced, so delete them explicitly; Map::tile clears their caches.
	for (int t = mapTeams - 1; t >= 0; --t)
	{
		delete teams[t];
		teams[t] = nullptr;
		sgslScript.removeTeam(t);
	}
	mapHeader.setNumberOfTeams(0);
	map.tile(rx, ry);
	map.setGame(this);
	for (int k = 0; k < teamCount; k++)
	{
		addTeam(TEAM_POS_END);
		const BaseTeam& initial = tiledHeader.getBaseTeam(k);
		teams[k]->teamNumber = initial.teamNumber;
		teams[k]->type = initial.type;
		teams[k]->numberOfPlayer = initial.numberOfPlayer;
		teams[k]->playersMask = initial.playersMask;
		teams[k]->setCorrectColor(initial.color);
		teams[k]->setCorrectMasks();
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
				// the anchor of this copy; offsets wrap on the repeated map
				const int ax = colonies[t].anchorX + i * w0, ay = colonies[t].anchorY + j * h0;
				const int W = map.getW(), H = map.getH();
				auto wrapX = [&](int v) { return ((v % W) + W) % W; };
				auto wrapY = [&](int v) { return ((v % H) + H) % H; };
				for (const BuildingTemplate& bt : colonies[t].buildings)
				{
					Building* b = addBuilding(wrapX(ax + bt.x), wrapY(ay + bt.y), bt.typeNum, k, bt.maxUnitWorking, bt.maxUnitWorkingFuture);
					if (!b)
						return false;
					b->hp = bt.hp;
					b->bullets = bt.bullets;
					b->maxUnitInside = bt.maxUnitInside;
					b->minLevelToFlag = bt.minLevelToFlag;
					b->minWorkerLevelToFlag=bt.minWorkerLevelToFlag;
					b->explorersRequireBombing=bt.explorersRequireBombing;
					b->receiveResourceMask = bt.receiveResourceMask;
					b->sendResourceMask = bt.sendResourceMask;
					b->priority = bt.priority;
					b->unitStayRange = bt.unitStayRange;
					for (int r = 0; r < MAX_NB_RESOURCES; r++)
						b->resources[r] = bt.resources[r];
					b->constructionResultState=bt.constructionResultState;
					b->constructionOriginTypeNum=bt.constructionOriginTypeNum;
					b->repairInitialDeficit=bt.repairInitialDeficit; b->repairHealthGranted=bt.repairHealthGranted;
					b->constructionBudget=bt.constructionBudget;
					b->constructionReserved=bt.constructionReserved;
					b->restoreConstructionReservations();
					for (int u = 0; u < NB_UNIT_TYPE; u++)
						b->ratio[u] = bt.ratio[u];
					for (int r = 0; r < BASIC_COUNT; r++)
						b->clearingResources[r] = bt.clearingResources[r];
					if (!teams[k]->startPosSet && b->type->semantics.production.enabledUnitMask)
					{
						teams[k]->startPosX = b->posX;
						teams[k]->startPosY = b->posY;
						teams[k]->startPosSet = 1;
					}
				}
				for (const PaintedTile& p : painted[t])
				{
					const int x = wrapX(ax + p.x), y = wrapY(ay + p.y);
					if (p.forbidden)
						map.addForbidden(x, y, k);
					if (p.guard)
						map.addGuardArea(x, y, k);
					if (p.clear)
						map.addClearArea(x, y, k);
					if (p.farm)
						map.addFarmArea(x, y, k);
				}
				for (const UnitTemplate& ut : colonies[t].units)
				{
					Unit* u = addUnit(wrapX(ax + ut.x), wrapY(ay + ut.y), k, ut.typeNum, std::max(ut.level[WALK], ut.level[SWIM]), 0, 0, 0);
					if (!u)
						return false;
					for (int a = 0; a < NB_ABILITY; a++)
					{
						u->level[a] = ut.level[a];
						u->performance[a] = teams[k]->race.getUnitType(ut.typeNum, ut.level[a])->performance[a];
					}
					u->hp = ut.hp;
					u->hungry = ut.hungry;
					u->hungriness = ut.hungriness;
					u->experience = ut.experience;
					u->experienceLevel = ut.experienceLevel;
					u->constructionLevel=ut.constructionLevel;
					u->fruitMask = ut.fruitMask;
					u->fruitCount = ut.fruitCount;
					u->medical = ut.medical;
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
