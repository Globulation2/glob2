// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: a resource-fetch target must track the gradient
// a walking unit actually follows, not a one-time snapshot from task
// assignment.
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "FileManager.h"
#include <SDL3/SDL.h>
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

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
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
	require(game.map.incResourceByIndex(nearX, nearY, WHEAT, 0), "seed the near wheat tile");
	require(game.map.incResourceByIndex(farX, farY, WHEAT, 0), "seed the far wheat tile");

	TestUnit* unit = new TestUnit(unitX, unitY, 0, WORKER, team, 0);
	team->myUnits[0] = unit;
	team->rebuildLiveLists();
	game.map.setGroundUnit(unitX, unitY, unit->gid);
	unit->destinationPurpose = WHEAT;
	unit->activity = Unit::ACT_FILLING;
	unit->displacement = Unit::DIS_GOING_TO_RESOURCE;
	unit->validTarget = true;
	unit->performance[WALK] = 10;
	unit->performance[SWIM] = swimSpeed;
	const int swimClass = unit->swimClass();
	require(swimClass == expectedClass, "exercise the requested swim class");

	// Task assignment: ascend the resource gradient once, same call as
	// Unit.cpp/UnitDisplacement.cpp.
	game.map.materialAvailableUpdateSlot(teamNumber, WHEAT, swimClass, unit->posX, unit->posY, &unit->targetX, &unit->targetY, NULL);
	require(unit->targetX == nearX && unit->targetY == nearY, "initial target is the nearer wheat tile");
	require(game.map.getGradient(teamNumber, WHEAT, swimClass, unit->targetX, unit->targetY) == GRADIENT_AT_GOAL,
		"initial target is the gradient's goal");

	// One action of walking: the target must not move while it is still valid.
	unit->stepGoingToResource();
	require(unit->targetX == nearX && unit->targetY == nearY, "target holds steady while still valid");

	// Another unit fully harvests the near tile. This mutates the resource
	// layer directly, the same way Map::decResource does; the cached
	// gradient is untouched until something rebuilds it.
	game.map.replaceResource(nearX, nearY, Resource{});
	require(game.map.getGradient(teamNumber, WHEAT, swimClass, nearX, nearY) == GRADIENT_AT_GOAL,
		"the cached gradient does not notice the depletion by itself");

	// Simulate the periodic rebuild every cached gradient gets from
	// Map::syncStep once per its round-robin turn.
	game.map.updateMaterialGradient(teamNumber, WHEAT, swimClass);
	require(game.map.getGradient(teamNumber, WHEAT, swimClass, nearX, nearY) != GRADIENT_AT_GOAL,
		"the rebuilt gradient no longer marks the depleted tile as the goal");

	// The unit takes its next action. pathfindResource reads the fresh
	// gradient and correctly steps toward the far tile; the stored target
	// must follow, not keep pointing at the now-empty near tile.
	unit->stepGoingToResource();
	require(unit->targetX == farX && unit->targetY == farY,
		"target is refreshed to the far wheat tile once the near one is gone");
	require(game.map.getGradient(teamNumber, WHEAT, swimClass, unit->targetX, unit->targetY) == GRADIENT_AT_GOAL,
		"refreshed target is the rebuilt gradient's goal");

	std::puts("PASS resource-fetch target is refreshed when the gradient it was ascended from is rebuilt");
}

// targetX/Y (also the debug path line, hotkey T) are set once, by ascending
// a gradient, when a fetch task starts (Unit.cpp, UnitDisplacement.cpp).
// pathfindMaterial (UnitMovement.cpp) re-reads the resource gradient that
// governs the unit's step fresh every action instead, and that field can be
// rebuilt while the unit is still walking. Fetching is greedy: the unit heads
// for the resource nearest to itself, even when another is a cheaper carry.
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
	require(game.map.materialSupplyModesSlot(inn, WHEAT) == 0, "stock permission without a supplier uses natural routing");
	game.map.setBuilding(5, 5, inn->type->width, inn->type->height, inn->gid);

	const int unitX = 16, unitY = 16;
	// A is close to the unit but a long carry from the building; B is a
	// longer fetch but a short carry. Greedy fetching walks to A.
	const int nearUnitX = 20, nearUnitY = 16;
	const int nearBuildingX = 7, nearBuildingY = 7;
	require(game.map.incResourceByIndex(nearUnitX, nearUnitY, WHEAT, 0), "seed the tile near the unit");
	require(game.map.incResourceByIndex(nearBuildingX, nearBuildingY, WHEAT, 0), "seed the tile near the building");

	TestUnit* unit = new TestUnit(unitX, unitY, 0, WORKER, team, 0);
	team->myUnits[0] = unit;
	team->rebuildLiveLists();
	game.map.setGroundUnit(unitX, unitY, unit->gid);
	unit->attachedBuilding = inn;
	unit->destinationPurpose = WHEAT;
	unit->activity = Unit::ACT_FILLING;
	unit->displacement = Unit::DIS_GOING_TO_RESOURCE;
	unit->validTarget = true;
	const int swimClass = unit->swimClass();

	require(game.map.getGlobalGradientDestination(game.map.getMaterialGradientSlot(teamNumber, WHEAT, swimClass), unit->posX, unit->posY, &unit->targetX, &unit->targetY),
		"sanity: ascending the plain gradient reaches an exact goal");
	require(unit->targetX == nearUnitX && unit->targetY == nearUnitY,
		"sanity: the plain gradient's nearest tile is the one close to the unit, not the building");

	// One action: the unit steps toward the tile nearest to it, and the target follows.
	unit->stepGoingToResource();
	require(unit->targetX == nearUnitX && unit->targetY == nearUnitY,
		"target follows the nearest resource, not the cheaper carry");

	// Another action while nothing changed: the target must hold steady.
	unit->stepGoingToResource();
	require(unit->targetX == nearUnitX && unit->targetY == nearUnitY,
		"target holds steady while still valid");

	// The near-unit tile gets fully harvested by someone else. The resource
	// gradient does not notice by itself.
	game.map.replaceResource(nearUnitX, nearUnitY, Resource{});
	game.map.updateMaterialGradient(teamNumber, WHEAT, swimClass);

	// Next action: only the near-building tile is left on the gradient: the
	// target must be refreshed to it.
	unit->stepGoingToResource();
	require(unit->targetX == nearBuildingX && unit->targetY == nearBuildingY,
		"target is refreshed to the only remaining wheat tile");

	require(game.map.materialRoutingCacheBytes() == 0, "ordinary natural fetch allocates no supplier cache");
	const auto* natural=game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass);
	const int marketType=game.buildingsTypes.getTypeNum("market",0,false);
	auto* market=game.addBuilding(24,24,marketType,0);
	require(market!=nullptr, "fruit-only market placed");
	market->materials[CHERRY]=1;
	require(!team->directStockSuppliers.empty(), "fruit-only market is a direct supplier");
	require(game.map.materialSupplyModesSlot(inn,WHEAT)==0, "fruit-only supplier cannot alter wheat routing");
	require(game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass,true,inn)==natural,
		"fruit-only supplier retains the original natural wheat field");
	require(game.map.materialRoutingCacheBytes()==0, "impossible wheat supplier creates no cached field");
	std::puts("PASS resource-fetch target tracks the nearest resource and refreshes when its gradient is rebuilt");
}
}

TEST_SUITE("ResourceFetchTarget")
{
	TEST_CASE("a depleted target is refreshed after its gradient rebuild for every swim class")
	{
		glob2test::HeadlessGlobals globals;
		const int swimSpeeds[] = {0, 20, 14, 10, 7, 5, 3};
		for (int swimClass = 0; swimClass < SWIM_CLASS_COUNT; ++swimClass)
			staleTargetIsRefreshedAfterGradientRebuild(swimClass, swimSpeeds[swimClass]);
		targetTracksTheGradientTheUnitActuallyFollows();
	}
}
