// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: a building's cached route field must be refreshed
// when a building goes up or comes down next to it, whatever order the sites
// were placed in. Reproduces the "walled-in inn" experiment: a 3x3 block of inn
// sites whose centre can only be reached while the ring is incomplete.
#include "GlobalContainer.h"
#include "EngineTiming.h"
#include "Game.h"
#include "GameGUI.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "Player.h"
#include "BasePlayer.h"
#include "Order.h"
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// An invalidated field is rebuilt on its next use once GRADIENT_DIRTY_REBUILD_TICKS
// have passed since it was built. The harness lets that interval elapse before
// judging, so a fix that flags the field and one that drops it both count. Taking
// the engine's own constant rather than a copy: when it was raised from 25 to 100
// a local copy here silently stopped covering the interval and the regression
// started passing stale fields.
static const Uint32 DIRTY_GRACE_TICKS = GRADIENT_DIRTY_REBUILD_TICKS;

struct World
{
	GameGUI gui;
	Game& game = gui.game;
	Team* team = nullptr;
	int siteType = -1;
	int siteW = 0, siteH = 0;

	World()
	{
		game.map.setSize(6, 6, GRASS);
		game.map.setGame(&game);
		game.addTeam(0);
		team = game.teams[0];
		game.players[0] = new Player(0, "harness", team, BasePlayer::P_LOCAL);
		game.gameHeader.setNumberOfPlayers(1);
		// Placement through an order needs the site to be on discovered ground.
		game.map.setMapDiscovered();
		siteType = globalContainer->buildingsTypes.getTypeNum("inn", 0, true);
		require(siteType >= 0, "inn construction site type exists");
		const BuildingType* type = globalContainer->buildingsTypes.get(siteType);
		siteW = type->width;
		siteH = type->height;
	}

	// The player's path: OrderCreate -> Game::executeCreate -> Game::addBuilding.
	Building* place(int x, int y)
	{
		std::shared_ptr<Order> order(new OrderCreate(0, x, y, siteType, 1, 1));
		order->sender = 0;
		game.executeOrder(order, 0);
		Uint16 gid = game.map.getBuilding(x, y);
		require(gid != NOGBID, "construction site placed through the create order");
		return team->myBuildings[Building::GIDtoID(gid)];
	}

	// The player's path: OrderDelete -> launchDelete -> Team::syncStep clears it.
	void remove(const std::vector<Building*>& sites)
	{
		for (Building* b : sites)
		{
			std::shared_ptr<Order> order(new OrderDelete(b->gid));
			order->sender = 0;
			game.executeOrder(order, 0);
		}
		team->syncStep();
	}

	// Eight sites packed around (cx,cy) so the centre has no free neighbour.
	std::vector<Building*> placeRing(int cx, int cy)
	{
		std::vector<Building*> ring;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (dx != 0 || dy != 0)
					ring.push_back(place(cx + dx * siteW, cy + dy * siteH));
		return ring;
	}

	bool available(Building* b, int x, int y)
	{
		int dist = -1;
		return game.map.buildingAvailable(b, 0, x, y, &dist);
	}

	void tick(Uint32 n) { game.stepCounter += n; }
};

// Leo's experiment on feat/task-switch-penalty: the centre site is placed first,
// so its field exists (units were already being offered it) when the ring closes.
static void ringPlacedAroundAnExistingField()
{
	World world;
	Building* centre = world.place(20, 20);
	require(world.available(centre, 10, 10), "an open site is offered to a unit outside");
	require(centre->globalGradient[0] != nullptr, "the centre's field is cached before the ring goes up");

	std::vector<Building*> ring = world.placeRing(20, 20);
	world.tick(DIRTY_GRACE_TICKS);
	require(!world.available(centre, 10, 10), "a site walled in by new construction is no longer offered to a unit outside");
	require(!world.available(centre, 40, 45), "nor to a distant unit");

	world.remove(ring);
	require(world.available(centre, 10, 10), "clearing the ring makes the site available at once");
	std::puts("PASS a ring placed around a cached field cuts it off, and clearing the ring restores it");
}

// Leo's experiment on master: the ring stands before the centre is placed, so
// the centre's field is first built after the wall closed.
static void centrePlacedInsideAnExistingRing()
{
	World world;
	std::vector<Building*> ring = world.placeRing(20, 20);
	Building* centre = world.place(20, 20);
	require(!world.available(centre, 10, 10), "a site placed inside a ring is never offered to a unit outside");

	world.remove(ring);
	require(world.available(centre, 10, 10), "clearing the ring makes the site available at once");
	std::puts("PASS a site placed inside a ring is unreachable until the ring is cleared");
}

int main(int argc, char** argv)
{
	const char* scenario = argc > 1 ? argv[1] : "all";
	require(argc <= 2 && (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "ring-after") == 0 || std::strcmp(scenario, "ring-before") == 0), "expected all, ring-after or ring-before");
	GlobalContainer globals;
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	if (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "ring-before") == 0) centrePlacedInsideAnExistingRing();
	if (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "ring-after") == 0) ringPlacedAroundAnExistingField();
	std::puts("Building gradient invalidation regressions passed");
	return 0;
}
