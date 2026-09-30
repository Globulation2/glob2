// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: each building gets a hiring attempt before retries,
// even when subscription reorders the live bucket.
#include "EngineFixtures.h"
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

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
}

static void hiringRoundIsFair()
{
    GameGUI gui;
    Game& game = gui.game;
    game.map.setSize(5, 5, GRASS);
    game.map.setGame(&game);
    game.addTeam(0);
    Team* team = game.teams[0];
    team->race.loadDefault();
    auto* first = game.addBuilding(8, 8, globalContainer->buildingsTypes.getFinishedTypeNum("inn"), 0);
    auto* second = game.addBuilding(16, 8, globalContainer->buildingsTypes.getFinishedTypeNum("inn"), 0);
    require(first && second, "create competing inns");
    for (auto* building : {first, second}) {
        building->maxUnitWorking = 2;
        building->resources[WHEAT] = 0;
        building->updateCallLists();
    }
    for (int n = 0; n < 2; ++n) {
        auto* unit = game.addUnit(12+n, 12, 0, WORKER, 0, 255, 0, 0);
        require(unit != nullptr, "create available worker");
        unit->carriedResource = WHEAT;
        unit->activity = Unit::ACT_RANDOM;
        unit->medical = Unit::MED_FREE;
    }
    team->updateAllBuildingTasks();
    std::printf("Hiring round: first inn %zu workers, second inn %zu workers\n",
        first->unitsWorking.size(), second->unitsWorking.size());
    require(first->unitsWorking.size() == 1 && second->unitsWorking.size() == 1,
        "each competing inn gets one worker before either retries");
    std::puts("PASS hiring follows building identity while the bucket reorders");
}
}

TEST_SUITE("HiringBucket")
{
	TEST_CASE("hiring round is fair")
	{
		glob2test::HeadlessGlobals globals;
		hiringRoundIsFair();
	}
}
