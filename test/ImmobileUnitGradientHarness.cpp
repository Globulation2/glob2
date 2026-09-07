// SPDX-License-Identifier: GPL-3.0-or-later
// Immobile units block the right tile of a building's local gradient, and a
// freshly built map starts without any.
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

	// 64x64 so a local-window index and a map index never coincide.
	World(int bx, int by)
	{
		game.map.setSize(6, 6, GRASS);
		game.map.setGame(&game);
		game.addTeam(0);
		const int typeNum = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
		require(typeNum >= 0, "inn type exists");
		inn = game.addBuilding(bx, by, typeNum, 0);
		require(inn != nullptr, "inn placed");
		game.map.setBuilding(bx, by, inn->type->width, inn->type->height, inn->gid);
	}

	// The local gradient value at map tile (x, y), as Map::pathfindBuilding reads it.
	Uint8 local(int x, int y) const
	{
		const int lx = (x - inn->posX + 15 + 32) & 31;
		const int ly = (y - inn->posY + 15 + 32) & 31;
		return inn->localGradient[0][lx + (ly << 5)];
	}
};

static void freshMapHasNoImmobileUnits()
{
	World world(20, 20);
	for (int y = 0; y < world.game.map.getH(); ++y)
		for (int x = 0; x < world.game.map.getW(); ++x)
			require(!world.game.map.isImmobileUnit(x, y), "a fresh map has no immobile unit anywhere");
	world.game.map.updateLocalGradient(world.inn, false);
	require(!world.inn->locked[0], "the inn is reachable on a fresh map");
	require(world.local(20, 23) > GRADIENT_UNREACHABLE, "open grass near the inn is reachable");
	require(world.local(7, 7) > GRADIENT_UNREACHABLE, "the corner of the local window is reachable");
	std::puts("PASS a fresh map has no immobile units and its local gradients are reachable");
}

static void immobileUnitBlocksItsOwnTile()
{
	World world(20, 20);
	// Map index of (20, 23) is 1492, beyond any local-window index; map index
	// of (2, 1) is 66, which is local (2, 2) = map (7, 7) if read as a window
	// index.
	world.game.map.markImmobileUnit(20, 23, 0);
	world.game.map.markImmobileUnit(2, 1, 0);
	world.game.map.updateLocalGradient(world.inn, false);
	require(world.local(20, 23) == GRADIENT_FORBIDDEN, "the tile with the immobile unit is blocked");
	require(world.local(20, 22) > GRADIENT_UNREACHABLE && world.local(20, 24) > GRADIENT_UNREACHABLE, "its neighbours are not");
	require(world.local(7, 7) > GRADIENT_UNREACHABLE, "a tile whose window index matches a marked map index stays free");
	world.game.map.clearImmobileUnit(20, 23);
	world.game.map.updateLocalGradient(world.inn, false);
	require(world.local(20, 23) > GRADIENT_UNREACHABLE, "clearing the unit frees the tile");
	std::puts("PASS an immobile unit blocks its own tile in the local gradient");
}

// Paints a full-width forbidden row at y, leaving gapX open (gapX < 0: no gap).
static void forbidRow(World& world, int y, int gapX)
{
	BrushAccumulator row;
	for (int x = 0; x < world.game.map.getW(); ++x)
		if (x != gapX)
			row.applyBrush(BrushApplication(x, y, 0), &world.game.map);
	std::shared_ptr<Order> order(new OrderAlterForbidden(0, BrushTool::MODE_ADD, &row, &world.game.map));
	order->sender = 0;
	world.game.executeOrder(order, 0);
}

static void paintingForbiddenAreaRefreshesGradients()
{
	World world(20, 20);
	Team* team = world.game.teams[0];
	world.game.players[0] = new Player(0, "harness", team, BasePlayer::P_LOCAL);
	world.game.gameHeader.setNumberOfPlayers(1);
	// Two forbidden rows enclose y=24..54; the only way to the inn is the gap at (20, 23).
	forbidRow(world, 23, 20);
	forbidRow(world, 55, -1);
	require(world.game.map.isForbidden(19, 23, team->me) && !world.game.map.isForbidden(20, 23, team->me), "the rows are painted with one gap");
	// One unit inside the inn's local window, one far outside it.
	int dist = -1, dx = 0, dy = 0;
	require(world.game.map.buildingAvailable(world.inn, false, 20, 26, &dist), "near the inn the route runs through the gap");
	require(world.game.map.buildingAvailable(world.inn, false, 40, 45, &dist), "far from the inn the route runs through the gap");
	require(world.game.map.pathfindBuilding(world.inn, false, 40, 45, &dx, &dy) && dy == -1, "the far unit heads north");
	forbidRow(world, 23, -1);
	require(!world.game.map.buildingAvailable(world.inn, false, 20, 26, &dist), "closing the gap cuts off the near unit at once");
	require(!world.game.map.buildingAvailable(world.inn, false, 40, 45, &dist), "closing the gap cuts off the far unit at once");
	require(!world.game.map.pathfindBuilding(world.inn, false, 40, 45, &dx, &dy), "the far unit has no route left");
	std::puts("PASS painting forbidden area refreshes building gradients");
}

int main()
{
	GlobalContainer globals;
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	freshMapHasNoImmobileUnits();
	immobileUnitBlocksItsOwnTile();
	paintingForbiddenAreaRefreshesGradients();
	std::puts("Immobile unit gradient regressions passed");
	return 0;
}
