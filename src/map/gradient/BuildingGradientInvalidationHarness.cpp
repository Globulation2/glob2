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
#include "GradientRuntime.h"
#include "Version.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <Stream.h>
#include <memory>
#include <map>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <queue>
#include <random>
#include <climits>

namespace
{
template<class Edit> void editTile(Map &map,int x,int y,Edit edit) { auto tile=map.getTile(x,y); edit(tile); map.replaceTile(x,y,tile); }

static void require(bool ok, const char *message)
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
	Game &game = gui.game;
	// Two teams, so a ring can be built by someone other than the field's owner.
	// One player per team; the player index doubles as the team number.
	Team *team = nullptr;
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
		const BuildingType *type = globalContainer->buildingsTypes.get(siteType);
		siteW = type->width;
		siteH = type->height;
		flagType = globalContainer->buildingsTypes.getTypeNum("explorationflag", 0, false);
		require(flagType >= 0, "exploration flag type exists");
	}

	// The player's path: OrderCreate -> Game::executeCreate -> Game::addBuilding.
	Building *place(int x, int y, int teamNumber = 0)
	{
		std::shared_ptr<Order> order(new OrderCreate(teamNumber, x, y, siteType, 1, 1));
		order->sender = teamNumber;
		game.executeOrder(order, 0);
		Uint16 gid = game.map.getBuilding(x, y);
		require(gid != NOGBID, "construction site placed through the create order");
		require(Building::GIDtoTeam(gid) == teamNumber,
				"the site belongs to the team that ordered it");
		return game.teams[teamNumber]->myBuildings[Building::GIDtoID(gid)];
	}

	// Same path, for a virtual building. A flag never reaches the building tile
	// grid, so there is no gid on the map to look it up by - find it among the
	// owner's buildings instead. radius keeps the flag's goal disc inside the ring.
	Building *placeFlag(int x, int y, int radius, int teamNumber = 0)
	{
		std::shared_ptr<Order> order(new OrderCreate(teamNumber, x, y, flagType, 1, 1, radius));
		order->sender = teamNumber;
		game.executeOrder(order, 0);
		require(game.map.getBuilding(x, y) == NOGBID,
				"a flag is not written into the building tile grid");
		Team *owner = game.teams[teamNumber];
		for (int id = 0; id < Building::MAX_COUNT; ++id)
		{
			Building *b = owner->myBuildings[id];
			if (b && b->type->isVirtual && b->posX == x && b->posY == y)
				return b;
		}
		require(false, "exploration flag placed through the create order");
		return nullptr;
	}

	// The player's path: OrderDelete -> launchDelete -> Team::syncStep clears it.
	void remove(const std::vector<Building *> &sites, int teamNumber = 0)
	{
		for (Building *b : sites)
		{
			std::shared_ptr<Order> order(new OrderDelete(b->gid));
			order->sender = teamNumber;
			game.executeOrder(order, 0);
		}
		game.teams[teamNumber]->syncStep();
	}

	// Eight sites packed around (cx,cy) so the centre has no free neighbour.
	std::vector<Building *> placeRing(int cx, int cy, int teamNumber = 0)
	{
		std::vector<Building *> ring;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (dx != 0 || dy != 0)
					ring.push_back(place(cx + dx * siteW, cy + dy * siteH, teamNumber));
		return ring;
	}

	bool available(Building *b, int x, int y)
	{
		int dist = -1;
		return game.map.buildingAvailable(b, 0, x, y, &dist);
	}

	void tick(Uint32 n) { game.stepCounter += n; }
};

static void idleFieldStorageIsReusedWithoutStaleRoutes()
{
	World world;
	Building *first = world.place(12, 12);
	Building *second = world.place(44, 44);
	require(second->globalGradient[0] == nullptr, "second building starts without a field");
	const size_t cells = static_cast<size_t>(world.game.map.getW()) * world.game.map.getH();
	const Uint16 *firstField = world.game.map.buildingGradient(first, 0);
	require(firstField != nullptr, "first building field exists");
	BuildingGradientSearch *firstSearch = first->globalGradientSearch[0].get();
	require(firstSearch != nullptr, "first building search exists");
	const std::vector<Uint16> expected(firstField, firstField + cells);

	world.tick(768);
	first->freeIdleGradients();
	require(first->globalGradient[0] == nullptr, "idle building drops its field");
	const Uint16 *secondField = world.game.map.buildingGradient(second, 0);
	require(secondField == firstField, "another building reuses the idle field storage");
	require(second->globalGradientSearch[0].get() == firstSearch,
			"another building reuses the idle search queues");
	require(std::vector<Uint16>(secondField, secondField + cells) != expected,
			"reused storage contains the second building's field");
	const Uint16 *rebuilt = world.game.map.buildingGradient(first, 0);
	require(std::vector<Uint16>(rebuilt, rebuilt + cells) == expected,
			"reactivated building rebuilds the same route values");
	const std::vector<Uint16> secondExpected(secondField, secondField + cells);
	BuildingGradientSearch *secondSearch = second->globalGradientSearch[0].get();
	second->resetPathfindGradients();
	require(second->globalGradient[0] == nullptr, "invalidation drops the old field");
	const Uint16 *invalidated = world.game.map.buildingGradient(second, 0);
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
	Building *centre = world.place(20, 20);
	require(world.available(centre, 10, 10), "an open site is offered to a unit outside");
	require(centre->globalGradient[0] != nullptr,
			"the centre's field is cached before the ring goes up");

	std::vector<Building *> ring = world.placeRing(20, 20);
	world.tick(DIRTY_GRACE_TICKS);
	require(!world.available(centre, 10, 10),
			"a site walled in by new construction is no longer offered to a unit outside");
	require(!world.available(centre, 40, 45), "nor to a distant unit");

	world.remove(ring);
	require(world.available(centre, 10, 10), "clearing the ring makes the site available at once");
	std::puts(
		"PASS a ring placed around a cached field cuts it off, and clearing the ring restores it");
}

// Leo's experiment on master: the ring stands before the centre is placed, so
// the centre's field is first built after the wall closed.
static void centrePlacedInsideAnExistingRing()
{
	World world;
	std::vector<Building *> ring = world.placeRing(20, 20);
	Building *centre = world.place(20, 20);
	require(!world.available(centre, 10, 10),
			"a site placed inside a ring is never offered to a unit outside");

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
	Building *centre = world.place(20, 20);
	require(world.available(centre, 10, 10), "an open site is offered to a unit outside");
	require(centre->globalGradient[0] != nullptr,
			"the centre's field is cached before the ring goes up");

	std::vector<Building *> ring = world.placeRing(20, 20, 1);
	world.tick(DIRTY_GRACE_TICKS);
	require(!world.available(centre, 10, 10),
			"a site walled in by another team is no longer offered to a unit outside");

	// Clearing it is the same story in reverse, with one difference worth pinning:
	// Team::syncStep frees the fields of the team that demolished, so the owner's
	// field is not freed here, only invalidated. It comes back on the next rebuild
	// the interval allows rather than on the next lookup.
	world.remove(ring, 1);
	world.tick(DIRTY_GRACE_TICKS);
	require(world.available(centre, 10, 10),
			"clearing the other team's ring makes the site available again");
	std::puts("PASS a ring built by another team cuts off a cached field, and clearing it restores "
			  "the field");
}

// A virtual flag is never written into the building tile grid, so walking the
// changed footprint could not discover its field however close the change was.
// The flag's goal disc is kept inside the ring, so once the ring closes there is
// no way in and no goal outside.
static void aRingCutsOffAVirtualFlagsField()
{
	World world;
	Building *flag = world.placeFlag(20, 20, 1);
	require(world.available(flag, 10, 10), "an open flag is offered to a unit outside");
	require(flag->globalGradient[0] != nullptr,
			"the flag's field is cached before the ring goes up");

	std::vector<Building *> ring = world.placeRing(20, 20);
	world.tick(DIRTY_GRACE_TICKS);
	require(!world.available(flag, 10, 10),
			"a flag walled in by new construction is no longer offered to a unit outside");

	world.remove(ring);
	require(world.available(flag, 10, 10), "clearing the ring makes the flag available at once");
	std::puts("PASS a ring around a virtual flag cuts off its field, though no flag is in the "
			  "building tile grid");
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
	require(world.available(centre, 45, 45),
			"paused field preserves its old route before refresh is due");
	world.tick(DIRTY_GRACE_TICKS);
	require(!world.available(centre, 45, 45),
			"normal refresh replaces the paused obstacle snapshot");
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
		require(map.buildingAvailable(centre, swim, 18, 20, &expectedDist),
				"complete distance exists");
		require(map.pathfindBuilding(centre, swim, 18, 20, &expectedDx, &expectedDy),
				"complete direction exists");
		map.updateGlobalGradient(centre, swim);
		int dist, dx, dy;
		require(map.buildingAvailable(centre, swim, 18, 20, &dist) && dist == expectedDist,
				"point API resolves its own distance");
		require(!centre->globalGradientSearch[swim]->complete(),
				"point read does not finish the field");
		map.updateGlobalGradient(centre, swim);
		require(map.pathfindBuilding(centre, swim, 18, 20, &dx, &dy) && dx == expectedDx &&
					dy == expectedDy,
				"movement API resolves its own input layer");
		require(!centre->globalGradientSearch[swim]->complete(),
				"movement does not finish the field");
		full = map.buildingGradient(centre, swim);
		require(centre->globalGradientSearch[swim]->complete(),
				"public array API completes the field");
		require(std::vector<Uint16>(full, full + expected.size()) == expected,
				"public array is fully resolved");
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
	auto capture = [&]
	{
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
			for (const auto *field :
				 {map.getForbiddenGradient(0, swim), map.getGuardAreasGradient(0, swim),
				  map.getClearAreasGradient(0, swim)})
				fields.emplace_back(field, field + cells);
		}
		return fields;
	};
	expected = capture();
	for (unsigned threads : {1, 2, 4, 8})
	{
		map.configureCompute(threads, 7);
		map.updateTeamAreaGradients(0);
		require(capture() == expected,
				"parallel area/building initialization preserves all fields");
	}
	// Exercise the hiring prepass with two independently owned weighted fields.
	Unit *a = world.game.addUnit(10, 10, 0, WORKER, 0, 0, 0, 0);
	Unit *b = world.game.addUnit(12, 10, 0, WORKER, 0, 0, 0, 0);
	require(a && b, "hiring test units created");
	for (auto *unit : {a, b})
	{
		unit->activity = Unit::ACT_RANDOM;
		unit->medical = Unit::MED_FREE;
		unit->performance[HARVEST] = 1;
		unit->performance[WALK] = 10;
	}
	a->performance[SWIM] = 0;
	b->performance[SWIM] = 20;
	map.updateGlobalGradient(building, a->swimClass());
	map.updateGlobalGradient(building, b->swimClass());
	const auto used = building->globalGradientUsedStep[a->swimClass()];
	map.advanceHiringGradients(building);
	require(building->globalGradientUsedStep[a->swimClass()] == used,
			"prepass does not touch use timestamps");
	const auto batches = map.computeExecutor().metrics().batches;
	map.advanceHiringGradients(building);
	require(map.computeExecutor().metrics().batches == batches,
			"settled hiring targets dispatch no jobs");
	for (auto *unit : {a, b})
	{
		const int swim = unit->swimClass();
		const auto *field = map.buildingGradient(building, swim);
		require(std::vector<Uint16>(field, field + cells) == expected[swim * 5],
				"hiring advancement preserves frozen fields");
	}
	std::puts("PASS parallel area batches, seed initialization, frozen hiring advancement");
}

static void delayedFields()
{
	for (unsigned workers : {0, 1, 2, 4, 8})
		for (int kind = 0; kind < 3; ++kind)
		{
			World world(7);
			Map &map = world.game.map;
			map.setTerrain(55, 55, 256);
			map.setResource(30, 30, 0, 1);
			map.addGuardArea(40, 40, 0);
			map.addClearArea(30, 30, 0);
			const int swim = 1;
			auto field = [&]()
			{
				return kind == 0   ? map.getResourceGradient(0, 0, swim)
					   : kind == 1 ? map.getGuardAreasGradient(0, swim)
								   : map.getClearAreasGradient(0, swim);
			};
			auto refresh = [&]()
			{
				if (kind == 0)
					map.updateResourcesGradient(0, 0, swim);
				else if (kind == 1)
					map.updateGuardAreasGradient(0, swim);
				else
					map.updateClearAreasGradient(0, swim);
			};
			const auto cells = map.getW() * map.getH();
			field();
			map.configureGradientPipeline(workers, 3);
			map.advanceGradientPipeline();
			map.syncStep(0); // Seed the only allocated periodic slot.
			require(map.gradientPipelineStatus().jobs == 1, "pipeline scheduled a real field");
			map.addForbidden(41, 40, 0);
			refresh();
			const std::vector<Uint16> expected(field(), field() + cells);
			map.advanceGradientPipeline();
			map.advanceGradientPipeline();
			map.advanceGradientPipeline();
			require(map.gradientPipelineStatus().discarded == 1,
					"synchronous refresh supersedes queued snapshot");
			require(std::vector<Uint16>(field(), field() + cells) == expected,
					"old field cannot overwrite fresh synchronous field");
			// A subsequent periodic snapshot publishes normally at its fixed deadline.
			map.syncStep(1);
			map.advanceGradientPipeline();
			map.advanceGradientPipeline();
			require(map.gradientPipelineStatus().published == 0, "no early publication");
			map.advanceGradientPipeline();
			require(map.gradientPipelineStatus().published == 1, "publication at deadline");
			map.syncStep(2);
			std::vector<Uint16> frozen(cells);
			if (kind == 0)
				map.seedResourcesGradient(0, 0, swim, frozen.data());
			else if (kind == 1)
				map.seedGuardAreasGradient(0, swim, frozen.data());
			else
				map.seedClearAreasGradient(0, swim, frozen.data());
			map.propagateGradient(frozen.data(), swim);
			map.setTerrain(55, 55, 0); // Workers must use captured water, not this live edit.
			map.advanceGradientPipeline();
			map.advanceGradientPipeline();
			map.advanceGradientPipeline();
			require(std::vector<Uint16>(field(), field() + cells) == frozen,
					"terrain changes do not alter a pending snapshot");
			map.syncStep(3); // Destruction must safely drain a job in flight.
		}
	std::puts("PASS delayed resource/guard/clear publication, synchronous supersession, teardown");
}
} // namespace

TEST_SUITE("BuildingGradientInvalidation")
{
	TEST_CASE("scheduled rules repair A-star decreased keys and choose a shortest legal step")
	{
		glob2test::HeadlessGlobals globals;
		World world(4);
		auto &map = world.game.map;
		world.game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
		std::mt19937 random(719);
		constexpr int infinity = INT_MAX / 2;
		for (int sample = 0; sample < 128; ++sample)
		{
			for (int y = 0; y < 16; ++y)
				for (int x = 0; x < 16; ++x)
					editTile(map, x, y, [&](Tile &tile) { tile.forbidden = random() % 100 < 35 ? world.team->me : 0; });
			map.setAreaMask(map.coordToIndex(1,1), &Tile::forbidden, 0); map.setAreaMask(map.coordToIndex(12,12), &Tile::forbidden, 0);
			// Independent immutable-key Dijkstra oracle, reverse from the goal.
			std::vector<int> distance(256, infinity);
			using Entry = std::pair<int, int>;
			std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
			distance[12 * 16 + 12] = 0;
			queue.emplace(0, 12 * 16 + 12);
			while (!queue.empty())
			{
				auto [cost, cell] = queue.top();
				queue.pop();
				if (cost != distance[cell])
					continue;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						if (!dx && !dy)
							continue;
						const int x = (cell % 16 + dx) & 15, y = (cell / 16 + dy) & 15;
						if (!map.isFreeForGroundUnit(x, y, false, world.team->me))
							continue;
						const int next = y * 16 + x, candidate = cost + (dx && dy ? 14 : 10);
						if (candidate < distance[next])
						{
							distance[next] = candidate;
							queue.emplace(candidate, next);
						}
					}
			}
			int dx = 0, dy = 0;
			const bool found = map.pathfindPointToPoint(1, 1, 12, 12, &dx, &dy, 0, world.team->me, 512);
			require(found == (distance[17] != infinity), "A-star reachability agrees with the oracle");
			if (found)
			{
				require((dx || dy) && map.isFreeForGroundUnit(1 + dx, 1 + dy, false, world.team->me),
						"A-star selects a legal nonzero step");
				require(distance[((1 + dy) & 15) * 16 + ((1 + dx) & 15)] +
							(dx && dy ? 14 : 10) == distance[17],
						"the selected step lies on a shortest route after decreased keys");
			}
		}
	}
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
	TEST_CASE("disabling instrumentation preserves queue admission, saved jobs and fixed publication")
	{
		glob2test::HeadlessGlobals globals;
		std::string reference;
		for (unsigned workers : {0u, 2u})
			for (bool measured : {true, false})
			{
				World world;
				auto &game = world.game;
				auto &map = game.map;
				auto *b = world.place(24, 24);
				map.buildingGradient(b, 0);
				game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
				map.configureGradientPipeline(workers, 8);
				map.configureBuildingGradientInstrumentation(measured);
				game.stepCounter = 200;
				editTile(map, 10, 10, [&](Tile &tile) { tile.forbidden |= world.team->me; });
				map.bumpTopologyGeneration();
				require(map.requestBuildingRefresh(b, 0), "request admitted without diagnostic state");
				map.submitBuildingRefreshes();
				const auto status = map.buildingRefreshStatus();
				require(status.pending == 1 && status.bytes > 0, "admission accounting always active");
				require(measured ? status.jobs == 1 : status.jobs == 0, "instrumentation toggle controls counters");
				game.stepCounter = 202;
				auto *bytes = new GAGCore::MemoryStreamBackend();
				GAGCore::BinaryOutputStream out(bytes);
				map.saveBuildingRefreshes(&out);
				out.flush();
				const auto data = bytes->takeContents();
				const std::string snapshot(data.data(), data.size());
				if (reference.empty()) reference = snapshot;
				else require(snapshot == reference, "saved private jobs independent of instrumentation and workers");
				GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(data.data(), data.size()));
				in.seekFromStart(0);
				map.loadBuildingRefreshes(&in, VERSION_MINOR);
				map.publishBuildingRefreshes();
				require(map.buildingRefreshStatus().pending == 1, "save does not publish early");
				game.stepCounter = 204;
				map.publishBuildingRefreshes();
				require(map.buildingRefreshStatus().pending == 0 &&
							b->lastGlobalGradientUpdateStepCounter[0] == 200 &&
							b->globalGradient[0][map.coordToIndex(10, 10)] == GRADIENT_FORBIDDEN,
						"restored field publishes at the same deadline");
			}
	}

	TEST_CASE("staffing hybrid retains lazy cold buildings and pending hot deadlines")
	{
		glob2test::HeadlessGlobals globals;
		World world;
		auto &map = world.game.map;
		auto *b = world.place(24, 24);
		map.buildingGradient(b, 0);
		world.game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
		world.game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientHybrid);
		map.configureGradientPipeline(2, 8);
		map.configureCompute(4, Map::ComputeHiring);
		map.advanceHiringGradients(b);
		require(map.hiringPrepasses == 0, "scheduled pipeline excludes the legacy frontier prepass");
		world.game.stepCounter = 200;
		require(!map.requestBuildingRefresh(b, 0), "unused building remains synchronous");
		for (int i = 0; i < 4; ++i)
		{
			auto *unit = world.game.addUnit(10 + i, 10, 0, WORKER, 0, 0, 0, 0);
			require(unit && unit->swimClass() == 0, "ground worker created");
			b->unitsWorking.push_back(unit);
			if (i < 3) require(!map.requestBuildingRefresh(b, 0), "low staffing remains lazy");
		}
		require(map.requestBuildingRefresh(b, 0), "four matching workers admit a background refresh");
		map.submitBuildingRefreshes();
		b->unitsWorking.clear();
		require(map.requestBuildingRefresh(b, 0), "pending deadline survives falling demand");
		world.game.stepCounter = 204;
		map.publishBuildingRefreshes();
		require(b->lastGlobalGradientUpdateStepCounter[0] == 200 && !map.buildingRefreshStatus().pending,
			"hot refresh retains captured age and deadline");
		require(!map.requestBuildingRefresh(b, 0), "cold building returns to the lazy path after publication");
	}

	TEST_CASE("partial scheduled fields preserve eager values through publication and save phases")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned workers : {0u, 1u, 2u, 4u, 8u})
			for (int phase : {-1, 0, 1, 2, 3, 4})
			{
				World world;
				auto &game = world.game;
				auto &map = game.map;
				auto *b = world.place(24, 24);
				editTile(map, 8, 8, [&](Tile &tile) { tile.resource.type = WOOD; });
				editTile(map, 8, 8, [&](Tile &tile) { tile.resource.amount = 10; });
				map.setCellTerrain(18, 18, TRAIL);
				map.buildingGradient(b, 0);
				map.roundTripGradient(b, WOOD, 0);
				game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
				game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPartial);
				map.configureGradientPipeline(workers, 8);
				map.configureBuildingGradientImpact("partial-oracle");
				game.stepCounter = 200;
				editTile(map, 10, 10, [&](Tile &tile) { tile.forbidden |= world.team->me; });
				map.bumpTopologyGeneration();
				map.beginGradientDecision("movement", b->gid, -1);
				const auto cells = std::size_t(map.getW()) * map.getH();
				const auto *walking = map.freshBuildingDecisionField(b, 0, -1);
				const auto *trip = map.freshBuildingDecisionField(b, 0, WOOD);
				const std::vector<Uint16> expectedWalking(walking, walking + cells), expectedTrip(trip, trip + cells);
				require(map.requestBuildingRefresh(b, 0), "partial bundle admitted");
				map.submitBuildingRefreshes();
				// Later map costs must never leak into either paused search.
				map.setCellTerrain(18, 18, WATER);
				if (phase >= 0)
				{
					game.stepCounter = 200 + phase;
					if (phase == 4) map.publishBuildingRefreshes();
					auto *bytes = new GAGCore::MemoryStreamBackend();
					GAGCore::BinaryOutputStream out(bytes);
					map.saveBuildingRefreshes(&out);
					out.flush();
					const auto data = bytes->takeContents();
					GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(data.data(), data.size()));
					in.seekFromStart(0);
					map.loadBuildingRefreshes(&in, VERSION_MINOR);
				}
				game.stepCounter = 204;
				map.publishBuildingRefreshes();
				require(b->lastGlobalGradientUpdateStepCounter[0] == 200 && b->roundTripGradientStep[WOOD][0] == 200,
					"save retains both captured ages");
				for (std::size_t cell = 0; cell < cells; cell += 19)
				{
					int distance = 0;
					map.buildingAvailable(b, 0, int(cell % map.getW()), int(cell / map.getW()), &distance);
					require(b->globalGradient[0][cell] == expectedWalking[cell], "resumed walking query matches eager snapshot");
					require(map.roundTripGradientAt(b, WOOD, 0, cell)[cell] == expectedTrip[cell], "resumed round-trip query matches eager snapshot");
				}
				map.finishRoundTripGradient(b, WOOD, 0);
				require(expectedTrip == std::vector<Uint16>(b->roundTripGradient[WOOD][0], b->roundTripGradient[WOOD][0] + cells),
					"materialized round-trip field matches eager oracle");
				if (phase == -1) require(b->roundTripGradientSearch[WOOD][0] != nullptr, "no-demand bundle publishes a paused round-trip frontier");
				b->freeGradients();
				require(!b->roundTripGradientSearch[WOOD][0], "storage teardown clears round-trip search ownership");
			}
	}

	TEST_CASE("scheduled building bundles publish at fixed deadlines across worker counts")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned workers : {0, 1, 2, 4, 8})
			for (unsigned delay : {2, 4, 8})
			{
				World world;
				auto &game = world.game;
				auto &map = game.map;
				int distance = 0;
				auto *b = world.place(24, 24);
				game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
				game.gameHeader.setBuildingGradientDelay(delay);
				map.configureGradientPipeline(workers, 8);
				map.buildingGradient(b, 0);
				const auto cells = std::size_t(map.getW()) * map.getH();
				std::vector<Uint16> old(b->globalGradient[0], b->globalGradient[0] + cells);
				game.stepCounter = DIRTY_GRACE_TICKS + 1;
				editTile(map, 10, 10, [&](Tile &tile) { tile.forbidden |= world.team->me; });
				map.bumpTopologyGeneration();
				map.buildingAvailable(b, 0, 2, 2, &distance);
				map.submitBuildingRefreshes();
				require(map.buildingRefreshStatus().pending == 1, "one refresh admitted");
				for (unsigned i = 0; i < delay; ++i)
				{
					map.publishBuildingRefreshes();
					require(std::vector<Uint16>(b->globalGradient[0],
												b->globalGradient[0] + cells) == old,
							"old field visible before deadline");
					map.buildingAvailable(b, 0, 2, 2, &distance);
					++game.stepCounter;
				}
				map.publishBuildingRefreshes();
				require(b->globalGradient[0][map.coordToIndex(10, 10)] == GRADIENT_FORBIDDEN,
						"replacement visible on deadline");
				require(map.buildingRefreshStatus().published == 1 &&
							map.buildingRefreshStatus().pending == 0,
						"one publication despite repeated requests");
				require(b->lastGlobalGradientUpdateStepCounter[0] == DIRTY_GRACE_TICKS + 1,
						"refresh age is captured tick");
			}
	}
	TEST_CASE(
		"pending building results survive save boundaries and cannot resurrect evicted fields")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned phase = 0; phase < 4; ++phase)
		{
			World world;
			auto &game = world.game;
			auto &map = game.map;
			int distance = 0;
			auto *b = world.place(24, 24);
			game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
			map.configureGradientPipeline(2, 8);
			map.buildingGradient(b, 0);
			game.stepCounter = DIRTY_GRACE_TICKS + 1;
			editTile(map, 10, 10, [&](Tile &tile) { tile.forbidden |= world.team->me; });
			map.bumpTopologyGeneration();
			map.buildingAvailable(b, 0, 2, 2, &distance);
			map.submitBuildingRefreshes();
			game.stepCounter += phase;
			auto *bytes = new GAGCore::MemoryStreamBackend();
			GAGCore::BinaryOutputStream out(bytes);
			map.saveBuildingRefreshes(&out);
			out.flush();
			const auto data = bytes->takeContents();
			require(b->globalGradient[0][map.coordToIndex(10, 10)] != GRADIENT_FORBIDDEN,
					"saving does not publish");
			GAGCore::BinaryInputStream in(
				new GAGCore::MemoryStreamBackend(data.data(), data.size()));
			in.seekFromStart(0);
			map.loadBuildingRefreshes(&in, VERSION_MINOR);
			require(map.buildingRefreshStatus().pending == 1, "save restores pending bundle");
			game.stepCounter = DIRTY_GRACE_TICKS + 5;
			map.publishBuildingRefreshes();
			require(map.buildingRefreshStatus().published == 1,
					"restored destination passes lifetime validation");
			require(b->globalGradient[0][map.coordToIndex(10, 10)] == GRADIENT_FORBIDDEN,
					"restored result publishes at original tick");
			game.stepCounter += DIRTY_GRACE_TICKS;
			editTile(map, 11, 10, [&](Tile &tile) { tile.forbidden |= world.team->me; });
			map.bumpTopologyGeneration();
			map.buildingAvailable(b, 0, 2, 2, &distance);
			map.submitBuildingRefreshes();
			b->resetPathfindGradients();
			game.stepCounter += 4;
			map.publishBuildingRefreshes();
			require(!b->globalGradient[0] && map.buildingRefreshStatus().discarded == 1,
					"invalidated destination remains absent");
		}
	}
	TEST_CASE("a deleted building's pending result cannot replace a reused destination")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned workers : {0, 4})
		{
			World world;
			auto &game = world.game;
			auto &map = game.map;
			game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
			map.configureGradientPipeline(workers, 8);
			auto *old = world.place(24, 24);
			map.buildingGradient(old, 0);
			const auto gid = old->gid;
			const auto identity = old->scriptIdentity;
			game.stepCounter = 200;
			map.requestBuildingRefresh(old, 0);
			map.submitBuildingRefreshes();
			world.remove({old});
			map.setMapDiscovered();
			auto *replacement = game.addBuilding(40, 40, world.siteType, 0, 1, 1);
			require(replacement != nullptr, "replacement allocated after deletion");
			require(replacement->gid == gid && replacement->scriptIdentity != identity,
					"numeric destination reused with a new lifetime");
			map.buildingGradient(replacement, 0);
			const auto cells = std::size_t(map.getW()) * map.getH();
			std::vector<Uint16> field(replacement->globalGradient[0],
									  replacement->globalGradient[0] + cells);
			game.stepCounter = 204;
			map.publishBuildingRefreshes();
			require(map.buildingRefreshStatus().discarded == 1 &&
						field == std::vector<Uint16>(replacement->globalGradient[0],
													 replacement->globalGradient[0] + cells),
					"old result discarded without touching replacement storage");
		}
	}

	TEST_CASE("published access metadata remains independent across swim-cost classes and saves")
	{
		glob2test::HeadlessGlobals globals;
		World world;
		auto &game = world.game;
		auto &map = game.map;
		game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
		map.configureGradientPipeline(2, 8);
		auto *b = world.place(24, 24);
		map.buildingGradient(b, 1);
		map.buildingGradient(b, 3);
		for (int y = 23; y <= 24 + world.siteH; ++y)
			for (int x = 23; x <= 24 + world.siteW; ++x)
				if (x == 23 || y == 23 || x == 24 + world.siteW || y == 24 + world.siteH)
					editTile(map, x, y, [&](Tile &tile) { tile.forbidden |= world.team->me; });
		map.bumpTopologyGeneration();
		game.stepCounter = 200;
		map.requestBuildingRefresh(b, 1);
		map.submitBuildingRefreshes();
		game.stepCounter = 204;
		map.publishBuildingRefreshes();
		require(b->locked[1] && !b->locked[3],
				"one class publication cannot change another class's access metadata");
		b->dirtyGradients();
		require(b->locked[1] && !b->locked[3], "invalidation keeps published access metadata");
		auto *bytes = new GAGCore::MemoryStreamBackend();
		GAGCore::BinaryOutputStream out(bytes);
		map.saveRuntimeState(&out);
		out.flush();
		const auto data = bytes->takeContents();
		GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(data.data(), data.size()));
		in.seekFromStart(0);
		map.loadRuntimeState(&in, VERSION_MINOR);
		require(b->locked[1] && !b->locked[3],
				"save and load preserve each class's published metadata");
	}

	TEST_CASE("later invalidation remains dirty and multiple building jobs share a deadline")
	{
		glob2test::HeadlessGlobals globals;
		World world;
		auto &game = world.game;
		auto &map = game.map;
		int distance = 0;
		game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
		map.configureGradientPipeline(4, 8);
		auto *a = world.place(24, 24);
		auto *b = world.place(40, 40);
		map.buildingGradient(a, 0);
		map.buildingGradient(b, 0);
		game.stepCounter = DIRTY_GRACE_TICKS + 1;
		editTile(map, 10, 10, [&](Tile &tile) { tile.forbidden |= world.team->me; });
		map.bumpTopologyGeneration();
		map.buildingAvailable(b, 0, 2, 2, &distance);
		map.buildingAvailable(a, 0, 2, 2, &distance);
		map.submitBuildingRefreshes();
		a->dirtyGradients();
		game.stepCounter += 4;
		map.publishBuildingRefreshes();
		require(a->dirtyGradient[0] && !b->dirtyGradient[0],
				"publication keeps notifications after capture");
		require(map.buildingRefreshStatus().published == 2, "both jobs published on same tick");
	}

	TEST_CASE("impact oracle never changes published fields or simulation random draws")
	{
		glob2test::HeadlessGlobals globals;
		World world;
		auto &game = world.game;
		auto &map = game.map;
		auto *b = world.place(24, 24);
		const auto *field = map.buildingGradient(b, 0);
		const auto cells = std::size_t(map.getW()) * map.getH();
		const auto age = b->lastGlobalGradientUpdateStepCounter[0];
		std::vector<Uint16> before(field, field + cells);
		auto rng = syncRandEngine();
		map.configureBuildingGradientImpact("impact-oracle");
		editTile(map, 10, 10, [&](Tile &tile) { tile.forbidden |= world.team->me; });
		map.bumpTopologyGeneration();
		map.beginGradientDecision("movement", b->gid, -1);
		const auto *fresh = map.freshBuildingDecisionField(b, 0, -1);
		require(fresh[map.coordToIndex(10, 10)] == GRADIENT_FORBIDDEN, "oracle sees live obstacle");
		require(before == std::vector<Uint16>(field, field + cells) &&
					b->lastGlobalGradientUpdateStepCounter[0] == age,
				"oracle leaves published field and age alone");
		require(rng == syncRandEngine(), "oracle does not consume game RNG");
	}

	TEST_CASE(
		"queued walking and round-trip bundles match synchronous kernels for weighted swim classes")
	{
		glob2test::HeadlessGlobals globals;
		for (bool modified : {false, true})
		for (int sw : {0, 1, Map::SWIM_CLASS_EVEN, SWIM_CLASS_COUNT - 1})
		{
			World world;
			auto &game = world.game;
			auto &map = game.map;
			auto *b = world.place(24, 24);
			map.setTerrain(10, 10, 0);
			if (modified)
			{
				for (int x = 3; x < 24; ++x) map.setCellTerrain(x, 12, TRAIL);
				map.setCellTerrain(11, 12, ICE);
			}
			editTile(map, 8, 8, [&](Tile &tile) { tile.resource.type = WOOD; });
			editTile(map, 8, 8, [&](Tile &tile) { tile.resource.amount = 10; });
			map.buildingGradient(b, sw);
			map.roundTripGradient(b, WOOD, sw);
			game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
			map.configureGradientPipeline(4, 8);
			game.stepCounter = DIRTY_GRACE_TICKS + 1;
			editTile(map, 9, 9, [&](Tile &tile) { tile.forbidden |= world.team->me; });
			map.bumpTopologyGeneration();
			map.requestBuildingRefresh(b, sw);
			map.submitBuildingRefreshes();
			game.stepCounter += 4;
			map.publishBuildingRefreshes();
			const auto cells = std::size_t(map.getW()) * map.getH();
			const std::vector<Uint16> walking(b->globalGradient[sw], b->globalGradient[sw] + cells),
				trip(b->roundTripGradient[WOOD][sw], b->roundTripGradient[WOOD][sw] + cells);
			game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline, false);
			map.updateGlobalGradient(b, sw);
			map.buildingGradient(b, sw);
			map.updateRoundTripGradient(b, WOOD, sw);
			require(walking ==
						std::vector<Uint16>(b->globalGradient[sw], b->globalGradient[sw] + cells),
					"walking field matches synchronous full result");
			require(trip == std::vector<Uint16>(b->roundTripGradient[WOOD][sw],
												b->roundTripGradient[WOOD][sw] + cells),
					"round trip matches synchronous full result");
		}
	}

	TEST_CASE("scheduled bundles retain captured terrain costs across edits and private save restoration")
	{
		glob2test::HeadlessGlobals globals;
		World world(5);
		auto &game = world.game;
		auto &map = game.map;
		auto *b = world.place(20, 20);
		for (int x = 3; x < 20; ++x) map.setCellTerrain(x, 15, TRAIL);
		map.setCellTerrain(8, 15, ICE);
		editTile(map, 6, 6, [&](Tile &tile) { tile.resource.type = WOOD; });
		editTile(map, 6, 6, [&](Tile &tile) { tile.resource.amount = 10; });
		game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
		map.configureGradientPipeline(4, 8);
		map.configureBuildingGradientImpact("terrain-bundle-oracle");
		const auto cells = std::size_t(map.getW()) * map.getH();
		std::array<std::vector<Uint16>, SWIM_CLASS_COUNT> walking, trips;
		for (int sw = 0; sw < SWIM_CLASS_COUNT; ++sw)
		{
			map.buildingGradient(b, sw);
			map.roundTripGradient(b, WOOD, sw);
		}
		game.stepCounter = 200;
		map.beginGradientDecision("movement", b->gid, -1);
		for (int sw = 0; sw < SWIM_CLASS_COUNT; ++sw)
		{
			const auto *a = map.freshBuildingDecisionField(b, sw, -1);
			const auto *c = map.freshBuildingDecisionField(b, sw, WOOD);
			walking[sw].assign(a, a + cells);
			trips[sw].assign(c, c + cells);
			map.requestBuildingRefresh(b, sw);
		}
		map.submitBuildingRefreshes();
		map.setCellTerrain(8, 15, WATER);
		map.setCellTerrain(9, 15, GRASS);
		game.stepCounter = 201;
		auto *bytes = new GAGCore::MemoryStreamBackend();
		GAGCore::BinaryOutputStream out(bytes);
		map.saveBuildingRefreshes(&out);
		out.flush();
		const auto data = bytes->takeContents();
		GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(data.data(),data.size()));
		in.seekFromStart(0);
		map.loadBuildingRefreshes(&in, VERSION_MINOR);
		game.stepCounter = 203;
		map.publishBuildingRefreshes();
		require(map.buildingRefreshStatus().pending == SWIM_CLASS_COUNT, "restored terrain bundles keep their fixed deadline");
		game.stepCounter = 204;
		map.publishBuildingRefreshes();
		for (int sw = 0; sw < SWIM_CLASS_COUNT; ++sw)
		{
			require(walking[sw] == std::vector<Uint16>(b->globalGradient[sw], b->globalGradient[sw] + cells), "published walking field uses captured road and ice costs");
			require(trips[sw] == std::vector<Uint16>(b->roundTripGradient[WOOD][sw], b->roundTripGradient[WOOD][sw] + cells), "round trip shares the captured terrain and resource parent");
			require(b->dirtyGradient[sw] || b->gradientGeneration[sw] != map.topologyGeneration, "later terrain changes remain dirty after publishing the snapshot");
		}
	}

	TEST_CASE("building queue admission is bounded and stable across completed-result restoration")
	{
		glob2test::HeadlessGlobals globals;
		World world(10);
		auto &game = world.game;
		auto &map = game.map;
		game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
		map.configureGradientPipeline(2, 8);
		std::vector<Building *> destinations;
		int distance = 0;
		for (int n = 0; n < 32; ++n)
		{
			auto *b = world.place(10 + n * 5, 24);
			destinations.push_back(b);
			map.buildingAvailable(b, 0, b->posX - 2, b->posY, &distance);
		}
		game.stepCounter = 200;
		for (auto it = destinations.rbegin(); it != destinations.rend(); ++it)
			map.requestBuildingRefresh(*it, 0);
		map.submitBuildingRefreshes();
		const auto before = map.buildingRefreshStatus();
		require(before.bytes <= BuildingGradientScheduler::BYTE_LIMIT &&
					before.pending == destinations.size() && before.queuedRequests == 0 &&
					before.synchronousFallback > 0,
				"overflow results are retained privately without increasing queued RAM");
		auto *bytes = new GAGCore::MemoryStreamBackend();
		GAGCore::BinaryOutputStream out(bytes);
		map.saveBuildingRefreshes(&out);
		out.flush();
		const auto data = bytes->takeContents();
		GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(data.data(), data.size()));
		in.seekFromStart(0);
		map.loadBuildingRefreshes(&in, VERSION_MINOR);
		require(map.buildingRefreshStatus().bytes == before.bytes,
				"loaded completed results retain admission reservation");
		++game.stepCounter;
		map.submitBuildingRefreshes();
		require(map.buildingRefreshStatus().pending == before.pending,
				"completion and restoration retain the original pending destinations");
		game.stepCounter = 204;
		map.publishBuildingRefreshes();
		for (std::size_t i = 0; i < destinations.size(); ++i)
			require(destinations[i]->lastGlobalGradientUpdateStepCounter[0] == 200,
					"all destinations publish on the original deadline");
		map.submitBuildingRefreshes();
		require(map.buildingRefreshStatus().pending == 0,
				"overflow leaves no deferred requests or overdue jobs");
	}

	TEST_CASE("an oversized building bundle retains its fixed deadline through save and load")
	{
		glob2test::HeadlessGlobals globals;
		World world(11);
		auto &game = world.game;
		auto &map = game.map;
		auto *b = world.place(24, 24);
		editTile(map, 8, 8, [&](Tile &tile) { tile.resource.type = WOOD; });
		editTile(map, 8, 8, [&](Tile &tile) { tile.resource.amount = 10; });
		map.buildingGradient(b, 0);
		map.roundTripGradient(b, WOOD, 0);
		game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
		map.configureGradientPipeline(4, 8);
		game.stepCounter = 200;
		editTile(map, 10, 10, [&](Tile &tile) { tile.forbidden |= world.team->me; });
		map.bumpTopologyGeneration();
		map.requestBuildingRefresh(b, 0);
		map.submitBuildingRefreshes();
		const auto status = map.buildingRefreshStatus();
		require(status.synchronousFallback == 1 && status.pending == 1 && status.bytes == 0,
				"oversized result is retained privately without reserving queued RAM");
		require(b->lastGlobalGradientUpdateStepCounter[0] == 0 &&
					b->globalGradient[0][map.coordToIndex(10, 10)] != GRADIENT_FORBIDDEN,
				"synchronous fallback does not publish early");
		game.stepCounter = 202;
		auto *bytes = new GAGCore::MemoryStreamBackend();
		GAGCore::BinaryOutputStream out(bytes);
		map.saveBuildingRefreshes(&out);
		out.flush();
		const auto data = bytes->takeContents();
		GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(data.data(), data.size()));
		in.seekFromStart(0);
		map.loadBuildingRefreshes(&in, VERSION_MINOR);
		map.publishBuildingRefreshes();
		require(map.buildingRefreshStatus().pending == 1 && map.buildingRefreshStatus().bytes == 0,
				"restored overflow remains private before deadline");
		game.stepCounter = 204;
		map.publishBuildingRefreshes();
		require(b->lastGlobalGradientUpdateStepCounter[0] == 200 &&
					b->roundTripGradientStep[WOOD][0] == 200 &&
					b->globalGradient[0][map.coordToIndex(10, 10)] == GRADIENT_FORBIDDEN,
				"fallback publishes both fields at the original deadline");
		require(status.fallbackNs > 0, "fallback elapsed cost reported");
	}

	TEST_CASE(
		"first construction of a new round-trip field remains synchronous during a pending bundle")
	{
		glob2test::HeadlessGlobals globals;
		World world;
		auto &game = world.game;
		auto &map = game.map;
		auto *b = world.place(24, 24);
		editTile(map, 8, 8, [&](Tile &tile) { tile.resource.type = WOOD; });
		editTile(map, 8, 8, [&](Tile &tile) { tile.resource.amount = 10; });
		map.buildingGradient(b, 0);
		game.gameHeader.getExperiments().set(ExperimentId::BuildingGradientPipeline);
		map.configureGradientPipeline(2, 8);
		game.stepCounter = 200;
		map.requestBuildingRefresh(b, 0);
		map.submitBuildingRefreshes();
		map.roundTripGradient(b, WOOD, 0);
		require(b->roundTripGradient[WOOD][0] != nullptr,
				"new field constructed before bundle deadline");
		const auto cells = std::size_t(map.getW()) * map.getH();
		const std::vector<Uint16> cold(b->roundTripGradient[WOOD][0],
									   b->roundTripGradient[WOOD][0] + cells);
		game.stepCounter = 204;
		map.publishBuildingRefreshes();
		require(cold == std::vector<Uint16>(b->roundTripGradient[WOOD][0],
											b->roundTripGradient[WOOD][0] + cells),
				"pending bundle does not replace a field absent from its captured inputs");
	}

	TEST_CASE("fresh oracle exposes closing and opening routes and moved flag goals")
	{
		glob2test::HeadlessGlobals globals;
		World world;
		auto &map = world.game.map;
		auto *b = world.place(24, 24);
		map.configureBuildingGradientImpact("topology-impact");
		int distance = 0;
		require(world.available(b, 10, 10), "open route seeded");
		const auto ring = world.placeRing(24, 24);
		map.beginGradientDecision("hiring", b->gid, -1);
		require(!map.buildingDecisionDistance(b, 0, -1, 10, 10, &distance, true),
				"fresh closed route rejects while cached route remains old");
		map.updateGlobalGradient(b, 0);
		map.buildingGradient(b, 0);
		world.remove(ring);
		map.beginGradientDecision("hiring", b->gid, -1);
		require(map.buildingDecisionDistance(b, 0, -1, 10, 10, &distance, true),
				"fresh route opens after blockers disappear");
		World flags;
		auto &flagMap = flags.game.map;
		flagMap.configureBuildingGradientImpact("flag-impact");
		auto *flag = flags.placeFlag(20, 20, 1);
		flagMap.buildingGradient(flag, 0);
		flag->posX = 24;
		flag->posY = 24;
		flagMap.beginGradientDecision("movement", flag->gid, -1);
		const auto *fresh = flagMap.freshBuildingDecisionField(flag, 0, -1);
		require(fresh[flagMap.coordToIndex(24, 24)] == GRADIENT_AT_GOAL &&
					flag->globalGradient[0][flagMap.coordToIndex(24, 24)] != GRADIENT_AT_GOAL,
				"moved flag retains old goal only in published field");
		auto *unit = new Unit(20, 20, Unit::GIDfrom(0, 0), WORKER, flags.team, 0);
		flags.team->myUnits[0] = unit;
		unit->dx = unit->dy = 0;
		flagMap.beginGradientDecision("movement", flag->gid, unit->gid);
		flagMap.auditBuildingMovement(unit, flag, true);
		unit->posY = 22;
		unit->dx = 1;
		unit->dy = 0;
		flagMap.beginGradientDecision("movement", flag->gid, unit->gid);
		const auto *alternativeField = flagMap.freshBuildingDecisionField(flag, 0, -1);
		const auto freshDirection = flagMap.evaluateGradientDirection(
			flags.team->me, 0, unit->posX, unit->posY, alternativeField, false).best;
		require(freshDirection >= 0, "fresh moved flag has a legal downhill direction");
		unit->dy = tabClose[freshDirection][1] == 0 ? 1 : 0;
		flagMap.auditBuildingMovement(unit, flag, true);
		flagMap.finishGradientImpact();
		std::ifstream records("flag-impact-decisions.csv");
		std::string line;
		std::getline(records, line);
		auto split = [](const std::string &line)
		{
			std::stringstream input(line);
			std::vector<std::string> columns;
			for (std::string value; std::getline(input, value, ',');)
				columns.push_back(value);
			return columns;
		};
		const auto header = split(line);
		auto values = [&]()
		{
			std::getline(records, line);
			const auto columns = split(line);
			require(columns.size() == header.size(), "movement audit rows retain scores");
			std::map<std::string, std::string> result;
			for (std::size_t i = 0; i < header.size(); ++i)
				result[header[i]] = columns[i];
			return result;
		};
		const auto stop = values(), alternative = values();
		require(std::stoi(stop.at("live_choice")) == 8 &&
				std::stoi(stop.at("fresh_choice")) >= 0 && std::stoi(stop.at("fresh_choice")) < 8,
				"old goal stop differs from the fresh legal direction");
		require(alternative.at("changed") == "1" && alternative.at("harm_cost") == "0" &&
					alternative.at("live_score") == alternative.at("fresh_score"),
				"equal-cost legal alternatives are changed directions without added path cost");
	}

	TEST_CASE("resource audit distinguishes a stale market route from harvesting")
	{
		glob2test::HeadlessGlobals globals;
		World world;
		auto &game = world.game;
		auto &map = game.map;
		struct DecisionUnit : Unit
		{
			using Unit::evaluateFetchDecision;
			using Unit::Unit;
		};
		game.gameHeader.getExperiments().set(ExperimentId::MarketsV2);
		game.configureBuildingCatalog();
		const auto innType = game.buildingsTypes.getTypeNum("inn", 0, false);
		const auto marketType = game.buildingsTypes.getTypeNum("market", 1, false);
		auto *inn = game.addBuilding(24, 24, innType, 0, 1, 1);
		auto *market = game.addBuilding(14, 10, marketType, 0, 1, 1);
		require(inn && market, "completed inn and market created");
		map.setBuilding(inn->posX, inn->posY, inn->type->width, inn->type->height, inn->gid);
		map.setBuilding(market->posX, market->posY, market->type->width, market->type->height, market->gid);
		market->resources[WHEAT] = 10;
		int resourceDistance = 0;
		require(map.resourceAvailable(0, WHEAT, 0, 10, 10, &resourceDistance, true, inn),
				"published stocked-market parent allocated");
		map.buildingGradient(market, 0);
		map.roundTripGradient(inn, WHEAT, 0);
		editTile(map, 24, 18, [&](Tile &tile) { tile.resource.type = WOOD; });
		editTile(map, 24, 18, [&](Tile &tile) { tile.resource.amount = 10; });
		require(map.resourceAvailable(0, WOOD, 0, 10, 10, &resourceDistance),
				"competing wood patch parent allocated");
		map.roundTripGradient(inn, WOOD, 0);
		map.configureBuildingGradientImpact("market-route-impact");
		DecisionUnit unit(10, 10, Unit::GIDfrom(0, 0), WORKER, world.team, 0);
		unit.performance[WALK] = 10;
		unit.attachedBuilding = inn;
		int needs[MAX_NB_RESOURCES]{};
		needs[WHEAT] = 2;
		needs[WOOD] = 1;
		map.beginGradientDecision("resource_type", inn->gid, unit.gid);
		const auto original = unit.evaluateFetchDecision(needs, 1000, false);
		require(original.resource == WHEAT, "nearby stocked market wins the original resource ranking");
		// Change the carrying route while holding the published resource parents
		// constant: wheat's supplier is west of the wall, wood and the inn east.
		for (int y = 0; y <= 40; ++y)
			map.setAreaMask(map.coordToIndex(18, y), &Tile::forbidden, world.team->me);
		map.beginGradientDecision("resource_type", inn->gid, unit.gid);
		auto rng = syncRandEngine();
		const auto live = unit.evaluateFetchDecision(needs, 1000, false);
		const auto fresh = unit.evaluateFetchDecision(needs, 1000, true);
		require(live.resource == WHEAT && fresh.resource == WOOD,
				"fresh carrying distance selects the competing wood patch while published route "
				"selects wheat market");
		require(rng == syncRandEngine() && market->resources[WHEAT] == 10,
				"evaluators consume neither randomness nor market stock");
		map.recordGradientDecision(inn, unit.swimClass(), live.resource,
								   fresh.resource, live.resource,
								   fresh.resource, live.score, fresh.score);
		map.auditResourceDestination(&unit, live.resource, live.exchange ? live.market : nullptr,
									 fresh.resource, fresh.exchange ? fresh.market : nullptr);
		map.finishGradientImpact();
		std::ifstream input("market-route-impact-decisions.csv");
		std::string line;
		bool resourceRecorded = false, destinationRecorded = false;
		while (std::getline(input, line))
		{
			resourceRecorded |= line.find(",resource,") != std::string::npos;
			destinationRecorded |= line.find(",resource_site,") != std::string::npos;
		}
		require(resourceRecorded && destinationRecorded,
				"controlled market discrepancy emits both choice and destination audit rows");
	}

	TEST_CASE("hiring audit detects a newly opened route without hiring or consuming RNG")
	{
		glob2test::HeadlessGlobals globals;
		World world;
		auto &game = world.game;
		auto &map = game.map;
		auto *b = world.place(24, 24);
		for (int y = 23; y <= 24 + world.siteH; ++y)
			for (int x = 23; x <= 24 + world.siteW; ++x)
				if (x == 23 || y == 23 || x == 24 + world.siteW || y == 24 + world.siteH)
					editTile(map, x, y, [&](Tile &tile) { tile.forbidden |= world.team->me; });
		map.bumpTopologyGeneration();
		require(!world.available(b, 10, 10), "closed route is cached before opening");
		for (int y = 23; y <= 24 + world.siteH; ++y)
			for (int x = 23; x <= 24 + world.siteW; ++x)
				editTile(map, x, y, [&](Tile &tile) { tile.forbidden &= ~world.team->me; });
		map.bumpTopologyGeneration();
		b->dirtyGradients();
		auto *unit = new Unit(10, 10, Unit::GIDfrom(0, 0), WORKER, world.team, 0);
		world.team->myUnits[0] = unit;
		unit->performance[WALK] = 10;
		unit->performance[HARVEST] = 10;
		unit->activity = Unit::ACT_RANDOM;
		unit->medical = Unit::MED_FREE;
		unit->attachedBuilding = nullptr;
		unit->carriedResource = WOOD;
		unit->trigHungry = 100;
		unit->hungry = 100 + 50 * unit->race->hungriness;
		b->desiredMaxUnitWorking = 1;
		require(b->neededResource(WOOD) > 0, "site needs the candidate's cargo");
		map.configureBuildingGradientImpact("opened-route-hiring");
		auto rng = syncRandEngine();
		require(!b->subscribeToBringResourcesStep(), "live stale route still rejects the worker");
		map.finishGradientImpact();
		require(unit->activity == Unit::ACT_RANDOM && b->unitsWorking.empty() &&
					rng == syncRandEngine(),
				"auditing leaves worker, staffing and RNG unchanged");
		std::ifstream input("opened-route-hiring-decisions.csv");
		std::string line;
		bool detected = false;
		while (std::getline(input, line))
			if (line.find(",hiring,") != std::string::npos &&
				line.find(",-1,0,") != std::string::npos)
				detected = true;
		require(detected, "audit records stale no-hire and fresh eligible candidate");
	}

	TEST_CASE("movement audit preserves forbidden escape and published resource goal stops")
	{
		glob2test::HeadlessGlobals globals;
		World world;
		auto &map = world.game.map;
		auto *b = world.place(24, 24);
		map.buildingGradient(b, 0);
		map.finishBuildingGradient(b, 0, BuildingRoute::Footprint);
		Unit unit(10, 10, Unit::GIDfrom(0, 0), WORKER, world.team, 0);
		unit.performance[WALK] = 10;
		unit.attachedBuilding = b;
		editTile(map, 10, 10, [&](Tile &tile) { tile.forbidden |= world.team->me; });
		map.configureBuildingGradientImpact("movement-branches");
		map.beginGradientDecision("movement", b->gid, unit.gid);
		const bool escaped = map.pathfindBuilding(b, 0, 10, 10, &unit.dx, &unit.dy);
		require(escaped, "live unit escapes newly forbidden ground");
		map.auditBuildingMovement(&unit, b, escaped);

		unit.posX = unit.posY = 8;
		editTile(map, 8, 8, [&](Tile &tile) { tile.resource.type = WOOD; });
		editTile(map, 8, 8, [&](Tile &tile) { tile.resource.amount = 10; });
		map.roundTripGradient(b, WOOD, 0);
		require(map.getResourceGradient(0, WOOD, 0)[map.coordToIndex(8, 8)] == GRADIENT_AT_GOAL,
				"published resource parent has a goal at the worker");
		map.beginGradientDecision("movement", b->gid, unit.gid);
		bool stop = false;
		const bool moved = map.pathfindResource(0, WOOD, 0, 8, 8, &unit.dx, &unit.dy, &stop, b);
		require(!moved && !stop, "live resource pathfinder stops on its published goal");
		map.auditBuildingMovement(&unit, b, moved, WOOD);
		map.finishGradientImpact();
		std::ifstream input("movement-branches-decisions.csv");
		std::string line;
		std::getline(input, line);
		int rows = 0;
		while (std::getline(input, line))
		{
			std::stringstream csv(line);
			std::vector<std::string> columns;
			std::string column;
			while (std::getline(csv, column, ','))
				columns.push_back(column);
			require(columns.size() > 12 && columns[12] == "0",
					"shared branch scoring reports no false building-staleness discrepancy");
			++rows;
		}
		require(rows == 2, "escape and resource goal decisions both audited");
	}

	TEST_CASE("actual map harvest and market pickup emit distinct resource outcomes")
	{
		glob2test::HeadlessGlobals globals;
		World world;
		auto &map = world.game.map;
		struct AcquisitionUnit : Unit
		{
			using Unit::handleDisplacement;
			using Unit::Unit;
		};
		auto *inn = world.game.addBuilding(24, 24,
			globalContainer->buildingsTypes.getTypeNum("inn", 0, false), 0, 1, 1);
		auto *market = world.game.addBuilding(14, 10,
			globalContainer->buildingsTypes.getTypeNum("market", 0, false), 0, 1, 1);
		require(inn && market, "inn and market created");
		map.configureBuildingGradientImpact("acquisition-sources");
		AcquisitionUnit unit(10, 10, Unit::GIDfrom(0, 0), WORKER, world.team, 0);
		unit.attachedBuilding = inn;
		unit.activity = Unit::ACT_FILLING;
		unit.displacement = Unit::DIS_HARVESTING;
		unit.movement = Unit::MOV_HARVESTING;
		unit.destinationPurpose = WOOD;
		unit.dx = 1;
		unit.dy = 0;
		editTile(map, 11, 10, [&](Tile &tile) { tile.resource.type = WOOD; });
		editTile(map, 11, 10, [&](Tile &tile) { tile.resource.amount = 10; });
		unit.handleDisplacement();
		require(unit.carriedResource == WOOD, "real map harvest grants wood");
		unit.carriedResource = UNIT_CARRIED_RESOURCE_NONE;
		unit.posX = 13;
		unit.posY = 10;
		unit.ownExchangeBuilding = market;
		unit.setTargetBuilding(market);
		unit.displacement = Unit::DIS_FILLING_BUILDING;
		unit.destinationPurpose = CHERRY;
		market->resources[CHERRY] = 10;
		unit.handleDisplacement();
		require(unit.carriedResource == CHERRY, "real exchange grants the requested fruit");
		map.finishGradientImpact();
		std::ifstream input("acquisition-sources-outcomes.csv");
		std::string line;
		std::getline(input, line);
		int harvests = 0, exchanges = 0;
		while (std::getline(input, line))
		{
			harvests += line.find(",harvested,") != std::string::npos;
			exchanges += line.find(",market_acquired,") != std::string::npos;
		}
		require(harvests == 1 && exchanges == 1,
				"each acquisition is recorded once at its actual source");
	}
}
