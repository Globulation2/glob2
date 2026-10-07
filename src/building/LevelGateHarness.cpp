// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression for Building::canUnitWorkHere: the schooling gate
// reads the independent construction qualification, not work-speed training.
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "FileManager.h"
#include <SDL3/SDL.h>
#include <string>
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Team.h"
#include "MapInternal.h"
#include "Race.h"
#include "Ressource.h"
#include "IntBuildingType.h"
#include <cstdio>
#include <cstdlib>

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
}

// One hiring pass: a worker of the given schooling level, carrying `carried`,
// four tiles from a building of the given kind that wants it. Returns whether
// the building hired the worker; `tooLowLevel` reports the rejection counter.
static bool hiringPass(const char* kind, int level, bool site, int carried, int workerLevel, Uint32* tooLowLevel, int harvestLevel = -1)
{
	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(5, 5, GRASS);
	game.map.setGame(&game);
	game.addTeam(0);
	Team* team = game.teams[0];
	team->race.loadDefault();
	int typeNum = globalContainer->buildingsTypes.getTypeNum(kind, level, site);
	require(typeNum >= 0, "building type exists");
	Building* building = game.addBuilding(8, 8, typeNum, 0);
	require(building != nullptr, "create building");
	building->maxUnitWorking = 2;
	building->materials[carried] = 0;
	building->updateCallLists();
	Unit* unit = game.addUnit(12, 12, 0, WORKER, 0, 255, 0, 0);
	require(unit != nullptr, "create worker");
	unit->level[HARVEST] = harvestLevel < 0 ? workerLevel : harvestLevel;
	unit->level[BUILD] = 3-workerLevel; // Deliberately different from qualification.
	unit->constructionLevel = workerLevel;
	unit->carriedMaterial = carried;
	unit->activity = Unit::ACT_RANDOM;
	unit->medical = Unit::MED_FREE;
	team->updateAllBuildingTasks();
	*tooLowLevel = building->unitsFailingRequirements[Building::UnitTooLowLevel];
	bool hired = building->unitsWorking.size() == 1;
	std::printf("%s level %d %s, worker level %d -> %s (too low level: %u)\n",
		kind, level + 1, site ? "site" : "completed", workerLevel, hired ? "hired" : "refused", *tooLowLevel);
	return hired;
}
}

TEST_SUITE("LevelGate")
{
	TEST_CASE("worker hiring reads construction qualification independently of speed training")
	{
		glob2test::HeadlessGlobals globals;
		Uint32 tooLow = 0;
		// The gate compares the authored requirement with construction qualification.
		require(hiringPass("inn", 0, false, WHEAT, 0, &tooLow) && tooLow == 0, "unschooled worker stocks a level-1 inn");
		require(!hiringPass("inn", 1, false, WHEAT, 0, &tooLow) && tooLow == 1, "unschooled worker refused by a level-2 inn");
		require(hiringPass("inn", 1, false, WHEAT, 1, &tooLow) && tooLow == 0, "level-1 worker stocks a level-2 inn");
		require(!hiringPass("inn", 1, true, WOOD, 0, &tooLow) && tooLow == 1, "unschooled worker refused by a level-2 inn site");
		require(hiringPass("inn", 1, true, WOOD, 1, &tooLow) && tooLow == 0, "level-1 worker builds a level-2 inn site");
		require(hiringPass("inn", 2, true, WOOD, 2, &tooLow) && tooLow == 0, "level-2 worker builds a level-3 inn site");
		// Harvest speed is independent of construction qualification.
		require(hiringPass("inn", 1, true, WOOD, 1, &tooLow, 0) && tooLow == 0, "qualification 1 with harvest 0 builds a level-2 inn site");
		require(!hiringPass("inn", 1, true, WOOD, 0, &tooLow, 1) && tooLow == 1, "qualification 0 with harvest 1 is refused by a level-2 inn site");
	}
}
