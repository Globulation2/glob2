// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression for immobile bookkeeping and weighted building routes.
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "MapInternal.h"
#include "Race.h"
#include "Player.h"
#include "BasePlayer.h"
#include "Brush.h"
#include "Order.h"
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct World
{
	GameGUI gui;
	Game& game = gui.game;
	Building* inn = nullptr;

	World()
	{
		game.map.setSize(6, 6, GRASS);
		game.map.setGame(&game);
		game.addTeam(0);
		const int typeNum = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
		require(typeNum >= 0, "inn type exists");
		inn = game.addBuilding(20, 20, typeNum, 0);
		require(inn != nullptr, "inn placed");
		game.map.setBuilding(20, 20, inn->type->width, inn->type->height, inn->gid);
	}

	void clearOccupancy()
	{
		// Isolate later scenarios from the initialization fix when testing the base.
		for (int y = 0; y < game.map.getH(); ++y)
			for (int x = 0; x < game.map.getW(); ++x)
				game.map.clearImmobileUnit(x, y);
	}

	Uint16 value(int swimClass, int x, int y)
	{
		const Uint16* gradient = game.map.buildingGradient(inn, swimClass);
		require(gradient != nullptr, "inn has an accessible entrance");
		return gradient[game.map.coordToIndex(x, y)];
	}
};

static void freshMapHasNoImmobileUnits()
{
	World world;
	for (int y = 0; y < world.game.map.getH(); ++y)
		for (int x = 0; x < world.game.map.getW(); ++x)
			require(!world.game.map.isImmobileUnit(x, y), "a fresh map has no immobile unit anywhere");
	for (int c = 0; c < SWIM_CLASS_COUNT; ++c)
	{
		require(world.value(c, 20, 23) > GRADIENT_UNREACHABLE, "open grass near the inn is reachable");
		require(world.value(c, 40, 45) > GRADIENT_UNREACHABLE, "distant grass is reachable");
	}
	std::puts("PASS fresh map occupancy and weighted routes for every swim class");
}

static void immobileUnitBlocksItsOwnTile()
{
	World world;
	world.clearOccupancy();
	world.game.map.markImmobileUnit(20, 23, 0);
	world.game.map.markImmobileUnit(2, 1, 0);
	for (int c = 0; c < SWIM_CLASS_COUNT; ++c)
	{
		require(world.value(c, 20, 23) == GRADIENT_FORBIDDEN, "the immobile unit's tile is blocked");
		require(world.value(c, 2, 1) == GRADIENT_FORBIDDEN, "the distant immobile unit's tile is blocked");
		require(world.value(c, 20, 22) > GRADIENT_UNREACHABLE && world.value(c, 20, 24) > GRADIENT_UNREACHABLE, "neighbours remain free");
		require(world.value(c, 7, 7) > GRADIENT_UNREACHABLE, "unrelated tile remains free");
	}
	world.game.map.clearImmobileUnit(20, 23);
	world.inn->resetPathfindGradients();
	for (int c = 0; c < SWIM_CLASS_COUNT; ++c)
		require(world.value(c, 20, 23) > GRADIENT_UNREACHABLE, "clearing the unit frees its tile on rebuild");
	std::puts("PASS immobile units block their own weighted-gradient cells");
}

static void alterForbidden(World& world, BrushTool::Mode mode, BrushAccumulator& brush)
{
	std::shared_ptr<Order> order(new OrderAlterForbidden(0, mode, &brush, &world.game.map));
	order->sender = 0;
	world.game.executeOrder(order, 0);
}

// Two full-width rows are needed to enclose an area on the toroidal map.
static void forbidRow(World& world, int y, int gapX)
{
	BrushAccumulator row;
	for (int x = 0; x < world.game.map.getW(); ++x)
		if (x != gapX)
			row.applyBrush(BrushApplication(x, y, 0), &world.game.map);
	alterForbidden(world, BrushTool::MODE_ADD, row);
}

static void paintingForbiddenAreaRefreshesGradients()
{
	World world;
	world.clearOccupancy();
	Team* team = world.game.teams[0];
	world.game.players[0] = new Player(0, "harness", team, BasePlayer::P_LOCAL);
	world.game.gameHeader.setNumberOfPlayers(1);
	forbidRow(world, 23, 20);
	forbidRow(world, 55, -1);
	require(world.game.map.isForbidden(19, 23, team->me) && !world.game.map.isForbidden(20, 23, team->me), "painted rows leave one gap");
	int dist = -1, dx = 0, dy = 0;
	for (int c = 0; c < SWIM_CLASS_COUNT; ++c)
	{
		require(world.game.map.buildingAvailable(world.inn, c, 20, 26, &dist), "near route uses the gap");
		require(world.game.map.buildingAvailable(world.inn, c, 40, 45, &dist), "distant route uses the gap");
		require(world.game.map.pathfindBuilding(world.inn, c, 40, 45, &dx, &dy), "distant unit can advance");
	}
	forbidRow(world, 23, -1);
	for (int c = 0; c < SWIM_CLASS_COUNT; ++c)
	{
		require(world.inn->globalGradient[c] == nullptr, "painting invalidates every cached swim class immediately");
		require(!world.game.map.buildingAvailable(world.inn, c, 20, 26, &dist), "closing the gap cuts off the near route");
		require(!world.game.map.buildingAvailable(world.inn, c, 40, 45, &dist), "closing the gap cuts off the distant route");
		require(!world.game.map.pathfindBuilding(world.inn, c, 40, 45, &dx, &dy), "distant unit has no route left");
	}
	BrushAccumulator gap;
	gap.applyBrush(BrushApplication(20, 23, 0), &world.game.map);
	alterForbidden(world, BrushTool::MODE_DEL, gap);
	for (int c = 0; c < SWIM_CLASS_COUNT; ++c)
	{
		require(world.inn->globalGradient[c] == nullptr, "erasing invalidates every cached swim class immediately");
		require(world.game.map.buildingAvailable(world.inn, c, 20, 26, &dist), "erasing restores the near route");
		require(world.game.map.buildingAvailable(world.inn, c, 40, 45, &dist), "erasing restores the distant route");
	}
	std::puts("PASS painting and erasing refresh all weighted building routes");
}

int main(int argc, char** argv)
{
	const char* scenario = argc > 1 ? argv[1] : "all";
	require(argc <= 2 && (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "fresh") == 0 || std::strcmp(scenario, "occupancy") == 0 || std::strcmp(scenario, "forbidden") == 0), "expected all, fresh, occupancy or forbidden");
	GlobalContainer globals;
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	if (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "fresh") == 0) freshMapHasNoImmobileUnits();
	if (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "occupancy") == 0) immobileUnitBlocksItsOwnTile();
	if (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "forbidden") == 0) paintingForbiddenAreaRefreshesGradients();
	std::puts("Immobile unit gradient regressions passed");
	return 0;
}
