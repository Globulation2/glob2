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
#include "BuildingGradientDepthPolicy.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "Player.h"
#include "Unit.h"
#include "BasePlayer.h"
#include "Order.h"
#include "Brush.h"
#include "SnapshotGradient.h"
#include "engine/sim/snapshot/WorldSnapshot.h"
#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <chrono>
#include <stdexcept>
#include <thread>
#include "BuildingGradientJob.h"
#include "Version.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
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
	EntityRandom random;
	random.seed(42, 54);
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
		require(map.pathfindBuilding(random, centre, swim, 18, 20, &expectedDx, &expectedDy), "complete direction exists");
		map.updateGlobalGradient(centre, swim);
		int dist, dx, dy;
		require(map.buildingAvailable(centre, swim, 18, 20, &dist) && dist == expectedDist,
			"point API resolves its own distance");
		require(!centre->globalGradientSearch[swim]->complete(), "point read does not finish the field");
		map.updateGlobalGradient(centre, swim);
		require(map.pathfindBuilding(random, centre, swim, 18, 20, &dx, &dy) && dx == expectedDx && dy == expectedDy,
			"movement API resolves its own input layer");
		require(!centre->globalGradientSearch[swim]->complete(), "movement does not finish the field");
		full = map.buildingGradient(centre, swim);
		require(centre->globalGradientSearch[swim]->complete(), "public array API completes the field");
		require(std::vector<Uint16>(full, full + expected.size()) == expected, "public array is fully resolved");
	}
	std::puts("PASS public distance, movement and full-field lazy API boundaries");
}

static void delayedFields()
{
	for (unsigned workers : {0, 1, 2, 4, 8}) for (int kind=0; kind<3; ++kind)
	{
		World world(7);
		Map &map=world.game.map;
		map.paintCell(55, 55, WATER);
		map.setResourceByIndex(30, 30, 0, 1);
		map.addGuardArea(40, 40, 0);
		map.addClearArea(30, 30, 0);
		const int swim=1;
		auto field=[&]() { return kind==0 ? map.getMaterialGradientSlot(0, 0, swim)
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
		map.paintCell(55, 55, GRASS); // Workers must use captured water, not this live edit.
		map.advanceGradientPipeline(); map.advanceGradientPipeline(); map.advanceGradientPipeline();
		require(std::vector<Uint16>(field(),field()+cells)==frozen, "terrain changes do not alter a pending snapshot");
		map.syncStep(3); // Destruction must safely drain a job in flight.
	}
	std::puts("PASS delayed resource/guard/clear publication, synchronous supersession, teardown");
}

// Scheduled kernel equivalence: the worker's building seeder and captured-input
// search reproduce the synchronous Map path cell for cell, for every route and
// swim class, across each terrain-cost branch of Map::propagateGradient.
// Partial searches resume to the same full field.
enum class KernelTerrain { BuiltinPlain, BuiltinModified, CustomPlain, CustomModified };

void requireSameCells(const std::vector<Uint16>& actual, const Uint16* expected, const std::string& what)
{
	for (size_t i = 0; i < actual.size(); ++i)
		if (actual[i] != expected[i])
		{
			INFO(what << " cell " << i << " actual " << actual[i] << " expected " << expected[i]);
			REQUIRE(actual[i] == expected[i]);
		}
}

void scheduledKernelsMatchSynchronous(KernelTerrain config)
{
	glob2test::HeadlessGame world({.wDec=6, .hDec=6, .teams=3, .discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true});
	auto& game = world.game;
	auto& m = game.map;
	using namespace gradient_preparation;
	const bool custom = config == KernelTerrain::CustomPlain || config == KernelTerrain::CustomModified;
	const bool modified = config == KernelTerrain::BuiltinModified || config == KernelTerrain::CustomModified;
	std::vector<TerrainType> palette = {GRASS, GRASS, GRASS, SAND, WATER};
	if (config == KernelTerrain::BuiltinModified) { palette.push_back(ICE); palette.push_back(TRAIL); }
	if (custom)
	{
		const char* plain = R"({"schemaVersion":1,"terrains":[
			{"key":"wp1:deep","name":"Deep","base":"water","appearance":"sand","properties":{"walkable":false,"swimmable":true}},
			{"key":"wp1:moor","name":"Moor","base":"grass","appearance":"sand","properties":{"walkable":true}}]})";
		const char* costly = R"({"schemaVersion":1,"terrains":[
			{"key":"wp1:deep","name":"Deep","base":"water","appearance":"sand","properties":{"walkable":false,"swimmable":true,"groundSpeedQ8":64}},
			{"key":"wp1:moor","name":"Moor","base":"grass","appearance":"sand","properties":{"groundSpeedQ8":192}}]})";
		m.game = nullptr;
		m.importTerrainDefinitions(config == KernelTerrain::CustomModified ? costly : plain);
		m.setGame(&game);
		palette.push_back(*m.terrainRegistry().find("wp1:deep"));
		palette.push_back(*m.terrainRegistry().find("wp1:moor"));
	}
	// An overlay building (no ground footprint) seeds its own cells as goals,
	// and a non-square footprint takes the perimeter reachability branch.
	auto catalog = nlohmann::json::parse(game.buildingsTypes.snapshotJson());
	catalog["variants"][game.buildingsTypes.getFinishedTypeNum("hospital")]["semantics"]["occupiesGround"] = false;
	catalog["variants"][game.buildingsTypes.getFinishedTypeNum("school")]["properties"]["width"] = 3;
	game.buildingsTypes.loadSnapshotJson(catalog.dump());
	game.configureBuildingCatalog();

	for (int y = 0; y < 64; ++y)
		for (int x = 0; x < 64; ++x)
		{
			const unsigned h = unsigned(x * 7 + y * 13 + (x * y) % 5);
			m.setVertexTerrain(x, y, palette[h % palette.size()]);
			switch ((x * 3 + y * 5 + x * y) % 23)
			{
			case 0: m.replaceResource(x, y, Resource{WOOD, 0, 1, 0}); break;
			case 1: m.replaceResource(x, y, Resource{WHEAT, 0, 1, 0}); break;
			case 2: m.replaceResource(x, y, Resource{STONE, 0, 1, 0}); break;
			case 3: m.replaceResource(x, y, Resource{ALGA, 0, 1, 0}); break;
			case 4: m.addForbidden(x, y, 0); break;
			case 5: m.addForbidden(x, y, 1); break;
			case 6: m.markImmobileUnit(x, y, 1); break;
			default: break;
			}
		}
	REQUIRE(m.hasTerrainMovementModifiers() == modified);
	REQUIRE((m.terrainRegistry().size() > TERRAIN_COUNT) == custom);

	std::vector<Building*> buildings;
	auto add = [&](const char* type, int x, int y, int team = 0) {
		Building* b = world.addBuilding(type, x, y, 0, team);
		b->unitStayRange = 5;
		std::fill_n(b->clearingMaterials, MaterialCount, false);
		b->clearingMaterials[WOOD] = b->clearingMaterials[ALGA] = true;
		buildings.push_back(b);
		return b;
	};
	add("inn", 4, 4); add("hospital", 14, 4); add("racetrack", 24, 4); add("swimmingpool", 36, 4);
	add("school", 4, 20); add("explorationflag", 20, 20); add("warflag", 30, 22); add("clearingflag", 40, 24);
	add("clearingflag", 50, 50)->clearingMaterials[ALGA] = false;
	add("warflag", 10, 40)->unitStayRange = 0;
	// A footprint fenced in by forbidden paint is locked for its owner.
	Building* fenced = add("inn", 50, 34);
	for (int y = -1; y <= fenced->type->height; ++y)
		for (int x = -1; x <= fenced->type->width; ++x)
			m.addForbidden(50 + x, 34 + y, 0);
	add("inn", 33, 26, 1); add("inn", 26, 28, 2); // an enemy and an ally next to a war flag
	game.teams[0]->allies |= game.teams[2]->me;
	REQUIRE(buildings[4]->type->width != buildings[4]->type->height);

	const auto snapshot = SimulationSnapshot::capture(game, SimulationSnapshot::captureCatalog(game),
		buildingRequirements());
	const size_t cells = size_t(m.getW()) * m.getH();
	std::vector<Uint16> actual(cells), partial(cells);
	int locked = 0, clearing = 0, partials = 0;
	for (Building* b : buildings)
		for (BuildingRoute route : {BuildingRoute::Footprint, BuildingRoute::Clearing, BuildingRoute::Combat})
			for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
			{
				const std::string what = std::string(b->type->type) + " route " + std::to_string(int(route)) + " swim " + std::to_string(swim);
				const int slot = b->routeSlot(swim, route), access = b->routeAccess(swim, route);
				if (!b->globalGradient[slot]) b->globalGradient[slot] = m.acquireBuildingGradientBuffer();
				m.updateGlobalGradient(b, swim, route);
				m.finishBuildingGradient(b, swim, route);
				const Uint16* expected = b->globalGradient[slot];
				const auto seed = captureBuildingSeed(*b, swim, route);
				const auto inputs = BuildingGradientSearch::Inputs::of(m, swim);
				BuildingGradientSearch search;
				const auto result = buildBuilding(seed, snapshot, inputs, actual.data(), search, gradient_kernel::COST_LIMIT);
				INFO(what);
				REQUIRE(result.locked == b->locked[access]);
				if (route == BuildingRoute::Clearing) { REQUIRE(int(result.resourceState) == b->anyResourceToClear[swim > 0]); ++clearing; }
				requireSameCells(actual, expected, what + " full");
				if (result.locked) { ++locked; continue; }
				REQUIRE(search.complete());
				// Partial: settle a few layers, check them, then resume by cell and finish.
				seedBuilding(seed, snapshot, partial.data());
				BuildingGradientSearch resumed;
				resumed.begin(inputs, partial.data(), swim, m.getW(), m.getH());
				for (int depth : {0, 7, 40, 160})
				{
					resumed.resolveToCost(depth);
					REQUIRE((resumed.complete() || resumed.settledCost() > depth));
					for (size_t i = 0; i < cells; ++i)
						if (expected[i] == GRADIENT_FORBIDDEN || (expected[i] > GRADIENT_UNREACHABLE && GRADIENT_AT_GOAL - expected[i] <= depth))
							REQUIRE(partial[i] == expected[i]);
				}
				partials += !resumed.complete();
				for (size_t i = 0; i < cells; i += 211)
				{
					resumed.resolve(i);
					REQUIRE(partial[i] == expected[i]);
				}
				resumed.finish();
				requireSameCells(partial, expected, what + " resumed");
				REQUIRE(resumed.poppedEntries() == search.poppedEntries());
			}
	REQUIRE(locked > 0);
	REQUIRE(clearing > 0);
	REQUIRE(partials > 0);

	std::printf("PASS kernel equivalence terrain=%d: %d locked, %d clearing, %d partial\n",
		int(config), locked, clearing, partials);
}

// ── Scheduled building gradients ──
// Each tick mirrors Game::syncStep: publication, team stepping (where units
// ask for fields and so request refreshes), the step counter, staging, and the
// observation boundary that captures and submits the staged jobs.
struct Scheduler
{
	Game& game;
	Map& map;
	Scheduler(World& world, unsigned workers, unsigned delay) : game(world.game), map(world.game.map)
	{
		game.gameHeader.setBuildingGradientDelay(delay);
		map.configureCompute(workers + 1);
		map.configureGradientPipeline(workers, 8);
	}
	template <class Teams> void tick(Teams&& teams)
	{
		map.advanceGradientPipeline();
		teams();
		++game.stepCounter;
		map.stagePeriodicGradientPreparation();
		map.preparePendingGradient();
	}
	void tick() { tick([] {}); }
	Map::BuildingGradientPipelineStatus status() const { return map.buildingGradientPipelineStatus(); }
	size_t cells() const { return size_t(map.getW()) * map.getH(); }
	std::vector<Uint16> field(const Uint16* values) const { return values ? std::vector<Uint16>(values, values + cells()) : std::vector<Uint16>(); }
	// A full field equal to what one synchronous build would leave.
	std::vector<Uint16> finished(Building* b, int swim, BuildingRoute route = BuildingRoute::Footprint)
	{
		map.finishBuildingGradient(b, swim, route);
		return field(b->globalGradient[b->routeSlot(swim, route)]);
	}
	Uint16 at(Building* b, int x, int y, int slot = 0) const { return b->globalGradient[slot][map.coordToIndex(x, y)]; }
};

std::string saveRuntime(Map& map, bool text = false)
{
	auto* backend = new GAGCore::MemoryStreamBackend;
	std::unique_ptr<GAGCore::OutputStream> out(text
		? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend))
		: static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(backend)));
	map.saveRuntimeState(out.get());
	out->flush();
	return backend->takeContents();
}

void loadRuntime(Map& map, const std::string& bytes, bool text = false)
{
	auto* backend = new GAGCore::MemoryStreamBackend;
	backend->write(bytes.data(), bytes.size());
	backend->seekFromStart(0);
	std::unique_ptr<GAGCore::InputStream> in(text
		? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(backend))
		: static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(backend)));
	map.loadRuntimeState(in.get(), VERSION_MINOR);
}

// 1. Fixed-deadline publication across worker counts and delays.
void scheduledFixedDeadlines()
{
	for (unsigned workers : {0u, 1u, 2u, 4u, 8u})
		for (unsigned delay : {1u, 4u, 8u})
		{
			World world;
			Scheduler s(world, workers, delay);
			Building* b = world.place(24, 24);
			int dist = 0;
			require(world.available(b, 2, 2), "cold field is built synchronously");
			const auto synchronous = s.status().synchronous;
			s.game.stepCounter = DIRTY_GRACE_TICKS + 1;
			s.map.addForbidden(10, 10, 0);
			s.tick([&] {
				s.map.buildingAvailable(b, 0, 2, 2, &dist);
				require(s.status().queued == 1, "a stale field requests a refresh");
			});
			const Uint32 captured = s.game.stepCounter;
			require(s.status().pending == 1 && s.status().jobs == 1, "the request is staged after the tick");
			for (unsigned i = 1; i < delay; ++i)
				s.tick([&] {
					require(s.at(b, 10, 10) != GRADIENT_FORBIDDEN && s.status().published == 0, "old field until the deadline");
					s.map.buildingAvailable(b, 0, 2, 2, &dist);
				});
			s.map.advanceGradientPipeline();
			const auto status = s.status();
			require(status.published == 1 && status.jobs == 1 && status.discarded == 0, "one publication despite repeated requests");
			require(s.at(b, 10, 10) == GRADIENT_FORBIDDEN, "replacement visible on the deadline");
			require(b->lastGlobalGradientUpdateStepCounter[0] == captured, "the field's age is its capture tick");
			require(status.synchronous == synchronous, "no owner rebuild");
			const auto published = s.finished(b, 0);
			s.map.updateGlobalGradient(b, 0);
			require(published == s.finished(b, 0), "published field equals a synchronous build of the captured world");
		}
	std::puts("PASS scheduled building fields publish at fixed deadlines across workers 0/1/2/4/8 and delays 1/4/8");
}

// 2. A request made during team stepping is captured at the boundary.
void scheduledCaptureAtBoundary()
{
	World world;
	Scheduler s(world, 2, 4);
	Building* b = world.place(24, 24);
	require(world.available(b, 2, 2), "cold field");
	s.game.stepCounter = DIRTY_GRACE_TICKS + 1;
	s.map.addForbidden(10, 10, 0);
	s.tick([&] {
		require(world.available(b, 2, 2), "old field still serves");
		require(s.status().queued == 1 && s.status().pending == 0, "queued, not captured, during team stepping");
		s.map.addForbidden(11, 10, 0); // Later in the same tick: part of the capture.
	});
	s.map.addForbidden(12, 10, 0); // After the boundary: not part of it.
	for (int i = 1; i < 4; ++i) s.tick();
	s.map.advanceGradientPipeline();
	require(s.status().published == 1, "published");
	require(s.at(b, 10, 10) == GRADIENT_FORBIDDEN && s.at(b, 11, 10) == GRADIENT_FORBIDDEN, "the capture follows the whole tick");
	require(s.at(b, 12, 10) != GRADIENT_FORBIDDEN, "the capture precedes later edits");
	require(b->gradientGeneration[0] != s.map.topologyGeneration, "a later edit leaves the published field stale");
	std::puts("PASS requests during team stepping are captured at the observation boundary");
}

// 3. Partial fields resume on the owner from transferred buckets. The depth
// setting moves work between worker and owner, never values.
void scheduledPartialResume()
{
	enum class Depth { Table, Full, Lazy };
	std::vector<Uint16> reference;
	for (unsigned workers : {0u, 2u})
	for (Depth mode : {Depth::Table, Depth::Full, Depth::Lazy})
	{
		World world(8);
		Scheduler s(world, workers, 4);
		s.map.setBuildingGradientDepth(mode == Depth::Full ? "full" : mode == Depth::Lazy ? "lazy" : "table");
		const int pool = globalContainer->buildingsTypes.getTypeNum("swimmingpool", 0, true);
		require(pool >= 0, "swimming pool site type");
		Building* b = s.game.addBuilding(100, 100, pool, 0, 1, 1);
		require(b != nullptr, "pool site placed");
		int dist = 0;
		require(s.map.buildingAvailable(b, 0, 98, 98, &dist), "cold field");
		const int depth = s.map.predictBuildingDepth(b, 0);
		require((mode == Depth::Full) == (depth == gradient_kernel::COST_LIMIT), "only full depth settles everything");
		s.game.stepCounter = DIRTY_GRACE_TICKS + 1;
		s.map.addForbidden(10, 10, 0);
		s.tick([&] { s.map.buildingAvailable(b, 0, 98, 98, &dist); });
		for (int i = 1; i < 4; ++i) s.tick();
		s.map.advanceGradientPipeline();
		require(s.status().published == 1, "published");
		auto* search = b->globalGradientSearch[0].get();
		// The table may settle a small map entirely; lazy publishes seeds only.
		const bool partial = !search || !search->complete();
		require(search && (mode == Depth::Full ? !partial : mode == Depth::Lazy ? partial : true)
			&& (!partial || search->settledCost() > depth), "the published field carries its frontier");
		const auto workerPopped = search->poppedEntries();
		require(s.map.buildingAvailable(b, 0, 230, 230, &dist), "far query resolves on the owner");
		require(!partial || search->poppedEntries() > workerPopped, "the owner continues the transferred search");
		const auto resumed = s.finished(b, 0);
		const auto resumedPopped = b->globalGradientSearch[0]->poppedEntries();
		s.map.updateGlobalGradient(b, 0);
		const auto full = s.finished(b, 0);
		require(resumed == full, "resumed values equal a full build");
		require(resumedPopped == b->globalGradientSearch[0]->poppedEntries(), "no rescan: the same entries as one full build");
		if (reference.empty()) reference = resumed;
		require(resumed == reference, "every depth setting publishes the same values");
		std::printf("PASS scheduled field depth mode %d (depth %d, worker popped %llu of %llu) resumes from transferred buckets\n",
			int(mode), depth, (unsigned long long)workerPopped, (unsigned long long)resumedPopped);
	}
}

// 4. Synchronous rebuilds and lifecycle hooks supersede; jobs share a deadline.
void scheduledSupersession()
{
	World world;
	Scheduler s(world, 4, 4);
	Building* a = world.place(24, 24);
	Building* b = world.place(40, 40);
	Building* c = world.place(8, 40);
	Building* d = world.place(40, 8);
	for (Building* x : {a, b, c, d}) require(world.available(x, 2, 2), "cold field");
	s.game.stepCounter = 600; // Past the idle eviction age as well.
	s.map.addForbidden(10, 10, 0);
	s.tick([&] { for (Building* x : {a, b, c, d}) world.available(x, 2, 2); });
	require(s.status().pending == 4 && s.status().jobs == 4, "four jobs admitted in one tick");
	s.tick([&] {
		s.map.updateGlobalGradient(a, 0);    // synchronous rebuild
		c->resetPathfindGradients();         // lifecycle reset
		d->globalGradientUsedStep[0] = 0;
		d->freeIdleGradients();              // idle eviction
		b->dirtyGradients();                 // a later notification, after capture
	});
	require(!c->globalGradient[0] && !d->globalGradient[0], "dropped fields");
	const auto expectedA = s.finished(a, 0);
	for (int i = 2; i < 4; ++i) s.tick();
	s.map.advanceGradientPipeline();
	const auto status = s.status();
	require(status.published == 1 && status.discarded == 3, "only the unsuperseded job publishes at the shared deadline");
	require(s.finished(a, 0) == expectedA, "a stale result cannot overwrite a synchronous rebuild");
	require(!c->globalGradient[0] && !d->globalGradient[0], "dropped fields stay dropped");
	require(s.at(b, 10, 10) == GRADIENT_FORBIDDEN && b->dirtyGradient[0], "publication keeps notifications after the capture");
	require(!b->refreshRequested.any() && !a->refreshRequested.any(), "no request outlives its job");
	std::puts("PASS synchronous rebuilds, resets and evictions supersede; several jobs share a deadline");
}

// 5. Pending results survive save boundaries and cannot resurrect evicted fields.
void scheduledSaveBoundaries()
{
	for (unsigned workers : {0u, 1u, 2u})
		for (unsigned phase = 0; phase < 4; ++phase)
			for (bool text : {false, true})
			{
				if (text && workers) continue;
				World source, restored;
				Scheduler s(source, workers, 4), r(restored, workers, 4);
				std::vector<Building*> buildings[2];
				for (auto* x : {&s, &r})
				{
					World& w = x == &s ? source : restored;
					auto& list = buildings[x == &s ? 0 : 1];
					list = {w.place(24, 24), w.place(40, 40)};
					for (Building* b : list) require(w.available(b, 2, 2), "cold field");
					x->game.stepCounter = 600;
					x->map.addForbidden(10, 10, 0);
					x->tick([&] { for (Building* b : list) w.available(b, 2, 2); });
					list[1]->globalGradientUsedStep[0] = 0;
					list[1]->freeIdleGradients(); // evicted while its job is pending
				}
				for (unsigned p = 0; p < phase; ++p) { s.tick(); r.tick(); }
				const auto bytes = saveRuntime(s.map, text);
				require(s.status().pending == 2, "saving keeps both jobs pending");
				loadRuntime(r.map, bytes, text);
				require(r.status().pending == 2, "loading restores the pending jobs");
				require(saveRuntime(r.map, text) == bytes, "a restored pipeline saves the same bytes");
				for (unsigned p = phase; p < 3; ++p)
				{
					require(r.at(buildings[1][0], 10, 10) != GRADIENT_FORBIDDEN, "no early publication after loading");
					s.tick(); r.tick();
				}
				s.map.advanceGradientPipeline(); r.map.advanceGradientPipeline();
				for (int i : {0, 1})
				{
					Scheduler& x = i ? r : s;
					Building* b = buildings[i][0];
					require(x.status().published == 1, "restored result publishes at its original deadline");
					require(x.at(b, 10, 10) == GRADIENT_FORBIDDEN && b->lastGlobalGradientUpdateStepCounter[0] == 601, "same capture");
					require(!buildings[i][1]->globalGradient[0], "an evicted destination stays absent");
				}
				require(s.finished(buildings[0][0], 0) == r.finished(buildings[1][0], 0), "same published field");
				require(saveRuntime(s.map, text) == saveRuntime(r.map, text), "continuation and resumed runs agree");
			}
	std::puts("PASS pending building results survive saves at phases 0..3 (workers 0/1/2, binary and text) without resurrection");
}

// A header delay changed with an empty pipeline (a fork, or a new header before
// the first tick) saves the new delay, and an empty saved section of another
// delay still loads: it carries no deadlines to remap.
void scheduledEmptyPipelineFollowsHeaderDelay()
{
	World source, restored;
	Scheduler s(source, 1, 4), r(restored, 1, 2);
	s.tick(); r.tick();
	require(s.status().pending == 0 && s.status().queued == 0, "nothing in flight");
	const auto before = saveRuntime(s.map);
	loadRuntime(r.map, before); // saved delay 4, match rules 2, nothing pending
	require(r.status().delay == 2, "the loaded pipeline follows the header");
	s.game.gameHeader.setBuildingGradientDelay(2);
	const auto after = saveRuntime(s.map);
	require(s.status().delay == 2, "saving applies the changed header first");
	loadRuntime(r.map, after);
	require(saveRuntime(r.map) == after, "the re-headered save round trips");
	std::puts("PASS an empty building pipeline follows a changed header delay through save and load");
}

// 6. A deleted building's pending result cannot replace a reused destination.
void scheduledReusedDestination()
{
	for (unsigned workers : {0u, 4u})
	{
		World world;
		Scheduler s(world, workers, 4);
		Building* old = world.place(24, 24);
		require(world.available(old, 2, 2), "cold field");
		const auto gid = old->gid;
		const auto identity = old->scriptIdentity;
		s.game.stepCounter = 200;
		s.map.addForbidden(10, 10, 0);
		s.tick([&] { world.available(old, 2, 2); });
		require(s.status().pending == 1, "pending");
		world.remove({old});
		s.map.setMapDiscovered();
		Building* replacement = s.game.addBuilding(40, 40, world.siteType, 0, 1, 1);
		require(replacement && replacement->gid == gid && replacement->scriptIdentity != identity, "destination reused with a new lifetime");
		require(world.available(replacement, 2, 2), "replacement's cold field");
		const auto field = s.finished(replacement, 0);
		for (int i = 1; i < 4; ++i) s.tick();
		s.map.advanceGradientPipeline();
		require(s.status().discarded == 1 && s.finished(replacement, 0) == field, "the old result is discarded untouched");
	}
	std::puts("PASS a deleted building's pending result cannot replace a reused destination");
}

// 7. Access metadata follows the newest capture across swim classes and saves.
void scheduledAccessMetadata()
{
	World world;
	Scheduler s(world, 2, 4);
	Building* b = world.place(24, 24);
	int dist = 0;
	for (int swim : {1, 3}) require(s.map.buildingAvailable(b, swim, 2, 2, &dist), "cold field");
	const int access = b->routeAccess(1, BuildingRoute::Footprint);
	require(access == b->routeAccess(3, BuildingRoute::Footprint), "classes 1 and 3 share one access variant");
	std::vector<std::pair<int, int>> fence;
	for (int y = 23; y <= 24 + world.siteH; ++y)
		for (int x = 23; x <= 24 + world.siteW; ++x)
			if (x == 23 || y == 23 || x == 24 + world.siteW || y == 24 + world.siteH) fence.push_back({x, y});
	auto paint = [&](bool on) { for (auto [x, y] : fence) on ? s.map.addForbidden(x, y, 0) : s.map.removeForbidden(x, y, 0); };
	s.game.stepCounter = DIRTY_GRACE_TICKS + 1;
	paint(true);
	s.tick([&] { s.map.buildingAvailable(b, 1, 2, 2, &dist); }); // captures the fence: locked
	s.tick([&] {
		paint(false);
		s.map.updateGlobalGradient(b, 3); // stamped with the capture's tick, but newer
		require(!b->locked[access], "synchronous build sees the open fence");
	});
	for (int i = 2; i < 4; ++i) s.tick();
	s.map.advanceGradientPipeline();
	require(s.status().published == 1 && !b->locked[access], "an older capture cannot overwrite newer access metadata");
	s.game.stepCounter += DIRTY_GRACE_TICKS;
	paint(true);
	s.tick([&] { s.map.buildingAvailable(b, 1, 2, 2, &dist); });
	for (int i = 1; i < 4; ++i) s.tick();
	s.map.advanceGradientPipeline();
	require(s.status().published == 2 && b->locked[access], "the newest capture writes access metadata");
	const auto bytes = saveRuntime(s.map);
	loadRuntime(s.map, bytes);
	require(b->locked[access], "saves keep the published metadata");
	std::puts("PASS access metadata follows the newest capture across swim classes and saves");
}

// 8. With the pipeline on, a team-wide reset or a forbidden-area paint keeps
// the building's walking field serving and refreshes it on schedule, at once
// and with no owner build. A building's own change still drops it, and with
// the pipeline off every reset drops it.
void scheduledResetsKeepStaleFields()
{
	for (auto cause : {Building::GradientDrop::Area, Building::GradientDrop::Team})
	for (unsigned workers : {0u, 2u})
	{
		World world;
		Scheduler s(world, workers, 4);
		Building* b = world.place(24, 24);
		require(world.available(b, 2, 2), "cold walking field");
		s.tick(); // Configures the pipeline.
		s.game.stepCounter += 300;
		s.map.updateGlobalGradient(b, 0);
		const Uint16* walking = b->globalGradient[0];
		const auto values = s.finished(b, 0);
		s.game.stepCounter += 10; // Inside the dirty throttle: a kept field is due anyway.
		s.map.addForbidden(10, 10, 0);
		b->resetPathfindGradients(cause);
		require(b->globalGradient[0] == walking && s.finished(b, 0) == values, "the stale field keeps serving");
		const auto before = s.status();
		Uint32 captured = 0;
		s.tick([&] {
			require(world.available(b, 2, 2) && s.status().queued == 1, "served while a refresh is requested");
			captured = s.game.stepCounter + 1;
		});
		require(s.status().pending == 1 && s.status().synchronous == before.synchronous, "one scheduled refresh, no owner build");
		for (int i = 1; i < 4; ++i) s.tick([&] { world.available(b, 2, 2); });
		s.map.advanceGradientPipeline();
		const auto after = s.status();
		require(after.published == before.published + 1 && after.synchronous == before.synchronous, "published on schedule");
		require(s.at(b, 10, 10) == GRADIENT_FORBIDDEN && b->lastGlobalGradientUpdateStepCounter[0] == captured,
			"refreshed from the capture");
		const auto published = s.finished(b, 0);
		s.map.updateGlobalGradient(b, 0);
		require(published == s.finished(b, 0), "the published field equals a synchronous build");
		b->resetPathfindGradients(Building::GradientDrop::Own);
		require(!b->globalGradient[0], "an own reset drops the field");
		require(world.available(b, 2, 2), "rebuilt cold");
		const auto reasons = s.status().synchronousByReason;
		require(reasons[size_t(Map::BuildingSyncReason::ColdOwn)] == 1, "the cold rebuild is counted by its drop cause");
	}
	{
		World world;
		Building* b = world.place(24, 24);
		require(world.available(b, 2, 2), "cold field");
		b->resetPathfindGradients(Building::GradientDrop::Area);
		require(!b->globalGradient[0], "with the pipeline off an area reset drops the field");
	}
	std::puts("PASS area and team-wide resets keep stale walking fields serving and refresh them on schedule");
}

// 9. Slow and failing workers never move the publication tick.
void slowWorker(const building_gradient::Job&) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); }
void failingWorker(const building_gradient::Job&) { throw std::runtime_error("injected building gradient failure"); }

void scheduledSlowAndFailingWorkers()
{
	std::vector<Uint16> reference;
	for (auto hook : {(void (*)(const building_gradient::Job&))nullptr, &slowWorker})
		for (unsigned workers : {0u, 2u})
		{
			building_gradient::workHook = hook;
			World world;
			Scheduler s(world, workers, 4);
			Building* b = world.place(24, 24);
			require(world.available(b, 2, 2), "cold field");
			s.game.stepCounter = DIRTY_GRACE_TICKS + 1;
			s.map.addForbidden(10, 10, 0);
			s.tick([&] { world.available(b, 2, 2); });
			for (int i = 1; i < 4; ++i) s.tick([&] { require(s.status().published == 0, "never early"); });
			s.map.advanceGradientPipeline();
			require(s.status().published == 1, "never late");
			const auto field = s.finished(b, 0);
			if (reference.empty()) reference = field;
			require(field == reference, "same result whatever the worker's timing");
		}
	for (bool atSave : {false, true})
		for (unsigned workers : {0u, 2u})
		{
			building_gradient::workHook = &failingWorker;
			World world;
			Scheduler s(world, workers, 4);
			Building* b = world.place(24, 24);
			require(world.available(b, 2, 2), "cold field");
			s.game.stepCounter = DIRTY_GRACE_TICKS + 1;
			s.map.addForbidden(10, 10, 0);
			s.tick([&] { world.available(b, 2, 2); });
			for (int i = 1; i < 4; ++i) s.tick();
			bool thrown = false;
			try { atSave ? (void)saveRuntime(s.map) : s.map.advanceGradientPipeline(); }
			catch (const std::runtime_error&) { thrown = true; }
			require(thrown, atSave ? "a failure surfaces at save" : "a failure surfaces at publication");
			require(s.at(b, 10, 10) != GRADIENT_FORBIDDEN, "a failed result never publishes");
			building_gradient::workHook = nullptr;
		}
	building_gradient::workHook = nullptr;
	std::puts("PASS slow and failing workers never change the publication tick; failures surface at publication or save");
}
// 10. A team-local forbidden edit carries other teams' current fields forward
// to the new topology generation (Game::executeAlterForbidden). A pending
// result captured before the edit moves with them, so it is not stale on
// arrival, as the synchronous path's field is not.
void scheduledForbiddenEditCarriesPendingGeneration()
{
	auto paint = [](World& w) {
		Utilities::BitArray mask(1);
		mask.set(0, true);
		std::shared_ptr<Order> order(new OrderAlterForbidden(0, BrushTool::MODE_ADD, 5, 5, 1, 1, mask));
		order->sender = 0;
		w.game.executeOrder(order, 0);
	};
	{
		World world; // synchronous reference
		Building* rival = world.place(40, 40, 1);
		require(world.available(rival, 2, 2), "cold field");
		world.game.stepCounter = DIRTY_GRACE_TICKS + 1;
		world.game.map.addForbidden(10, 10, 0);
		require(world.available(rival, 2, 2), "synchronous refresh");
		paint(world);
		require(rival->gradientGeneration[0] == world.game.map.topologyGeneration,
			"synchronous: the team-local edit keeps the rival's field current");
	}
	World world;
	Scheduler s(world, 2, 4);
	Building* rival = world.place(40, 40, 1);
	require(world.available(rival, 2, 2), "cold field");
	s.game.stepCounter = DIRTY_GRACE_TICKS + 1;
	s.map.addForbidden(10, 10, 0);
	s.tick([&] { world.available(rival, 2, 2); });
	require(s.status().pending == 1, "the rival's refresh is pending");
	paint(world);
	for (int i = 1; i < 4; ++i) s.tick();
	s.map.advanceGradientPipeline();
	require(s.status().published == 1, "published");
	require(rival->gradientGeneration[0] == s.map.topologyGeneration,
		"scheduled: the published field is current like the synchronous one");
	std::puts("PASS a pending result keeps the generation a team-local edit carries forward");
}

// 11. Two swim classes of one access variant captured at the same
// boundary publish in request order, and the later one writes the shared
// access metadata, as the later synchronous build does. A boundary repeated
// within one tick (direct map stepping) gives the two captures different
// worlds, so the order is observable.
void scheduledSameTickSwimClassesKeepRequestOrder()
{
	auto fence = [](World& world, bool on) {
		for (int y = 23; y <= 24 + world.siteH; ++y)
			for (int x = 23; x <= 24 + world.siteW; ++x)
				if (x == 23 || y == 23 || x == 24 + world.siteW || y == 24 + world.siteH)
					on ? world.game.map.addForbidden(x, y, 0) : world.game.map.removeForbidden(x, y, 0);
	};
	int dist = 0;
	{
		World world; // synchronous reference
		Building* b = world.place(24, 24);
		for (int swim : {1, 3}) require(world.game.map.buildingAvailable(b, swim, 2, 2, &dist), "cold field");
		const int access = b->routeAccess(1, BuildingRoute::Footprint);
		auto paint = [&](bool on) { fence(world, on); };
		world.game.stepCounter = DIRTY_GRACE_TICKS + 1;
		paint(true);
		world.game.map.buildingAvailable(b, 1, 2, 2, &dist);
		require(b->locked[access], "synchronous: the fenced build locks");
		paint(false);
		world.game.map.buildingAvailable(b, 3, 2, 2, &dist);
		require(!b->locked[access], "synchronous: the later build of the same tick wins");
	}
	World world;
	Scheduler s(world, 2, 4);
	Building* b = world.place(24, 24);
	for (int swim : {1, 3}) require(s.map.buildingAvailable(b, swim, 2, 2, &dist), "cold field");
	const int access = b->routeAccess(1, BuildingRoute::Footprint);
	require(access == b->routeAccess(3, BuildingRoute::Footprint), "classes 1 and 3 share one access variant");
	auto paint = [&](bool on) { fence(world, on); };
	s.game.stepCounter = DIRTY_GRACE_TICKS + 1;
	paint(true);
	s.tick([&] { s.map.buildingAvailable(b, 1, 2, 2, &dist); }); // captures the fence
	paint(false);
	s.map.buildingAvailable(b, 3, 2, 2, &dist); // same tick, open fence
	s.map.stagePeriodicGradientPreparation();
	s.map.preparePendingGradient();
	require(s.status().pending == 2, "both swim classes are pending with one capture tick");
	require(b->lastGlobalGradientUpdateStepCounter[b->routeSlot(1, BuildingRoute::Footprint)] < s.game.stepCounter, "nothing published yet");
	for (int i = 1; i < 4; ++i) s.tick();
	s.map.advanceGradientPipeline();
	require(s.status().published == 2, "both published at one deadline");
	require(!b->locked[access], "scheduled: the later capture of the same tick wins, like the synchronous build");
	std::puts("PASS same-tick swim classes write access metadata in request order");
}

}

TEST_SUITE("BuildingGradientInvalidation")
{
	TEST_CASE("prediction retains reader demand across rebuilds and save completion")
	{
		glob2test::HeadlessGlobals globals;
		World world;
		Scheduler scheduler(world, 2, 1);
		auto& map = world.game.map;
		map.setBuildingGradientDepth("table");
		Building* building = world.place(24, 24);
		REQUIRE(world.available(building, 2, 2));
		const int demand = building->globalGradientSearch[0]->requiredCost();
		REQUIRE(demand > 0);
		const int predicted = map.predictBuildingDepth(building, 0);
		map.finishBuildingGradient(building, 0);
		CHECK(map.predictBuildingDepth(building, 0) == predicted);
		CHECK(building->globalGradientSearch[0]->requiredCost() == demand);
		world.game.stepCounter = DIRTY_GRACE_TICKS + 1;
		map.addForbidden(10, 10, 0);
		scheduler.tick([&] { REQUIRE(world.available(building, 2, 2)); });
		map.advanceGradientPipeline();
		REQUIRE(scheduler.status().published == 1);
		CHECK(building->settledCostHint[0] == demand);
		CHECK(building->globalGradientSearch[0]->requiredCost() == 0);
		CHECK(map.predictBuildingDepth(building, 0) == BuildingGradientDepth::target(0, demand));
		map.updateGlobalGradient(building, 0);
		CHECK(building->settledCostHint[0] == 0);
		CHECK(map.predictBuildingDepth(building, 0) == BuildingGradientDepth::target(0, 0));
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
	TEST_CASE("snapshot building seeds and captured-input searches match the synchronous kernels for every route and swim class")
	{
		for (auto config : {KernelTerrain::BuiltinPlain, KernelTerrain::BuiltinModified,
				KernelTerrain::CustomPlain, KernelTerrain::CustomModified})
		{
			glob2test::HeadlessGlobals globals;
			scheduledKernelsMatchSynchronous(config);
		}
	}
	TEST_CASE("scheduled building fields publish at fixed deadlines across worker counts and delays")
	{
		glob2test::HeadlessGlobals globals;
		scheduledFixedDeadlines();
	}
	TEST_CASE("requests during team stepping are captured at the observation boundary")
	{
		glob2test::HeadlessGlobals globals;
		scheduledCaptureAtBoundary();
	}
	TEST_CASE("partial scheduled fields resume on the owner from transferred buckets")
	{
		glob2test::HeadlessGlobals globals;
		scheduledPartialResume();
	}
	TEST_CASE("synchronous rebuilds and lifecycle hooks supersede pending jobs that share a deadline")
	{
		glob2test::HeadlessGlobals globals;
		scheduledSupersession();
	}
	TEST_CASE("pending building results survive save boundaries and cannot resurrect evicted fields")
	{
		glob2test::HeadlessGlobals globals;
		scheduledSaveBoundaries();
	}
	TEST_CASE("a deleted building's pending result cannot replace a reused destination")
	{
		glob2test::HeadlessGlobals globals;
		scheduledReusedDestination();
	}
	TEST_CASE("access metadata follows the newest capture across swim classes and saves")
	{
		glob2test::HeadlessGlobals globals;
		scheduledAccessMetadata();
	}
	TEST_CASE("area and team-wide resets keep stale walking fields serving with the pipeline on")
	{
		glob2test::HeadlessGlobals globals;
		scheduledResetsKeepStaleFields();
	}
	TEST_CASE("slow and failing workers never change the publication tick")
	{
		glob2test::HeadlessGlobals globals;
		scheduledSlowAndFailingWorkers();
	}
	TEST_CASE("a pending result keeps the generation a team-local edit carries forward")
	{
		glob2test::HeadlessGlobals globals;
		scheduledForbiddenEditCarriesPendingGeneration();
	}
	TEST_CASE("same-tick swim classes write access metadata in request order")
	{
		glob2test::HeadlessGlobals globals;
		scheduledSameTickSwimClassesKeepRequestOrder();
	}
	TEST_CASE("an empty building pipeline follows a changed header delay through save and load")
	{
		glob2test::HeadlessGlobals globals;
		scheduledEmptyPipelineFollowsHeaderDelay();
	}
}
