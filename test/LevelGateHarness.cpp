// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression for Building::canUnitWorkHere: the schooling gate
// reads the one worker level (build), not the harvest half.
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#include "FileManager.h"
#include <SDL.h>
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

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
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
	building->resources[carried] = 0;
	building->updateCallLists();
	Unit* unit = game.addUnit(12, 12, 0, WORKER, 0, 255, 0, 0);
	require(unit != nullptr, "create worker");
	unit->level[HARVEST] = harvestLevel < 0 ? workerLevel : harvestLevel;
	unit->level[BUILD] = workerLevel;
	unit->carriedResource = carried;
	unit->activity = Unit::ACT_RANDOM;
	unit->medical = Unit::MED_FREE;
	team->updateAllBuildingTasks();
	*tooLowLevel = building->unitsFailingRequirements[Building::UnitTooLowLevel];
	bool hired = building->unitsWorking.size() == 1;
	std::printf("%s level %d %s, worker level %d -> %s (too low level: %u)\n",
		kind, level + 1, site ? "site" : "completed", workerLevel, hired ? "hired" : "refused", *tooLowLevel);
	return hired;
}

int main(int argc, char** argv)
{
	SDL_SetMainReady();
	require(argc == 3, "usage: harness PROFILE ROOT");
	require(std::string(argv[1]).find("glob2-save-test-") == 0, "disposable profile required");
	GlobalContainer globals(argv[1]);
	globals.fileManager->addDir(argv[2]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();

	Uint32 tooLow = 0;
	// The gate compares the building's tier with the worker level.
	require(hiringPass("inn", 0, false, CORN, 0, &tooLow) && tooLow == 0, "unschooled worker stocks a level-1 inn");
	require(!hiringPass("inn", 1, false, CORN, 0, &tooLow) && tooLow == 1, "unschooled worker refused by a level-2 inn");
	require(hiringPass("inn", 1, false, CORN, 1, &tooLow) && tooLow == 0, "level-1 worker stocks a level-2 inn");
	require(!hiringPass("inn", 1, true, WOOD, 0, &tooLow) && tooLow == 1, "unschooled worker refused by a level-2 inn site");
	require(hiringPass("inn", 1, true, WOOD, 1, &tooLow) && tooLow == 0, "level-1 worker builds a level-2 inn site");
	require(hiringPass("inn", 2, true, WOOD, 2, &tooLow) && tooLow == 0, "level-2 worker builds a level-3 inn site");
	// It reads the worker level (build); a stale harvest value does not matter.
	require(hiringPass("inn", 1, true, WOOD, 1, &tooLow, 0) && tooLow == 0, "build level 1 with harvest 0 builds a level-2 inn site");
	require(!hiringPass("inn", 1, true, WOOD, 0, &tooLow, 1) && tooLow == 1, "build level 0 with harvest 1 is refused by a level-2 inn site");
	std::puts("PASS the hiring gate reads the one worker level");
	return 0;
}
