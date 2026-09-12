// SPDX-License-Identifier: GPL-3.0-or-later
// A fertile checkerboard farm fetched from by many inn workers. Each forbidden
// square holds a protected wheat seed; wheat spreads from the seeds into the
// open squares one unit at a time, and a single harvest clears a new tile, so
// every worker is drawn to the same few ripe tiles. This is the crowding the
// scenario measures.
//
// Usage: WheatCrowdingHarness [ticks] [checker|dots|sparse] [inns] [workersPerInn] [sink] [seedSpan]
//   checker: every other square is a seed; dots: one in four; sparse: one in sixteen.
//   seedSpan (odd, default 13) confines the seeds to a centred square that many
//   tiles wide, leaving the rest of the farm open ground: the same farm, less wheat.
//   sink=1 (default) empties the inns and keeps the workers fed every tick, so
//   demand never runs out and the workforce stays constant: deliveries then
//   measure fetch throughput alone. sink=0 leaves the colony to itself.
// Set GLOB2_HARVEST_METRICS=1 for the fetch counters and WHEAT_TIMELINE_EVERY=N
// for a WHEAT_TICK line every N ticks, WHEAT_SEED=N for another random run. The final WHEAT_RESULT line carries the
// checksum, so two builds can be compared for identical simulation.
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "HarvestMetrics.h"
#include "Unit.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "Ressource.h"
#include "Utilities.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

namespace
{
	// 64x64, all water apart from a base island and a farm peninsula joined
	// by a land bridge. The farm has water on three sides within 8 tiles, so
	// the random water probe in Map::growResources nearly always succeeds.
	constexpr int BASE_X0 = 4, BASE_X1 = 26, BASE_Y0 = 14, BASE_Y1 = 50;
	constexpr int BRIDGE_X1 = 33, BRIDGE_Y0 = 30, BRIDGE_Y1 = 34;
	constexpr int FARM_X0 = 33, FARM_X1 = 47, FARM_Y0 = 25, FARM_Y1 = 39;

	constexpr int FARM_CX = (FARM_X0 + FARM_X1) / 2, FARM_CY = (FARM_Y0 + FARM_Y1) / 2;

	enum class Pattern { Checker, Dots, Sparse };

	int seedSpan = FARM_X1 - FARM_X0 - 1;

	bool isSeed(int x, int y, Pattern pattern)
	{
		// Leave a one-tile open border so workers can walk around the farm.
		if (x <= FARM_X0 || x >= FARM_X1 || y <= FARM_Y0 || y >= FARM_Y1)
			return false;
		if (std::abs(x - FARM_CX) > seedSpan / 2 || std::abs(y - FARM_CY) > seedSpan / 2)
			return false;
		switch (pattern)
		{
			case Pattern::Checker: return (x + y) % 2 == 0;
			case Pattern::Dots: return x % 2 == 0 && y % 2 == 0;
			case Pattern::Sparse: return x % 4 == 0 && y % 4 == 0;
		}
		return false;
	}

	void paintGrass(Game& game, int x0, int x1, int y0, int y1)
	{
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x)
				game.map.setUMatPos(x, y, GRASS, 1);
	}
}

int main(int argc, char** argv)
{
	const int ticks = argc > 1 ? std::atoi(argv[1]) : 12000;
	const std::string patternName = argc > 2 ? argv[2] : "checker";
	const int innCount = argc > 3 ? std::atoi(argv[3]) : 6;
	const int workersPerInn = argc > 4 ? std::atoi(argv[4]) : 8;
	const bool sink = argc > 5 ? std::atoi(argv[5]) != 0 : true;
	if (argc > 6)
		seedSpan = std::atoi(argv[6]);
	require(patternName == "checker" || patternName == "dots" || patternName == "sparse", "pattern is checker, dots or sparse");
	const Pattern pattern = patternName == "checker" ? Pattern::Checker : patternName == "dots" ? Pattern::Dots : Pattern::Sparse;
	require(ticks > 0 && innCount > 0 && innCount <= 6 && workersPerInn > 0 && seedSpan > 0,
		"usage: [ticks] [checker|dots|sparse] [inns<=6] [workersPerInn] [sink] [seedSpan]");

	GlobalContainer globals;
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();

	// WHEAT_SEED picks a different, still deterministic, run of the same farm.
	if (const char* seed = std::getenv("WHEAT_SEED"))
		setSyncRandSeed((Uint32)std::strtoul(seed, nullptr, 10));

	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(6, 6, WATER);
	game.map.setGame(&game);
	game.addTeam(0);
	Team* team = game.teams[0];
	// The gradient scheduler expects an in-use field; allocation is lazy.
	game.map.getResourceGradient(0, CORN, 0);

	paintGrass(game, BASE_X0, BASE_X1, BASE_Y0, BASE_Y1);
	paintGrass(game, BASE_X1, BRIDGE_X1, BRIDGE_Y0, BRIDGE_Y1);
	paintGrass(game, FARM_X0, FARM_X1, FARM_Y0, FARM_Y1);

	int seeds = 0;
	for (int y = FARM_Y0; y <= FARM_Y1; ++y)
		for (int x = FARM_X0; x <= FARM_X1; ++x)
			if (isSeed(x, y, pattern))
			{
				game.map.addForbidden(x, y, 0);
				require(game.map.incResource(x, y, CORN, 0), "plant a wheat seed");
				for (int grow = 0; grow < 4; ++grow)
					game.map.incResource(x, y, CORN, 0);
				++seeds;
			}

	// Empty inns in two columns by the bridge, each staffed well past what the
	// farm can feed.
	const int innType = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
	require(innType >= 0, "inn type exists");
	const int innYs[6] = {16, 22, 28, 34, 40, 46};
	const int innXs[2] = {20, 14};
	for (int i = 0; i < innCount; ++i)
	{
		Building* inn = game.addBuilding(innXs[i / 3 % 2], innYs[(i * 2 + 1) % 6], innType, 0, workersPerInn, workersPerInn);
		require(inn != nullptr, "inn placed");
		game.map.setBuilding(inn->posX, inn->posY, inn->type->width, inn->type->height, inn->gid);
		inn->resources[CORN] = 0;
		inn->update();
	}

	// Idle workers spread over the base island.
	const int workers = innCount * workersPerInn;
	int placed = 0, fed = 0;
	for (int y = BASE_Y0 + 1; y < BASE_Y1 && placed < workers; y += 2)
		for (int x = BASE_X0 + 1; x < 12 && placed < workers; x += 2)
			if (Unit* unit = game.addUnit(x, y, 0, WORKER, 0, 0, 0, 0))
			{
				fed = unit->hungry;
				++placed;
			}
	require(placed == workers, "every worker placed");
	require(game.integrity(), "scenario setup is consistent");

	std::printf("WHEAT_SETUP pattern=%s seedSpan=%d seeds=%d inns=%d workersPerInn=%d workers=%d ticks=%d sink=%d\n",
		patternName.c_str(), seedSpan, seeds, innCount, workersPerInn, workers, ticks, sink ? 1 : 0);

	const int every = std::getenv("WHEAT_TIMELINE_EVERY") ? std::atoi(std::getenv("WHEAT_TIMELINE_EVERY")) : 0;
	for (int i = 0; i <= ticks; ++i)
	{
		const bool last = i == ticks;
		if (last || (every > 0 && i % every == 0))
		{
			int alive = 0, fetching = 0, harvesting = 0, carrying = 0;
			for (int u = 0; u < Unit::MAX_COUNT; ++u)
				if (const Unit* unit = team->myUnits[u])
				{
					++alive;
					if (unit->activity == Unit::ACT_FILLING && unit->displacement == Unit::DIS_GOING_TO_RESOURCE)
						++fetching;
					else if (unit->displacement == Unit::DIS_HARVESTING)
						++harvesting;
					else if (unit->carriedResource == CORN)
						++carrying;
				}
			int innWheat = 0;
			for (int b = 0; b < Building::MAX_COUNT; ++b)
				if (team->myBuildings[b])
					innWheat += team->myBuildings[b]->resources[CORN];
			int openTiles = 0, openWheat = 0;
			for (int y = FARM_Y0; y <= FARM_Y1; ++y)
				for (int x = FARM_X0; x <= FARM_X1; ++x)
				{
					const Resource& r = game.map.getTile(x, y).resource;
					if (r.type == CORN && !isSeed(x, y, pattern))
					{
						++openTiles;
						openWheat += r.amount;
					}
				}
			if (last)
			{
				require(game.integrity(), "integrity after the run");
				HarvestMetrics::report(game);
			}
			std::printf("%s ticks=%u delivered=%llu alive=%d fetching=%d harvesting=%d carrying=%d innWheat=%d openWheatTiles=%d openWheat=%d checksum=%08x\n",
				last ? "WHEAT_RESULT" : "WHEAT_TICK", game.stepCounter, HarvestMetrics::deliveries(0, CORN),
				alive, fetching, harvesting, carrying, innWheat, openTiles, openWheat, game.checkSum());
		}
		if (last)
			break;

		game.syncStep(0);
		if (sink)
		{
			for (int b = 0; b < Building::MAX_COUNT; ++b)
				if (Building* inn = team->myBuildings[b])
					if (inn->resources[CORN] > 0)
					{
						inn->resources[CORN] = 0;
						inn->update();
					}
			for (int u = 0; u < Unit::MAX_COUNT; ++u)
				if (Unit* unit = team->myUnits[u])
					unit->hungry = fed;
		}
	}
	return 0;
}
