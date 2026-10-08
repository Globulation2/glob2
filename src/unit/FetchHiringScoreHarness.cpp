// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression for hiring a fetcher: the hunger check that decides
// whether a unit may be hired for a resource measures the walk to that resource,
// not the whole fetch-and-carry trip, and the score estimates that whole trip.
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
#include "BuildingType.h"
#include "MapInternal.h"
#include "Race.h"
#include "Ressource.h"
#include "IntBuildingType.h"
#include "FixedPoint.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

// Named in a friend declaration, so it stays outside the anonymous namespace.
class FetchHiringScoreHarness
{
public:
    static std::vector<Unit*> candidates(Building* building)
    {
        Building::BringMaterialsCandidate candidates[Unit::MAX_COUNT];
        const int count=building->gatherBringMaterialsCandidates(candidates, WHEAT);
        std::vector<Unit*> units;
        for (int i=0; i<count; ++i) units.push_back(candidates[i].unit);
        return units;
    }
    static int unavailable(Building* building)
        { return building->unitsFailingRequirements[Building::UnitNotAvailable]; }
    static Unit* carrying(Building* building)
    {
        int targets[MaterialSlotCount], served[MaterialSlotCount];
        building->fetchApportionment(targets, served);
        Building::BringMaterialsSelection selection{-1, 0x7fffffff, nullptr};
        building->selectUnitCarryingWantedMaterial(targets, served, selection);
        return selection.choosen;
    }
	static bool consider(Building* building, Unit* unit, int resource, int* dist)
		{ return building->considerUnitForMaterial(unit, resource, dist); }
};

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
}

// considerUnitForMaterial is private; a friend fixture reaches it without
// exposing it to game callers, the way GameGUISelectionHarness does.

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
	team->rebuildLiveLists();
	game.map.setBuilding(siteX, siteY, type->width, type->height, site->gid);
	require(site->neededMaterial(WOOD) > 0, "the site still wants wood");

	// One patch of wood, a walk away from a unit standing at the site.
	const int woodX = siteX + 9, woodY = siteY;
	require(game.map.incResourceByIndex(woodX, woodY, WOOD, 0), "seed the wood tile");

	Unit* unit = new Unit(siteX - 2, siteY, 0, WORKER, team, 0);
	team->myUnits[0] = unit;
	team->rebuildLiveLists();
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
	require(game.map.materialAvailableSlot(0, WOOD, swimClass, unit->posX, unit->posY, &distResource),
		"the wood is reachable");

	const int wholeTrip = distResource + std::max(distBuilding, distResource);
	std::printf("distBuilding=%d distResource=%d wholeTrip=%d\n", distBuilding, distResource, wholeTrip);
	require(wholeTrip - distBuilding > distResource,
		"the trip really is longer than the walk to the wood, so the two are distinguishable");

	// Just enough time to reach the wood, not enough for the whole trip. The
	// unit is hireable: it eats at the site on the way back.
	const int timeLeft = distResource + 2;
	require(timeLeft > distBuilding, "the site itself is within reach");
	require(timeLeft < wholeTrip, "the whole trip does not fit in the same budget");
	unit->trigHungry = 100;
	unit->hungry = unit->trigHungry + timeLeft * unit->race->hungriness;

	int dist = 0;
	require(FetchHiringScoreHarness::consider(site, unit, WOOD, &dist),
		"a unit that can reach the wood is hireable for it");
	require(dist == (wholeTrip<<Q8_FIXED_POINT_SHIFT), "the hire is scored by the whole trip");

	// One step short of the wood is not enough.
	unit->hungry = unit->trigHungry + distResource * unit->race->hungriness;
	require(!FetchHiringScoreHarness::consider(site, unit, WOOD, &dist),
		"a unit that would starve before reaching the wood is not hired");
	std::puts("PASS the hunger check measures the walk to the resource, not the whole trip");
}

// The score is a whole trip, not the one-way walk, so a resource far from the
// building is not ranked as cheaply as one beside it.
static void theScoreEstimatesTheWholeTrip()
{
	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(5, 5, GRASS); // 32x32
	game.map.setGame(&game);
	game.addTeam(0);
	Team* team = game.teams[0];

	const Sint32 siteType = globalContainer->buildingsTypes.getTypeNum("market", 0, true);
	const BuildingType* type = globalContainer->buildingsTypes.get(siteType);
	const int siteX = 8, siteY = 8;
	Building* site = new Building(siteX, siteY, Building::GIDfrom(0, 0), siteType, team,
	                              &globalContainer->buildingsTypes, 4, 4);
	team->myBuildings[0] = site;
	team->rebuildLiveLists();
	game.map.setBuilding(siteX, siteY, type->width, type->height, site->gid);

	require(game.map.incResourceByIndex(siteX + 9, siteY, WOOD, 0), "seed the wood tile");

	// Standing at the building, so the walk out and the carry home are close to
	// the same length and the whole job is close to twice the walk.
	Unit* unit = new Unit(siteX - 2, siteY, 0, WORKER, team, 0);
	team->myUnits[0] = unit;
	team->rebuildLiveLists();
	unit->performance[WALK] = 10;
	unit->performance[HARVEST] = 10;
	unit->activity = Unit::ACT_RANDOM;
	unit->displacement = Unit::DIS_RANDOM;
	unit->medical = Unit::MED_FREE;
	unit->attachedBuilding = NULL;
	unit->trigHungry = 100;
	unit->hungry = unit->trigHungry + 1000 * unit->race->hungriness;

	const int swimClass = unit->swimClass();
	int distBuilding = 0, distResource = 0;
	require(game.map.buildingAvailable(site, swimClass, unit->posX, unit->posY, &distBuilding),
		"the site is reachable");
	require(game.map.materialAvailableSlot(0, WOOD, swimClass, unit->posX, unit->posY, &distResource),
		"the wood is reachable");
	require(distBuilding < distResource, "the unit is nearer the site than the wood");

	int dist = 0;
	require(FetchHiringScoreHarness::consider(site, unit, WOOD, &dist), "the unit is hireable for the wood");

	const int wholeTrip = distResource + std::max(distBuilding, distResource);
	std::printf("score: distBuilding=%d distResource=%d scored=%d expected=%d\n",
		distBuilding, distResource, dist, wholeTrip<<Q8_FIXED_POINT_SHIFT);
	require(dist == (wholeTrip<<Q8_FIXED_POINT_SHIFT),
		"the score is the walk out plus the carry home");

	std::puts("PASS the hiring score is the whole trip, not a one-way walk");
}
}

TEST_SUITE("FetchHiringScore")
{
    TEST_CASE("carrying candidates preserve sparse slot ties and all purpose writes")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true});
        auto* team = world.game.teams[0];
        auto* inn = world.addBuilding("inn", 8, 8, 0, 0);
        REQUIRE(inn);
        inn->materials[WHEAT] = 0;
        // Identical positions and hunger deliberately tie the candidates.
        // Attach in reverse order to exercise sorted traversal rather than insertion order.
        for (int slot : {Unit::MAX_COUNT-1, 3})
        {
            auto* unit = new Unit(6, 8, slot, WORKER, team, 0);
            team->myUnits[slot] = unit;
            team->attachUnit(slot);
            unit->performance[HARVEST] = 10;
            unit->activity = Unit::ACT_RANDOM;
            unit->medical = Unit::MED_FREE;
            unit->carriedMaterial = WHEAT;
            unit->destinationPurpose = -1;
        }
        CHECK(FetchHiringScoreHarness::carrying(inn) == team->myUnits[3]);
        CHECK(team->myUnits[3]->destinationPurpose == WHEAT);
        CHECK(team->myUnits[Unit::MAX_COUNT-1]->destinationPurpose == WHEAT);
        REQUIRE(world.game.map.incResourceByIndex(16, 8, WHEAT, 0));
        CHECK(FetchHiringScoreHarness::candidates(inn) == std::vector<Unit*>{team->myUnits[3], team->myUnits[Unit::MAX_COUNT-1]});
        team->myUnits[Unit::MAX_COUNT-1]->activity = Unit::ACT_FILLING;
        CHECK(FetchHiringScoreHarness::candidates(inn) == std::vector<Unit*>{team->myUnits[3]});
        CHECK(FetchHiringScoreHarness::unavailable(inn) == 1);
        FetchHiringScoreHarness::candidates(inn);
        CHECK(FetchHiringScoreHarness::unavailable(inn) == 1);

    }

	TEST_CASE("a unit is judged on the walk to the resource")
	{
		glob2test::HeadlessGlobals globals;
		aUnitIsJudgedOnTheWalkToTheResource();
	}
	TEST_CASE("the score estimates the whole fetch-and-carry trip")
	{
		glob2test::HeadlessGlobals globals;
		theScoreEstimatesTheWholeTrip();
	}
}
