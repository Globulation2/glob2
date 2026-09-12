// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: after a delivery a worker is released for the hiring
// auction only when at least RELEASE_IDLE_WORKER_PERCENT of the team's workers
// are idle; otherwise it keeps its building and picks its next trip itself.
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
#include "UnitConsts.h"
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
	void deliver() { needToRecheckMedical = true; handleDisplacement(); }
};

// A worker standing at the door of an inn that wants wheat, wheat on its back,
// about to deposit; `others` more workers of the team, idle or busy.
struct Bed
{
	GameGUI gui;
	Game& game;
	Team* team;
	Building* inn;
	TestUnit* carrier;
	Bed(int others, bool othersIdle) : game(gui.game)
	{
		game.map.setSize(5, 5, GRASS);
		game.map.setGame(&game);
		for (int y = 0; y < game.map.getH(); ++y)
			for (int x = 0; x < game.map.getW(); ++x)
				game.map.clearImmobileUnit(x, y);
		game.addTeam(0);
		team = game.teams[0];
		team->race.loadDefault();
		int innType = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
		require(innType >= 0, "inn type exists");
		inn = game.addBuilding(8, 8, innType, 0);
		require(inn != nullptr, "inn placed");
		game.map.setBuilding(8, 8, inn->type->width, inn->type->height, inn->gid);
		inn->maxUnitWorking = 2;
		inn->resources[CORN] = 0;
		inn->updateCallLists();
		require(game.map.incResource(14, 8, CORN, 0), "wheat within reach for the next trip");
		// The carrier: attached, at the door, wheat on its back, depositing.
		carrier = new TestUnit(7, 8, Unit::GIDfrom(0, 0), WORKER, team, 0);
		team->myUnits[0] = carrier;
		game.map.setGroundUnit(7, 8, carrier->gid);
		carrier->activity = Unit::ACT_FILLING;
		carrier->medical = Unit::MED_FREE;
		carrier->attachedBuilding = inn;
		inn->unitsWorking.push_back(carrier);
		carrier->setTargetBuilding(inn);
		carrier->destinationPurpose = CORN;
		carrier->carriedResource = CORN;
		carrier->displacement = Unit::DIS_FILLING_BUILDING;
		carrier->dx = 1; carrier->dy = 0;
		carrier->validTarget = false;
		for (int i = 0; i < others; ++i)
		{
			Unit* u = game.addUnit(2 + i, 2, 0, WORKER, 0, 0, 0, 0);
			require(u != nullptr, "place another worker");
			u->medical = Unit::MED_FREE;
			u->activity = othersIdle ? Unit::ACT_RANDOM : Unit::ACT_FILLING;
		}
	}
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
	require(RELEASE_IDLE_WORKER_PERCENT == 20, "this harness assumes the one-in-five threshold");

	{
		// Four idle workers of five: released.
		Bed bed(4, true);
		require(bed.team->idleWorkerShareAtLeast(RELEASE_IDLE_WORKER_PERCENT), "four idle of five is above the threshold");
		bed.carrier->deliver();
		require(bed.inn->resources[CORN] == 1, "the wheat was deposited");
		require(bed.carrier->activity == Unit::ACT_RANDOM && bed.carrier->attachedBuilding == NULL, "released for the auction");
		require(bed.inn->unitsWorking.empty(), "no longer working for the inn");
		std::puts("gig release: with idle hands the deliverer goes back to the pool");
	}
	{
		// Four busy workers of five: keeps its building and heads out again.
		Bed bed(4, false);
		require(!bed.team->idleWorkerShareAtLeast(RELEASE_IDLE_WORKER_PERCENT), "nobody idle is below the threshold");
		bed.carrier->deliver();
		require(bed.inn->resources[CORN] == 1, "the wheat was deposited");
		require(bed.carrier->activity == Unit::ACT_FILLING && bed.carrier->attachedBuilding == bed.inn, "keeps its building");
		require(bed.carrier->displacement == Unit::DIS_GOING_TO_RESOURCE && bed.carrier->destinationPurpose == CORN, "already heading for the next wheat");
		std::puts("gig release: with nobody idle the deliverer keeps its job");
	}
	{
		// Exactly one idle of five: the threshold is inclusive.
		Bed bed(4, false);
		bed.team->myUnits[1]->activity = Unit::ACT_RANDOM;
		require(bed.team->idleWorkerShareAtLeast(RELEASE_IDLE_WORKER_PERCENT), "one in five counts");
		bed.carrier->deliver();
		require(bed.carrier->activity == Unit::ACT_RANDOM, "released at exactly one in five");
		std::puts("gig release: one idle worker in five is enough");
	}
	std::puts("PASS a delivery releases the worker only while a fifth of the team is idle");
	return 0;
}
