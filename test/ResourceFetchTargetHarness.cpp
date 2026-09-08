// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: a resource-fetch target must track the gradient
// a walking unit actually follows, not a one-time snapshot from task
// assignment.
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
#include "Building.h"
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

// handleMovementGoingToResource() is protected; a subclass gets access the
// same way test/README.md's Map subclass pattern does for Map predicates.
struct TestUnit : Unit
{
	TestUnit(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, int level)
		: Unit(x, y, gid, typeNum, team, level) {}
	void stepGoingToResource() { handleMovementGoingToResource(); }
};

static void staleTargetIsRefreshedAfterGradientRebuild(int expectedClass, int swimSpeed)
{
	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(5, 5, GRASS); // 32x32
	game.map.setGame(&game);
	game.addTeam(0);
	Team* team = game.teams[0];
	const int teamNumber = team->teamNumber;

	const int unitX = 16, unitY = 16;
	const int nearX = 20, nearY = 16; // distance 4
	const int farX = 16, farY = 25;   // distance 9
	require(game.map.incResource(nearX, nearY, CORN, 0), "seed the near corn tile");
	require(game.map.incResource(farX, farY, CORN, 0), "seed the far corn tile");

	TestUnit* unit = new TestUnit(unitX, unitY, 0, WORKER, team, 0);
	team->myUnits[0] = unit;
	game.map.setGroundUnit(unitX, unitY, unit->gid);
	unit->destinationPurpose = CORN;
	unit->activity = Unit::ACT_FILLING;
	unit->displacement = Unit::DIS_GOING_TO_RESOURCE;
	unit->validTarget = true;
	unit->performance[WALK] = 10;
	unit->performance[SWIM] = swimSpeed;
	const int swimClass = unit->swimClass();
	require(swimClass == expectedClass, "exercise the requested swim class");

	// Task assignment: ascend the resource gradient once, same call as
	// Unit.cpp/UnitDisplacement.cpp.
	game.map.resourceAvailableUpdate(teamNumber, CORN, swimClass, unit->posX, unit->posY, &unit->targetX, &unit->targetY, NULL);
	require(unit->targetX == nearX && unit->targetY == nearY, "initial target is the nearer corn tile");
	require(game.map.getGradient(teamNumber, CORN, swimClass, unit->targetX, unit->targetY) == GRADIENT_AT_GOAL,
		"initial target is the gradient's goal");

	// One action of walking: the target must not move while it is still valid.
	unit->stepGoingToResource();
	require(unit->targetX == nearX && unit->targetY == nearY, "target holds steady while still valid");

	// Another unit fully harvests the near tile. This mutates the resource
	// layer directly, the same way Map::decResource does; the cached
	// gradient is untouched until something rebuilds it.
	game.map.getTile(nearX, nearY).resource.clear();
	require(game.map.getGradient(teamNumber, CORN, swimClass, nearX, nearY) == GRADIENT_AT_GOAL,
		"the cached gradient does not notice the depletion by itself");

	// Simulate the periodic rebuild every cached gradient gets from
	// Map::syncStep once per its round-robin turn.
	game.map.updateResourcesGradient(teamNumber, CORN, swimClass);
	require(game.map.getGradient(teamNumber, CORN, swimClass, nearX, nearY) != GRADIENT_AT_GOAL,
		"the rebuilt gradient no longer marks the depleted tile as the goal");

	// The unit takes its next action. pathfindResource reads the fresh
	// gradient and correctly steps toward the far tile; the stored target
	// must follow, not keep pointing at the now-empty near tile.
	unit->stepGoingToResource();
	require(unit->targetX == farX && unit->targetY == farY,
		"target is refreshed to the far corn tile once the near one is gone");
	require(game.map.getGradient(teamNumber, CORN, swimClass, unit->targetX, unit->targetY) == GRADIENT_AT_GOAL,
		"refreshed target is the rebuilt gradient's goal");

	std::puts("PASS resource-fetch target is refreshed when the gradient it was ascended from is rebuilt");
}

// targetX/Y (also the debug path line, hotkey T) are set once, by ascending
// a gradient, when a fetch task starts (Unit.cpp, UnitDisplacement.cpp).
// pathfindResource (UnitMovement.cpp) re-reads whichever gradient actually
// governs the unit's step fresh every action instead -- the building's
// round-trip field when it minimises fetch-plus-carry, the plain resource
// gradient otherwise -- and either field can be rebuilt, or the preference
// between them can flip, while the unit is still walking.
static void targetTracksTheGradientTheUnitActuallyFollows()
{
	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(5, 5, GRASS); // 32x32
	game.map.setGame(&game);
	// setSize leaves immobileUnits[] zeroed, not IMMOBILE_UNIT_NONE (255), so
	// every cell reads as immobile-unit-occupied until cleared; a real game
	// never observes this because something else sweeps it first.
	for (int y = 0; y < game.map.getH(); ++y)
		for (int x = 0; x < game.map.getW(); ++x)
			game.map.clearImmobileUnit(x, y);
	game.addTeam(0);
	Team* team = game.teams[0];
	const int teamNumber = team->teamNumber;
	const int innType = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
	require(innType >= 0, "inn type exists");
	Building* inn = game.addBuilding(5, 5, innType, 0);
	require(inn != nullptr, "inn placed");
	game.map.setBuilding(5, 5, inn->type->width, inn->type->height, inn->gid);

	const int unitX = 16, unitY = 16;
	// A is close to the unit but a long carry from the building; B is a
	// longer fetch but a short carry, so the round trip through B is
	// cheaper even though A is the nearer tile to ascend to from the unit.
	const int nearUnitX = 20, nearUnitY = 16;
	const int nearBuildingX = 7, nearBuildingY = 7;
	require(game.map.incResource(nearUnitX, nearUnitY, CORN, 0), "seed the tile near the unit");
	require(game.map.incResource(nearBuildingX, nearBuildingY, CORN, 0), "seed the tile near the building");

	TestUnit* unit = new TestUnit(unitX, unitY, 0, WORKER, team, 0);
	team->myUnits[0] = unit;
	game.map.setGroundUnit(unitX, unitY, unit->gid);
	unit->attachedBuilding = inn;
	unit->destinationPurpose = CORN;
	unit->activity = Unit::ACT_FILLING;
	unit->displacement = Unit::DIS_GOING_TO_RESOURCE;
	unit->validTarget = true;
	const int swimClass = unit->swimClass();

	require(game.map.getGlobalGradientDestination(game.map.getResourceGradient(teamNumber, CORN, swimClass), unit->posX, unit->posY, &unit->targetX, &unit->targetY),
		"sanity: ascending the plain gradient reaches an exact goal");
	require(unit->targetX == nearUnitX && unit->targetY == nearUnitY,
		"sanity: the plain gradient's nearest tile is the one close to the unit, not the building");

	// One action: pathfindResource builds and prefers the round-trip field,
	// so the unit steps toward the tile that is cheaper to fetch and carry,
	// and the target must follow it, not the plain gradient's nearer tile.
	unit->stepGoingToResource();
	require(unit->targetX == nearBuildingX && unit->targetY == nearBuildingY,
		"target follows the round-trip gradient's cheaper tile, not the nearest one to the unit");

	// Another action while nothing changed: the target must hold steady.
	unit->stepGoingToResource();
	require(unit->targetX == nearBuildingX && unit->targetY == nearBuildingY,
		"target holds steady while still valid");

	// The near-building tile gets fully harvested by someone else. Neither
	// the resource gradient nor the round-trip field notice by themselves.
	game.map.getTile(nearBuildingX, nearBuildingY).resource.clear();
	game.map.updateResourcesGradient(teamNumber, CORN, swimClass);
	game.map.updateRoundTripGradient(inn, CORN, swimClass);

	// Next action: only the near-unit tile is left on either gradient: the
	// target must be refreshed to it.
	unit->stepGoingToResource();
	require(unit->targetX == nearUnitX && unit->targetY == nearUnitY,
		"target is refreshed to the only remaining corn tile");

	std::puts("PASS resource-fetch target tracks the round-trip gradient and refreshes when it is rebuilt");
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
	const int swimSpeeds[] = {0, 20, 14, 10, 7, 5, 3};
	for (int swimClass = 0; swimClass < SWIM_CLASS_COUNT; ++swimClass)
		staleTargetIsRefreshedAfterGradientRebuild(swimClass, swimSpeeds[swimClass]);
	targetTracksTheGradientTheUnitActuallyFollows();
	std::puts("Resource fetch target regressions passed");
	return 0;
}
