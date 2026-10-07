// SPDX-License-Identifier: GPL-3.0-or-later
// Builds petri_construction.map: five AINone colonies, one per island, each
// short of workers and each asking one question about construction under
// that shortage. Written to $GLOB2_PETRI_OUT; skipped when it is unset.
//
//  team 0  feed and fortify  2 inns to keep stocked, 4 defence tower sites
//  team 1  finish vs start   4 barracks sites round one forest, one 5/7 built
//  team 2  near and far      4 barracks sites at 8, 16, 24, 32 tiles from wood
//  team 3  two shores        main island with 2 part-built sites, and an islet
//                            with 3 workers, wheat, wood and a fresh inn site
//  team 4  priority          3 sites by the wood, 1 high-priority site far off
//
// Every island has a school, which takes in any worker left idle.
#include "EngineFixtures.h"
#include "GlobalContainer.h"
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
#include "Utilities.h"
#include "MapHeader.h"
#include "Player.h"
#include "AI.h"
#include <BinaryStream.h>
#include <GzipUtil.h>
#include <StreamBackend.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
}

struct Builder
{
	Game& game;
	explicit Builder(Game& g) : game(g) {}

	void land(int x0, int y0, int w, int h)
	{
		for (int y = y0; y < y0 + h; ++y)
			for (int x = x0; x < x0 + w; ++x)
				game.map.setUMTerrain(x, y, GRASS);
	}
	// A filled rectangle of a map resource (trees, wheat, stone).
	void patch(int x0, int y0, int w, int h, int resource)
	{
		for (int y = y0; y < y0 + h; ++y)
			for (int x = x0; x < x0 + w; ++x)
				game.map.setResourceByIndex(x, y, resource, 0);
	}
	Building* finished(int x, int y, const char* type, int team, int workers)
	{
		const Sint32 t = globalContainer->buildingsTypes.getTypeNum(type, 0, false);
		require(t >= 0, "finished building type exists");
		Building* b = game.addBuilding(x, y, t, team, workers, workers);
		require(b != nullptr, "place a finished building");
		return b;
	}
	Building* site(int x, int y, const char* type, int team, int workers, int woodDelivered = 0)
	{
		const Sint32 t = globalContainer->buildingsTypes.getTypeNum(type, 0, true);
		require(t >= 0, "building site type exists");
		Building* b = game.addBuilding(x, y, t, team, workers, workers);
		require(b != nullptr, "place a building site");
		if (woodDelivered)
			b->materials[WOOD] = woodDelivered;
		return b;
	}
	void workers(int x0, int y0, int w, int h, int team, int count)
	{
		int placed = 0;
		for (int y = y0; y < y0 + h && placed < count; ++y)
			for (int x = x0; x < x0 + w && placed < count; ++x)
				if (game.map.isFreeForGroundUnit(x, y, false, 1u << team)
					&& game.addUnit(x, y, team, WORKER, 0, 0, 0, 0))
					++placed;
		require(placed == count, "place every worker");
	}
	// The colony's home: inns with a wheat field 30 tiles off, a school, workers.
	void home(int x, int y, int team, int inns, int count)
	{
		for (int i = 0; i < inns; ++i)
			finished(x + 4 * i, y, "inn", team, 2);
		patch(x + 30, y - 2, 6, 4 * inns + 6, WHEAT);
		finished(x + 4 * inns + 2, y, "school", team, 1);
		workers(x - 3, y + 4, 4 * inns + 8, 4, team, count);
	}
};

// Island origins on a 256x256 map; each island is 70 by 100 tiles of grass.
constexpr int IW = 70, IH = 100;
constexpr int ISLANDS[5][2] = {{8, 10}, {93, 10}, {178, 10}, {8, 140}, {130, 140}};

static void buildPetriConstruction(const std::string& path)
{
	GameGUI gui;
	Game& game = gui.game;
	setSyncRandSeed(2026);
	game.gameHeader.setRandomSeed(2026);
	game.map.setSize(8, 8, WATER);
	game.map.setGame(&game);
	Builder b(game);
	for (const auto& at : ISLANDS)
		b.land(at[0], at[1], IW, IH);
	// Team 3's islet, separated by 12 tiles of water from its main island.
	b.land(90, 165, 26, 26);
	game.map.controlSand();
	game.map.rebuildTerrain();
	for (int t = 0; t < 5; ++t)
	{
		game.addTeam();
		game.teams[t]->race.loadDefault();
	}

	// Each island: 8 workers for 8 sites, a school to take in anyone idle,
	// and the inns' wheat 20 tiles from the inns so feeding competes for hands.
	// Team 0: feed and fortify. Two inns to keep stocked while eight defence
	// towers go up by a forest 30 tiles off; finished towers then want stone,
	// 15 tiles further.
	{
		const int X = ISLANDS[0][0], Y = ISLANDS[0][1];
		b.home(X + 10, Y + 10, 0, 2, 8);
		for (int i = 0; i < 8; ++i)
			b.site(X + 30 + 4 * (i % 4), Y + 50 + 4 * (i / 4), "defencetower", 0, 3);
		b.patch(X + 50, Y + 50, 8, 12, WOOD);
		b.patch(X + 30, Y + 80, 12, 6, STONE);
	}
	// Team 1: finish vs start. Eight barracks round one forest, all about 20
	// tiles from it; two already hold 5 of their 7 wood.
	{
		const int X = ISLANDS[1][0], Y = ISLANDS[1][1];
		b.home(X + 10, Y + 10, 1, 1, 8);
		const int cx = X + 35, cy = Y + 50;
		b.patch(cx - 4, cy - 7, 8, 14, WOOD);
		const int ring[8][2] = {{-2, -24}, {14, -18}, {20, -2}, {14, 14}, {-2, 20}, {-18, 14}, {-24, -2}, {-18, -18}};
		for (int i = 0; i < 8; ++i)
			b.site(cx + ring[i][0], cy + ring[i][1], "barracks", 1, 3, i % 4 == 0 ? 5 : 0);
	}
	// Team 2: near and far. Eight barracks in a row, 8 to 43 tiles from the
	// forest.
	{
		const int X = ISLANDS[2][0], Y = ISLANDS[2][1];
		b.home(X + 10, Y + 10, 2, 1, 8);
		b.patch(X + 4, Y + 50, 6, 14, WOOD);
		for (int i = 0; i < 8; ++i)
			b.site(X + 18 + 5 * i, Y + 52, "barracks", 2, 3);
	}
	// Team 3: two shores. Eight part-built barracks on the main island; on
	// the islet three workers have wheat and wood but no inn, only its site.
	{
		const int X = ISLANDS[3][0], Y = ISLANDS[3][1];
		b.home(X + 10, Y + 10, 3, 1, 8);
		b.patch(X + 50, Y + 50, 8, 10, WOOD);
		for (int i = 0; i < 8; ++i)
			b.site(X + 20 + 6 * (i % 4), Y + 45 + 8 * (i / 4), "barracks", 3, 3, 3);
		b.site(98, 170, "inn", 3, 2);
		b.patch(94, 180, 6, 4, WHEAT);
		b.patch(106, 180, 6, 4, WOOD);
		b.workers(100, 174, 6, 2, 3, 3);
	}
	// Team 4: priority. Eight barracks by the forest; one high-priority
	// barracks 45 tiles away with a forest of its own.
	{
		const int X = ISLANDS[4][0], Y = ISLANDS[4][1];
		b.home(X + 10, Y + 10, 4, 1, 8);
		b.patch(X + 8, Y + 40, 10, 6, WOOD);
		for (int i = 0; i < 8; ++i)
			b.site(X + 6 + 6 * (i % 4), Y + 50 + 6 * (i / 4), "barracks", 4, 3);
		Building* urgent = b.site(X + 50, Y + 85, "barracks", 4, 3);
		urgent->priority = 1;
		urgent->updateCallLists();
		b.patch(X + 58, Y + 78, 6, 6, WOOD);
	}

	// One AINone player per team, as petri2 has, so the file loads as a game.
	GameHeader header = game.gameHeader;
	header.setNumberOfPlayers(5);
	for (int p = 0; p < 5; ++p)
		header.getBasePlayer(p) = BasePlayer(p, "AINone", p, BasePlayer::playerTypeFromImplementationID(AI::NONE));
	game.setGameHeader(header);

	if (std::getenv("GLOB2_PETRI_DEBUG"))
		for (int t = 0; t < 5; ++t)
		{
			for (int u = 0; u < Unit::MAX_COUNT; ++u)
				if (Unit* unit = game.teams[t]->myUnits[u]) { unit->activity = Unit::ACT_RANDOM; unit->medical = Unit::MED_FREE; }
			game.teams[t]->updateAllBuildingTasks();
			for (int i = 0; i < Building::MAX_COUNT; ++i)
				if (Building* bd = game.teams[t]->myBuildings[i])
				{
					std::printf("DBG team %d %s at %d,%d working=%zu desired=%d fail:", t, bd->type->type.c_str(), bd->posX, bd->posY, bd->unitsWorking.size(), bd->desiredMaxUnitWorking);
					for (int r = 0; r < Building::UnitCantWorkReasonSize; ++r) std::printf(" %u", bd->unitsFailingRequirements[r]);
					std::printf("\n");
				}
		}

	auto* backend = new GAGCore::MemoryStreamBackend();
	std::string contents;
	{
		GAGCore::BinaryOutputStream stream(backend);
		game.save(&stream, true, "petri_construction");
		contents = backend->takeContents();
	}
	require(GAGCore::writeGzipAtomicToPath(glob2GzipWritePath(path), contents), "write the map");
	std::printf("wrote %s\n", glob2GzipWritePath(path).c_str());
}
}

TEST_SUITE("PetriConstruction")
{
	TEST_CASE("build the petri_construction bed")
	{
		const char* out = std::getenv("GLOB2_PETRI_OUT");
		if (!out)
			return;
		glob2test::HeadlessGlobals globals;
		buildPetriConstruction(out);
	}
}
