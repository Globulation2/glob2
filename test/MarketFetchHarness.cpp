// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: a building's fetch gradient counts the team's
// stocked markets as goals, so a worker hired for a resource no tile holds is
// led to a market, takes the resource at its door and carries it home; a
// nearer tile still wins; a market that runs dry stops being a goal.
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

struct TestUnit : Unit
{
	using Unit::Unit;
	void arrive() { handleDisplacement(); }
};

struct Bed
{
	GameGUI gui;
	Game& game;
	Team* team;
	Building* inn;
	Building* market;
	TestUnit* unit;
	Bed(int unitX, int unitY) : game(gui.game)
	{
		game.map.setSize(6, 6, GRASS); // 64x64
		game.map.setGame(&game);
		for (int y = 0; y < game.map.getH(); ++y)
			for (int x = 0; x < game.map.getW(); ++x)
				game.map.clearImmobileUnit(x, y);
		game.addTeam(0);
		team = game.teams[0];
		team->race.loadDefault();
		// Fruit is only a goal while seen: give the team vision of the whole bed.
		game.map.setMapDiscovered(0, 0, game.map.getW(), game.map.getH(), team->me);
		// A level-2 inn wants 80 of each fruit.
		int innType = globalContainer->buildingsTypes.getTypeNum("inn", 1, false);
		require(innType >= 0, "inn type exists");
		inn = game.addBuilding(6, 6, innType, 0);
		require(inn != nullptr, "inn placed");
		game.map.setBuilding(6, 6, inn->type->width, inn->type->height, inn->gid);
		inn->maxUnitWorking = 2;
		inn->resources[CORN] = inn->type->maxResource[CORN];
		inn->updateCallLists();
		int marketType = globalContainer->buildingsTypes.getTypeNum("market", 0, false);
		require(marketType >= 0, "market type exists");
		market = game.addBuilding(40, 40, marketType, 0);
		require(market != nullptr, "market placed");
		game.map.setBuilding(40, 40, market->type->width, market->type->height, market->gid);
		unit = new TestUnit(unitX, unitY, Unit::GIDfrom(0, 0), WORKER, team, 1);
		team->myUnits[0] = unit;
		game.map.setGroundUnit(unitX, unitY, unit->gid);
		unit->activity = Unit::ACT_RANDOM;
		unit->medical = Unit::MED_FREE;
	}
	void hire() { team->updateAllBuildingTasks(); }
	bool marketTile(int x, int y) const { return game.map.getBuilding(x, y) == market->gid; }
};

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

	{
		// No cherries on the map, ten in the market four tiles from the worker.
		Bed bed(36, 36);
		bed.market->resources[CHERRY] = 10;
		int dist = 0;
		require(bed.game.map.resourceAvailable(0, CHERRY, bed.unit->swimClass(), 36, 36, &dist, true), "the with-markets gradient reaches the stocked market");
		require(!bed.game.map.resourceAvailable(0, CHERRY, bed.unit->swimClass(), 36, 36, &dist, false), "the plain gradient knows no cherries");
		bed.hire();
		require(bed.inn->unitsWorking.size() == 1 && bed.unit->destinationPurpose == CHERRY, "inn hires the worker for cherries held by the market");
		require(bed.unit->displacement == Unit::DIS_GOING_TO_RESOURCE, "walking the fetch gradient");
		require(bed.marketTile(bed.unit->targetX, bed.unit->targetY), "the gradient's goal is the market");
		std::printf("market fetch: hired via the market, distance %d tiles\n", dist);
	}
	{
		// Standing at the market's door: the take happens on arrival. A fruit
		// delivery is ten units (multiplierResource), so is a take.
		Bed bed(39, 39);
		bed.market->resources[CHERRY] = 20;
		bed.hire();
		require(bed.inn->unitsWorking.size() == 1 && bed.unit->displacement == Unit::DIS_GOING_TO_RESOURCE, "hired and walking");
		bed.unit->arrive();
		require(bed.unit->carriedResource == CHERRY, "took a cherry out of the market");
		require(bed.market->resources[CHERRY] == 10, "market stock went down by one take");
		require(bed.unit->displacement == Unit::DIS_GOING_TO_BUILDING && bed.unit->targetBuilding == bed.inn, "carrying it to the inn");
		std::puts("market fetch: the resource is taken at the market's door");
	}
	{
		// Cherries two tiles from the worker beat the market.
		Bed bed(36, 36);
		bed.market->resources[CHERRY] = 10;
		require(bed.game.map.incResource(34, 36, CHERRY, 0), "seed a cherry tile near the worker");
		bed.hire();
		require(bed.inn->unitsWorking.size() == 1 && bed.unit->displacement == Unit::DIS_GOING_TO_RESOURCE, "hired for cherries");
		require(bed.unit->targetX == 34 && bed.unit->targetY == 36, "the nearer tile is the goal, not the market");
		std::puts("market fetch: a nearer tile wins over the market");
	}
	{
		// The market runs dry: after the rebuild it is no goal any more.
		Bed bed(36, 36);
		bed.market->resources[CHERRY] = 1;
		int dist = 0;
		require(bed.game.map.resourceAvailable(0, CHERRY, bed.unit->swimClass(), 36, 36, &dist, true), "stocked market is a goal");
		bed.market->removeResourceFromBuilding(CHERRY);
		bed.game.map.updateResourcesGradient(0, CHERRY, bed.unit->swimClass(), true);
		require(!bed.game.map.resourceAvailable(0, CHERRY, bed.unit->swimClass(), 36, 36, &dist, true), "an empty market is no goal");
		bed.hire();
		require(bed.inn->unitsWorking.empty() && bed.inn->unitsFailingRequirements[Building::UnitCantAccessFruit] >= 1, "nobody hired, counted as no fruit reachable");
		std::puts("market fetch: an empty market is no source");
	}
	{
		// Market levels: a level-1 market never hands out wood even though the
		// team pool holds some; a level-3 market does.
		Bed bed(36, 36);
		require(globalContainer->buildingsTypes.getTypeNum("market", 1, false) >= 0 && globalContainer->buildingsTypes.getTypeNum("market", 2, false) >= 0, "market levels 2 and 3 exist");
		bed.market->resources[WOOD] = 20;
		int dist = 0;
		require(!bed.game.map.resourceAvailable(0, WOOD, bed.unit->swimClass(), 36, 36, &dist, true), "a level-1 market is no wood goal");
		int top = globalContainer->buildingsTypes.getTypeNum("market", 2, false);
		bed.market->typeNum = top;
		bed.market->type = globalContainer->buildingsTypes.get(top);
		bed.game.map.updateResourcesGradient(0, WOOD, bed.unit->swimClass(), true);
		require(bed.game.map.resourceAvailable(0, WOOD, bed.unit->swimClass(), 36, 36, &dist, true), "a level-3 market hands out wood");
		std::puts("market levels: only a level that takes the resource hands it out");
	}
	std::puts("PASS stocked markets are goals of the fetch gradients");
	return 0;
}
