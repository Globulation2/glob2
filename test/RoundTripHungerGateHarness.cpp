// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: the hunger check that decides whether a unit may be
// hired for a resource must measure the walk to that resource, not the length
// of the whole fetch-and-carry trip.
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
#include "BuildingType.h"
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

// considerUnitForResources is private; a friend fixture reaches it without
// exposing it to game callers, the way GameGUISelectionHarness does.
class RoundTripHungerGateHarness
{
public:
	static bool consider(Building* building, Unit* unit, int* dist, int* resource)
		{ return building->considerUnitForResources(unit, dist, resource); }
};

static void aUnitIsJudgedOnTheWalkToTheResource()
{
	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(5, 5, GRASS); // 32x32
	game.map.setGame(&game);
	game.addTeam(0);
	Team* team = game.teams[0];

	const Sint32 siteType = globalContainer->buildingsTypes.getTypeNum("market", 0, true);
	require(siteType >= 0, "the market building site type exists");
	const BuildingType* type = globalContainer->buildingsTypes.get(siteType);

	const int siteX = 8, siteY = 8;
	Building* site = new Building(siteX, siteY, Building::GIDfrom(0, 0), siteType, team,
	                              &globalContainer->buildingsTypes, 4, 4);
	team->myBuildings[0] = site;
	game.map.setBuilding(siteX, siteY, type->width, type->height, site->gid);
	require(site->neededResource(WOOD) > 0, "the site still wants wood");

	// One patch of wood, a walk away from a unit standing at the site.
	const int woodX = siteX + 9, woodY = siteY;
	require(game.map.incResource(woodX, woodY, WOOD, 0), "seed the wood tile");

	Unit* unit = new Unit(siteX - 2, siteY, 0, WORKER, team, 0);
	team->myUnits[0] = unit;
	unit->performance[WALK] = 10;
	unit->performance[HARVEST] = 10;
	unit->activity = Unit::ACT_RANDOM;
	unit->displacement = Unit::DIS_RANDOM;
	unit->medical = Unit::MED_FREE;
	unit->attachedBuilding = NULL;
	const int swimClass = unit->swimClass();

	int distBuilding = 0, distResource = 0;
	require(game.map.buildingAvailable(site, swimClass, unit->posX, unit->posY, &distBuilding),
		"the site is reachable");
	require(game.map.resourceAvailable(0, WOOD, swimClass, unit->posX, unit->posY, &distResource),
		"the wood is reachable");

	// The round-trip field only exists once somebody has fetched this resource
	// for this building, which is when the two lengths can be confused.
	require(game.map.roundTripGradient(site, WOOD, swimClass) != NULL, "the round-trip field builds");
	int roundTrip = 0;
	require(game.map.roundTripDistance(site, WOOD, swimClass, unit->posX, unit->posY, &roundTrip),
		"the round trip is known here");
	std::printf("distBuilding=%d distResource=%d roundTrip=%d\n", distBuilding, distResource, roundTrip);
	require(roundTrip - distBuilding > distResource,
		"the trip really is longer than the walk to the wood, so the two are distinguishable");

	// Just enough time to reach the wood, not enough for the whole trip. The
	// unit is hireable: it eats at the site on the way back.
	const int timeLeft = distResource + 2;
	require(timeLeft > distBuilding, "the site itself is within reach");
	require(timeLeft < roundTrip - distBuilding, "the whole trip does not fit in the same budget");
	unit->trigHungry = 100;
	unit->hungry = unit->trigHungry + timeLeft * unit->race->hungriness;

	int dist = 0, resource = -1;
	require(RoundTripHungerGateHarness::consider(site, unit, &dist, &resource),
		"a unit that can reach the wood is hireable for it");
	require(resource == WOOD, "and it is hired for the wood");

	std::puts("PASS the hunger check measures the walk to the resource, not the whole trip");
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
	aUnitIsJudgedOnTheWalkToTheResource();
	std::puts("Round-trip hunger gate regressions passed");
	return 0;
}
