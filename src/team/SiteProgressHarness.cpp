// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: between two construction sites otherwise tied for
// workers, the one further along hires first, so a team short of hands
// finishes what it started before it spreads over new sites.
#include "EngineFixtures.h"
#include <list>
#include "GlobalContainer.h"
#include "FileManager.h"
#include <SDL3/SDL.h>
#include <string>
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Team.h"
#include "Building.h"
#include "BuildingType.h"
#include "MapInternal.h"
#include "Race.h"
#include "Ressource.h"
#include "IntBuildingType.h"
#include <cstdio>

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
}

// Two market sites, the left one holding some stone already, and one idle
// worker standing closer to the right, untouched one.
static void theSiteFurtherAlongHiresFirst()
{
	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(5, 5, GRASS); // 32x32
	game.map.setGame(&game);
	game.addTeam(0);
	Team* team = game.teams[0];
	team->race.loadDefault();

	const Sint32 siteType = globalContainer->buildingsTypes.getTypeNum("market", 0, true);
	require(siteType >= 0, "the market building site type exists");
	Building* started = game.addBuilding(4, 8, siteType, 0);
	Building* fresh = game.addBuilding(22, 8, siteType, 0);
	require(started && fresh, "place both sites");
	started->materials[STONE] = 2;

	int delivered = 0, total = 0;
	started->constructionProgress(&delivered, &total);
	std::printf("started site: %d of %d delivered\n", delivered, total);
	require(delivered > 0 && delivered < total, "the left site is part built");
	fresh->constructionProgress(&delivered, &total);
	require(delivered == 0 && total > 0, "the right site is untouched");
	require(Team::buildingHasHigherPriority(started, fresh), "the started site ranks first");
	require(!Team::buildingHasHigherPriority(fresh, started), "and the order is strict");

	for (int i = 0; i < 4; ++i)
	{
		require(game.map.incResourceByIndex(14, 4 + i, STONE, 0), "seed stone between the sites");
		require(game.map.incResourceByIndex(15, 4 + i, WOOD, 0), "seed wood between the sites");
	}
	Unit* worker = game.addUnit(19, 14, 0, WORKER, 0, 255, 0, 0);
	require(worker != nullptr, "place the worker");
	worker->activity = Unit::ACT_RANDOM;
	worker->medical = Unit::MED_FREE;
	started->updateCallLists();
	fresh->updateCallLists();

	team->updateAllBuildingTasks();
	std::printf("started site %zu workers, fresh site %zu workers\n",
		started->unitsWorking.size(), fresh->unitsWorking.size());
	require(started->unitsWorking.size() == 1 && fresh->unitsWorking.empty(),
		"the one worker goes to the site further along");
	std::puts("PASS the site further along hires first");
}
}

TEST_SUITE("SiteProgress")
{
	TEST_CASE("the site further along hires first")
	{
		glob2test::HeadlessGlobals globals;
		theSiteFurtherAlongHiresFirst();
	}
}
