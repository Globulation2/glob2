// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: a building's cached route field must be refreshed
// when the ground it routes over moves, whatever order the sites were placed in,
// whoever placed them, and whether or not the field belongs to something that
// stands on the map at all. The first two scenarios reproduce the "walled-in inn"
// experiment: a 3x3 block of inn sites whose centre can only be reached while the
// ring is incomplete. The last two pin the cases a proximity walk over the changed
// footprint structurally could not reach - another team's buildings, and a virtual
// flag, which is never written into the building tile grid.
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
	// Two teams, so a ring can be built by someone other than the field's owner.
	// One player per team; the player index doubles as the team number.
	Team* team = nullptr;
	int siteType = -1;
	int flagType = -1;
	int siteW = 0, siteH = 0;

	World()
	{
		game.map.setSize(6, 6, GRASS);
		game.map.setGame(&game);
		game.addTeam(0);
		game.addTeam(1);
		team = game.teams[0];
		game.players[0] = new Player(0, "harness", team, BasePlayer::P_LOCAL);
		game.players[1] = new Player(1, "rival", game.teams[1], BasePlayer::P_LOCAL);
		game.gameHeader.setNumberOfPlayers(2);
		// Placement through an order needs the site to be on discovered ground.
		game.map.setMapDiscovered();
		siteType = globalContainer->buildingsTypes.getTypeNum("inn", 0, true);
		require(siteType >= 0, "inn construction site type exists");
		const BuildingType* type = globalContainer->buildingsTypes.get(siteType);
		siteW = type->width;
		siteH = type->height;
		flagType = globalContainer->buildingsTypes.getTypeNum("explorationflag", 0, false);
		require(flagType >= 0, "exploration flag type exists");
	}

	// The player's path: OrderCreate -> Game::executeCreate -> Game::addBuilding.
	Building* place(int x, int y, int teamNumber = 0)
	{
		std::shared_ptr<Order> order(new OrderCreate(teamNumber, x, y, siteType, 1, 1));
		order->sender = teamNumber;
		game.executeOrder(order, 0);
		Uint16 gid = game.map.getBuilding(x, y);
		require(gid != NOGBID, "construction site placed through the create order");
		require(Building::GIDtoTeam(gid) == teamNumber, "the site belongs to the team that ordered it");
		return game.teams[teamNumber]->myBuildings[Building::GIDtoID(gid)];
	}

	// Same path, for a virtual building. A flag never reaches the building tile
	// grid, so there is no gid on the map to look it up by - find it among the
	// owner's buildings instead. radius keeps the flag's goal disc inside the ring.
	Building* placeFlag(int x, int y, int radius, int teamNumber = 0)
	{
		std::shared_ptr<Order> order(new OrderCreate(teamNumber, x, y, flagType, 1, 1, radius));
		order->sender = teamNumber;
		game.executeOrder(order, 0);
		require(game.map.getBuilding(x, y) == NOGBID, "a flag is not written into the building tile grid");
		Team* owner = game.teams[teamNumber];
		for (int id = 0; id < Building::MAX_COUNT; ++id)
		{
			Building* b = owner->myBuildings[id];
			if (b && b->type->isVirtual && b->posX == x && b->posY == y)
				return b;
		}
		require(false, "exploration flag placed through the create order");
		return nullptr;
	}

	// The player's path: OrderDelete -> launchDelete -> Team::syncStep clears it.
	void remove(const std::vector<Building*>& sites, int teamNumber = 0)
	{
		for (Building* b : sites)
		{
			std::shared_ptr<Order> order(new OrderDelete(b->gid));
			order->sender = teamNumber;
			game.executeOrder(order, 0);
		}
		game.teams[teamNumber]->syncStep();
	}

	// Eight sites packed around (cx,cy) so the centre has no free neighbour.
	std::vector<Building*> placeRing(int cx, int cy, int teamNumber = 0)
	{
		std::vector<Building*> ring;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (dx != 0 || dy != 0)
					ring.push_back(place(cx + dx * siteW, cy + dy * siteH, teamNumber));
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

// The invalidation this replaced walked the changed footprint and dirtied only
// the buildings of the team that changed it. Another team's construction is
// exactly as solid, and the field it cuts off belongs to someone else.
static void aRivalTeamsRingCutsOffACachedField()
{
	World world;
	Building* centre = world.place(20, 20);
	require(world.available(centre, 10, 10), "an open site is offered to a unit outside");
	require(centre->globalGradient[0] != nullptr, "the centre's field is cached before the ring goes up");

	std::vector<Building*> ring = world.placeRing(20, 20, 1);
	world.tick(DIRTY_GRACE_TICKS);
	require(!world.available(centre, 10, 10), "a site walled in by another team is no longer offered to a unit outside");

	// Clearing it is the same story in reverse, with one difference worth pinning:
	// Team::syncStep frees the fields of the team that demolished, so the owner's
	// field is not freed here, only invalidated. It comes back on the next rebuild
	// the interval allows rather than on the next lookup.
	world.remove(ring, 1);
	world.tick(DIRTY_GRACE_TICKS);
	require(world.available(centre, 10, 10), "clearing the other team's ring makes the site available again");
	std::puts("PASS a ring built by another team cuts off a cached field, and clearing it restores the field");
}

// A virtual flag is never written into the building tile grid, so walking the
// changed footprint could not discover its field however close the change was.
// The flag's goal disc is kept inside the ring, so once the ring closes there is
// no way in and no goal outside.
static void aRingCutsOffAVirtualFlagsField()
{
	World world;
	Building* flag = world.placeFlag(20, 20, 1);
	require(world.available(flag, 10, 10), "an open flag is offered to a unit outside");
	require(flag->globalGradient[0] != nullptr, "the flag's field is cached before the ring goes up");

	std::vector<Building*> ring = world.placeRing(20, 20);
	world.tick(DIRTY_GRACE_TICKS);
	require(!world.available(flag, 10, 10), "a flag walled in by new construction is no longer offered to a unit outside");

	world.remove(ring);
	require(world.available(flag, 10, 10), "clearing the ring makes the flag available at once");
	std::puts("PASS a ring around a virtual flag cuts off its field, though no flag is in the building tile grid");
}

int main(int argc, char** argv)
{
	const char* scenario = argc > 1 ? argv[1] : "all";
	require(argc <= 2 && (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "ring-after") == 0
		|| std::strcmp(scenario, "ring-before") == 0 || std::strcmp(scenario, "ring-other-team") == 0
		|| std::strcmp(scenario, "ring-flag") == 0), "expected all, ring-after, ring-before, ring-other-team or ring-flag");
	GlobalContainer globals;
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	if (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "ring-before") == 0) centrePlacedInsideAnExistingRing();
	if (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "ring-after") == 0) ringPlacedAroundAnExistingField();
	if (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "ring-other-team") == 0) aRivalTeamsRingCutsOffACachedField();
	if (std::strcmp(scenario, "all") == 0 || std::strcmp(scenario, "ring-flag") == 0) aRingCutsOffAVirtualFlagsField();
	std::puts("Building gradient invalidation regressions passed");
	return 0;
}
