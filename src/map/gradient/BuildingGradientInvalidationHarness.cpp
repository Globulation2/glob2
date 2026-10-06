// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: a building's cached route field must be refreshed
// when the ground it routes over moves, whatever order the sites were placed in,
// whoever placed them, and whether or not the field belongs to something that
// stands on the map at all. The first two scenarios reproduce the "walled-in inn"
// experiment: a 3x3 block of inn sites whose centre can only be reached while the
// ring is incomplete. The last two pin the cases a proximity walk over the changed
// footprint structurally could not reach - another team's buildings, and a virtual
// flag, which is never written into the building tile grid.
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "EngineTiming.h"
#include "Game.h"
#include "GameGUI.h"
#include "Building.h"
#include "BuildingGradientSearch.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "Player.h"
#include "Unit.h"
#include "BasePlayer.h"
#include "Order.h"
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
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

	World(int exponent = 6)
	{
		game.map.setSize(exponent, exponent, GRASS);
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

static void idleFieldStorageIsReusedWithoutStaleRoutes()
{
	World world;
	Building* first = world.place(12, 12);
	Building* second = world.place(44, 44);
	require(second->globalGradient[0] == nullptr, "second building starts without a field");
	const size_t cells = static_cast<size_t>(world.game.map.getW()) * world.game.map.getH();
	const Uint16* firstField = world.game.map.buildingGradient(first, 0);
	require(firstField != nullptr, "first building field exists");
	BuildingGradientSearch* firstSearch = first->globalGradientSearch[0].get();
	require(firstSearch != nullptr, "first building search exists");
	const std::vector<Uint16> expected(firstField, firstField + cells);

	world.tick(768);
	first->freeIdleGradients();
	require(first->globalGradient[0] == nullptr, "idle building drops its field");
	const Uint16* secondField = world.game.map.buildingGradient(second, 0);
	require(secondField == firstField, "another building reuses the idle field storage");
	require(second->globalGradientSearch[0].get() == firstSearch,
		"another building reuses the idle search queues");
	require(std::vector<Uint16>(secondField, secondField + cells) != expected,
		"reused storage contains the second building's field");
	const Uint16* rebuilt = world.game.map.buildingGradient(first, 0);
	require(std::vector<Uint16>(rebuilt, rebuilt + cells) == expected,
		"reactivated building rebuilds the same route values");
	const std::vector<Uint16> secondExpected(secondField, secondField + cells);
	BuildingGradientSearch* secondSearch = second->globalGradientSearch[0].get();
	second->resetPathfindGradients();
	require(second->globalGradient[0] == nullptr, "invalidation drops the old field");
	const Uint16* invalidated = world.game.map.buildingGradient(second, 0);
	require(invalidated == secondField && second->globalGradientSearch[0].get() == secondSearch,
		"invalidation reuses field storage and search queues");
	require(std::vector<Uint16>(invalidated, invalidated + cells) == secondExpected,
		"invalidation rebuilds the same route values");
	std::puts("PASS idle and invalidated field storage is reused without stale routes");
}

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

static void aPausedFieldKeepsItsOriginalObstacles()
{
	World world;
	Building *centre = world.place(20, 20);
	require(world.available(centre, 18, 20), "nearby query succeeds on a lazy field");
	require(centre->globalGradientSearch[0] && !centre->globalGradientSearch[0]->complete(),
		"nearby query leaves a retained frontier");
	world.placeRing(20, 20, 1);
	// Cached gradients retain their pre-edit obstacle snapshot until the
	// normal refresh deadline, including while their search is paused.
	require(world.available(centre, 45, 45), "paused field preserves its old route before refresh is due");
	world.tick(DIRTY_GRACE_TICKS);
	require(!world.available(centre, 45, 45), "normal refresh replaces the paused obstacle snapshot");
	std::puts("PASS a paused field freezes obstacles until the existing refresh deadline");
}

// Public point APIs must resolve for their caller, while the array API must
// finish the whole field. Compare movement and distance against a complete field.
static void publicReadsResolveTheirInputs()
{
	World world;
	Map &map = world.game.map;
	Building *centre = world.place(20, 20);
	for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
	{
		const Uint16 *full = map.buildingGradient(centre, swim);
		require(full != nullptr, "public field is reachable");
		std::vector<Uint16> expected(full, full + map.getW() * map.getH());
		int expectedDist, expectedDx, expectedDy;
		require(map.buildingAvailable(centre, swim, 18, 20, &expectedDist), "complete distance exists");
		require(map.pathfindBuilding(centre, swim, 18, 20, &expectedDx, &expectedDy), "complete direction exists");
		map.updateGlobalGradient(centre, swim);
		int dist, dx, dy;
		require(map.buildingAvailable(centre, swim, 18, 20, &dist) && dist == expectedDist,
			"point API resolves its own distance");
		require(!centre->globalGradientSearch[swim]->complete(), "point read does not finish the field");
		map.updateGlobalGradient(centre, swim);
		require(map.pathfindBuilding(centre, swim, 18, 20, &dx, &dy) && dx == expectedDx && dy == expectedDy,
			"movement API resolves its own input layer");
		require(!centre->globalGradientSearch[swim]->complete(), "movement does not finish the field");
		full = map.buildingGradient(centre, swim);
		require(centre->globalGradientSearch[swim]->complete(), "public array API completes the field");
		require(std::vector<Uint16>(full, full + expected.size()) == expected, "public array is fully resolved");
	}
	std::puts("PASS public distance, movement and full-field lazy API boundaries");
}

static void parallelFields()
{
	World world(7);
	Map &map = world.game.map;
	Building *building = world.place(20, 20);
	Building *flag = world.placeFlag(40, 40, 5);
	for (int x = 50; x < 65; ++x)
	{
		map.setTerrain(x, 60, 256);
		map.addForbidden(x, 62, 0);
		map.addGuardArea(x, 64, 0);
		map.addClearArea(x, 66, 0);
	}
	const size_t cells = map.getW() * map.getH();
	std::vector<std::vector<Uint16>> expected;
	auto capture = [&] {
		std::vector<std::vector<Uint16>> fields;
		for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
		{
			for (auto *b : {building, flag})
			{
				map.buildingGradient(b, swim);
				map.updateGlobalGradient(b, swim);
				const auto *field = map.buildingGradient(b, swim);
				require(field != nullptr, "parallel test building reachable");
				fields.emplace_back(field, field + cells);
			}
			for (const auto *field : {map.getForbiddenGradient(0, swim), map.getGuardAreasGradient(0, swim), map.getClearAreasGradient(0, swim)})
				fields.emplace_back(field, field + cells);
		}
		return fields;
	};
	expected = capture();
	for (unsigned threads : {1, 2, 4, 8})
	{
		map.configureCompute(threads, 7);
		map.updateTeamAreaGradients(0);
		require(capture() == expected, "parallel area/building initialization preserves all fields");
	}
	// Exercise the hiring prepass with two independently owned weighted fields.
	Unit *a = world.game.addUnit(10, 10, 0, WORKER, 0, 0, 0, 0);
	Unit *b = world.game.addUnit(12, 10, 0, WORKER, 0, 0, 0, 0);
	require(a && b, "hiring test units created");
	for (auto *unit : {a, b}) { unit->activity = Unit::ACT_RANDOM; unit->medical = Unit::MED_FREE; unit->performance[HARVEST] = 1; unit->performance[WALK] = 10; }
	a->performance[SWIM] = 0; b->performance[SWIM] = 20;
	map.updateGlobalGradient(building, a->swimClass());
	map.updateGlobalGradient(building, b->swimClass());
	const auto used = building->globalGradientUsedStep[a->swimClass()];
	map.advanceHiringGradients(building);
	require(building->globalGradientUsedStep[a->swimClass()] == used, "prepass does not touch use timestamps");
	for (auto *unit : {a, b})
	{
		const int swim = unit->swimClass();
		const auto *field = map.buildingGradient(building, swim);
		require(std::vector<Uint16>(field, field + cells) == expected[swim * 5], "hiring advancement preserves frozen fields");
	}
	std::puts("PASS parallel area batches, seed initialization, frozen hiring advancement");
}

static void delayedFields()
{
	for (unsigned workers : {0, 1, 2, 4, 8}) for (int kind=0; kind<3; ++kind)
	{
		World world(7);
		Map &map=world.game.map;
		map.setTerrain(55, 55, 256);
		map.setResource(30, 30, 0, 1);
		map.addGuardArea(40, 40, 0);
		map.addClearArea(30, 30, 0);
		const int swim=1;
		auto field=[&]() { return kind==0 ? map.getMaterialGradient(0, 0, swim)
			: kind==1 ? map.getGuardAreasGradient(0, swim) : map.getClearAreasGradient(0, swim); };
		auto refresh=[&]() { if(kind==0) map.updateMaterialGradient(0, 0, swim);
			else if(kind==1) map.updateGuardAreasGradient(0, swim); else map.updateClearAreasGradient(0, swim); };
		const auto cells=map.getW()*map.getH();
		field();
		map.configureGradientPipeline(workers, 3);
		map.advanceGradientPipeline(); map.syncStep(0); // Seed the only allocated periodic slot.
		require(map.gradientPipelineStatus().jobs==1, "pipeline scheduled a real field");
		map.addForbidden(41, 40, 0);
		refresh();
		const std::vector<Uint16> expected(field(),field()+cells);
		map.advanceGradientPipeline(); map.advanceGradientPipeline(); map.advanceGradientPipeline();
		require(map.gradientPipelineStatus().discarded==1, "synchronous refresh supersedes queued snapshot");
		require(std::vector<Uint16>(field(),field()+cells)==expected, "old field cannot overwrite fresh synchronous field");
		// A subsequent periodic snapshot publishes normally at its fixed deadline.
		map.syncStep(1);
		map.advanceGradientPipeline(); map.advanceGradientPipeline();
		require(map.gradientPipelineStatus().published==0, "no early publication");
		map.advanceGradientPipeline();
		require(map.gradientPipelineStatus().published==1, "publication at deadline");
		map.syncStep(2);
		std::vector<Uint16> frozen(cells);
		if(kind==0) map.seedMaterialGradient(0, 0, swim, frozen.data());
		else if(kind==1) map.seedGuardAreasGradient(0, swim, frozen.data());
		else map.seedClearAreasGradient(0, swim, frozen.data());
		map.propagateGradient(frozen.data(), swim);
		map.setTerrain(55, 55, 0); // Workers must use captured water, not this live edit.
		map.advanceGradientPipeline(); map.advanceGradientPipeline(); map.advanceGradientPipeline();
		require(std::vector<Uint16>(field(),field()+cells)==frozen, "terrain changes do not alter a pending snapshot");
		map.syncStep(3); // Destruction must safely drain a job in flight.
	}
	std::puts("PASS delayed resource/guard/clear publication, synchronous supersession, teardown");
}
}

TEST_SUITE("BuildingGradientInvalidation")
{
	TEST_CASE("centre placed inside an existing ring")
	{
		glob2test::HeadlessGlobals globals;
		centrePlacedInsideAnExistingRing();
	}
	TEST_CASE("ring placed around an existing field")
	{
		glob2test::HeadlessGlobals globals;
		ringPlacedAroundAnExistingField();
	}
	TEST_CASE("a rival teams ring cuts off a cached field")
	{
		glob2test::HeadlessGlobals globals;
		aRivalTeamsRingCutsOffACachedField();
	}
	TEST_CASE("a ring cuts off a virtual flags field")
	{
		glob2test::HeadlessGlobals globals;
		aRingCutsOffAVirtualFlagsField();
	}
	TEST_CASE("a paused field keeps its original obstacles")
	{
		glob2test::HeadlessGlobals globals;
		aPausedFieldKeepsItsOriginalObstacles();
	}
	TEST_CASE("public reads resolve their inputs")
	{
		glob2test::HeadlessGlobals globals;
		publicReadsResolveTheirInputs();
	}
	TEST_CASE("parallel fields")
	{
		glob2test::HeadlessGlobals globals;
		parallelFields();
	}
	TEST_CASE("delayed fields")
	{
		glob2test::HeadlessGlobals globals;
		delayedFields();
	}
	TEST_CASE("idle field storage is reused without stale routes")
	{
		glob2test::HeadlessGlobals globals;
		idleFieldStorageIsReusedWithoutStaleRoutes();
	}
}
