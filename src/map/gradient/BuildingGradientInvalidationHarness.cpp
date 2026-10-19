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
#include "GradientFixture.h"
#include "LegacyPopulationOutput.h"

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
#include <sstream>
#include <tuple>
#include <memory>
#include <chrono>
#include <stdexcept>
#include <thread>
#include "BuildingGradientJob.h"
#include "GradientExecutionAudit.h"
#include "Version.h"
#include "ResourceGrowth.h"
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
	bool scheduled = false;
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
		for (Building* b : owner->liveBuildings.entries())
		{
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

	// Explicit reference setup for warm-refresh tests. Cold-publication tests use
	// requestBuildingField directly and never call this synchronous helper.
	void warm(Building* b, int swim = 0)
	{
		const int slot = b->routeSlot(swim, BuildingRoute::Automatic);
		if (!b->globalGradient[slot]) b->globalGradient[slot] = game.map.acquireBuildingGradientBuffer();
		game.map.updateGlobalGradient(b, swim);
		game.map.finishBuildingGradient(b, swim);
	}

	bool available(Building* b, int x, int y)
	{
		int dist = -1;
		if (!scheduled)
		{
			const auto captured=b->lastGlobalGradientUpdateStepCounter[0];
			const bool refresh=(b->dirtyGradient[0] || b->gradientGeneration[0]!=game.map.topologyGeneration)
				&& captured+DIRTY_GRACE_TICKS<=game.stepCounter;
			glob2test::awaitGradient(game,[&] {
				game.map.requestBuildingField(b,0);
				return refresh && b->lastGlobalGradientUpdateStepCounter[0]==captured ? nullptr : b->globalGradient[0];
			});
		}
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
	world.warm(first);
	const Uint16* firstField = world.game.map.buildingGradient(first, 0);
	require(firstField != nullptr, "first building field exists");
	BuildingGradientSearch* firstSearch = first->globalGradientSearch[0].get();
	require(firstSearch != nullptr, "first building search exists");
	const std::vector<Uint16> expected(firstField, firstField + cells);

	world.tick(768);
	first->freeIdleGradients();
	require(first->globalGradient[0] == nullptr, "idle building drops its field");
	world.warm(second);
	const Uint16* secondField = world.game.map.buildingGradient(second, 0);
	require(secondField == firstField, "another building reuses the idle field storage");
	require(second->globalGradientSearch[0].get() == firstSearch,
		"another building reuses the idle search queues");
	require(std::vector<Uint16>(secondField, secondField + cells) != expected,
		"reused storage contains the second building's field");
	world.warm(first);
	const Uint16* rebuilt = world.game.map.buildingGradient(first, 0);
	require(std::vector<Uint16>(rebuilt, rebuilt + cells) == expected,
		"reactivated building rebuilds the same route values");
	const std::vector<Uint16> secondExpected(secondField, secondField + cells);
	BuildingGradientSearch* secondSearch = second->globalGradientSearch[0].get();
	second->resetPathfindGradients();
	require(second->globalGradient[0] == nullptr, "invalidation drops the old field");
	world.warm(second);
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
	world.tick(DIRTY_GRACE_TICKS);
	require(world.available(centre, 10, 10), "clearing the ring makes the site available after refresh publication");
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
        // Water-only callers need water outside the inn's occupied grass island.
        // Retain the original all-grass setup for walking/swimming classes0-6.
        if(swim==SWIM_CLASS_COUNT-1)
            for(int y=0;y<map.getH();++y)
                for(int x=0;x<map.getW();++x)
                    if(map.getBuilding(x,y)==NOGBID) map.paintCell(x,y,WATER);
		world.warm(centre,swim);
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

static void preparePeriodic(Map& map)
{
    map.stagePeriodicGradientPreparation();
    map.game->snapshotStore().invalidateBoundary();
    map.preparePendingGradient(map.game->snapshotStore().captureBoundary(*map.game));
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
		const auto reference=[&]() {
			std::vector<Uint16> values(cells);
			if(kind==0) map.seedMaterialGradient(0, 0, swim, values.data());
			else if(kind==1) map.seedGuardAreasGradient(0, swim, values.data());
			else map.seedClearAreasGradient(0, swim, values.data());
			map.propagateGradient(values.data(), swim);
			return values;
		};
		const auto original=reference();
		map.configureCompute(workers+1);
		map.configureGradientPipeline(workers, 3);
		const auto publish=[&]() { ++world.game.stepCounter; map.advanceGradientPipeline(); };
		const auto capture=[&]() { map.stagePeriodicGradientPreparation(); glob2test::prepareGradientBoundary(world.game); };
		require(!field(), "cold read enrolls without publishing a placeholder");
		publish(); capture(); // Admission at advance 1, publication at advance 4.
		require(map.gradientPipelineStatus().jobs==1, "pipeline scheduled a real field");
		map.addForbidden(41, 40, 0);
		refresh();
		publish(); require(!field(), "first captured field remains pending at advance 2");
		publish(); require(!field(), "first captured field remains pending at advance 3");
		publish();
		require(field()!=nullptr, "cold field publishes at its original deadline despite a soft edit");
		require(map.gradientPipelineStatus().discarded==0, "soft changes do not supersede private work");
		require(std::vector<Uint16>(field(),field()+cells)==original, "first publication uses its original capture");
		const auto frozen=reference();
		require(frozen!=original, "the changed world requires a different field");
        // Warm periodic fields retain the shared warm-admission minimum since the
        // captured generation. Reach eligibility before testing its new deadline.
        while(world.game.stepCounter<1+AdaptiveGradientState::MinimumInterval) publish();
		refresh(); capture();

		map.paintCell(55, 55, GRASS); // Workers must use captured water, not this live edit.
		publish(); publish();
		require(std::vector<Uint16>(field(),field()+cells)==original, "refresh cannot publish before its deadline");
		publish();
		require(std::vector<Uint16>(field(),field()+cells)==frozen, "terrain changes do not alter a pending snapshot");
        while(world.game.stepCounter<1+2*AdaptiveGradientState::MinimumInterval) publish();
		refresh(); capture(); // Destruction must safely drain private work.
        require(map.gradientPipelineStatus().pending==1, "teardown retains an admitted private refresh");

	}
	std::puts("PASS cold and soft-stale resource/guard/clear deadlines, captured terrain, teardown");
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
		b->clearingMaterialMask=0;
		b->setClearsMaterial(WOOD,true); b->setClearsMaterial(ALGA,true);
		buildings.push_back(b);
		return b;
	};
	add("inn", 4, 4); add("hospital", 14, 4); add("racetrack", 24, 4); add("swimmingpool", 36, 4);
	add("school", 4, 20); add("explorationflag", 20, 20); add("warflag", 30, 22); add("clearingflag", 40, 24);
	add("clearingflag", 50, 50)->setClearsMaterial(ALGA,false);
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
		SimulationSnapshot::Simulation);
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
				const auto inputs = BuildingGradientSearch::Inputs::ofSnapshot(snapshot, swim);
				BuildingGradientSearch search;
				const auto result = buildBuilding(seed, snapshot, inputs, actual.data(), search, gradient_kernel::COST_LIMIT);
				INFO(what);
				REQUIRE(result.locked == b->isLocked(access));
				if (route == BuildingRoute::Clearing) { REQUIRE(int(result.resourceState) == b->anyResourceToClear[swimAccessVariant(swim)]); ++clearing; }
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
				REQUIRE(resumed.settledCost() == search.settledCost());
			}
	REQUIRE(locked > 0);
	REQUIRE(clearing > 0);
	REQUIRE(partials > 0);

	std::printf("PASS kernel equivalence terrain=%d: %d locked, %d clearing, %d partial\n",
		int(config), locked, clearing, partials);
}

// ── Scheduled building gradients ──
// Isolated pipeline tests below count publication advances directly, separating
// consumer requests from captured input mutations. The real owner observation's
// Game-tick deadline and tick-zero metadata are pinned by a separate case below.
struct Scheduler
{
	Game& game;
	Map& map;
	Scheduler(World& world, unsigned workers, unsigned delay) : game(world.game), map(world.game.map)
	{
		world.scheduled = true;
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
		map.game->snapshotStore().invalidateBoundary(); map.preparePendingGradient(map.game->snapshotStore().captureBoundary(*map.game));
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

// Observe offsets while the real legacy loader consumes the retained binary.
// Section names are diagnostics only in BinaryInputStream, so this reader does
// not add, remove, or reinterpret any historical bytes.
struct LegacyAccessOffsetReader final : GAGCore::BinaryInputStream
{
    struct Flag { int team, building, swim; size_t offset; };
    std::vector<std::string> sections;
    std::vector<Flag> flags;
    explicit LegacyAccessOffsetReader(const std::string& bytes)
        : BinaryInputStream(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()))
    { seekFromStart(0); }
    void readEnterSection(const std::string name) override { sections.push_back(name); }
    void readEnterSection(unsigned id) override { sections.push_back(std::to_string(id)); }
    void readLeaveSection(size_t count=1) override
    {
        if(count>sections.size()) throw std::runtime_error("Unbalanced legacy fixture sections");
        sections.resize(sections.size()-count);
    }
    Uint8 readUint8(const std::string name) override
    {
        if(name=="locked")
            for(size_t i=0;i+6<sections.size();++i)
                if(sections[i]=="mapRuntime" && sections[i+1]=="teams"
                    && sections[i+3]=="buildings" && sections[i+5]=="access")
                {
                    flags.push_back({std::stoi(sections[i+2]),std::stoi(sections[i+4]),
                        std::stoi(sections[i+6]),getPosition()});
                    break;
                }
        return BinaryInputStream::readUint8(name);
    }
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

// A following field makes historical-layout tests detect unread observation
// markers, rather than silently accepting a current stream's legacy prefix.
constexpr Uint32 RuntimeSentinel = 0x51a7c0de;
std::string saveLegacyRuntime(Map& map, int version)
{
    auto* backend = new GAGCore::MemoryStreamBackend;
    glob2test::LegacyPopulationOutput output(backend, map, version);
    map.saveRuntimeState(&output);
    output.writeUint32(RuntimeSentinel, "followingSection");
    output.flush();
    return backend->takeContents();
}

void loadRuntime(Map& map, const std::string& bytes, bool text = false)
{
    auto backend=std::make_unique<GAGCore::MemoryStreamBackend>();
    backend->write(bytes.data(),bytes.size()); backend->seekFromStart(0);
    // Text readers are nonowning; binary readers delete their backend.
    if(text) {
        GAGCore::TextInputStream input(backend.get()); map.loadRuntimeState(&input,VERSION_MINOR);
    } else {
        GAGCore::BinaryInputStream input(backend.release()); map.loadRuntimeState(&input,VERSION_MINOR);
    }
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
			world.warm(b);
			require(world.available(b, 2, 2), "reference warm field");
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

void scheduledPerTeamQuotas()
{
    for (unsigned workers : {0u,2u,4u}) {
        World world;
        Scheduler scheduler(world,workers,4);
        std::vector<Building*> first,second;
        for (int i=0;i<6;++i) first.push_back(world.place(4+i*8,8,0));
        for (int i=0;i<2;++i) second.push_back(world.place(12+i*16,40,1));
        for (auto* list : {&first,&second})
            for (auto* b : *list) world.warm(b);
        scheduler.game.stepCounter=600;
        scheduler.tick([&] {
            // Team 0's excess requests precede every request from team 1.
            for (auto* list : {&first,&second})
                for (auto* b : *list) REQUIRE(scheduler.map.requestBuildingRefresh(b,0));
        });
        const auto firstCapture=scheduler.game.stepCounter;
        CHECK(scheduler.status().pending==6);
        CHECK(scheduler.status().queued==2);
        CHECK(scheduler.status().jobs==6);
        scheduler.tick();
        CHECK(scheduler.status().pending==8);
        CHECK(scheduler.status().queued==0);
        scheduler.tick(); scheduler.tick();
        CHECK(scheduler.status().published==0);
        scheduler.tick();
        CHECK(scheduler.status().published==6);
        for (unsigned i=0;i<first.size();++i)
            CHECK(first[i]->lastGlobalGradientUpdateStepCounter[0]==(i<4 ? firstCapture : 0));
        for (auto* b : second) CHECK(b->lastGlobalGradientUpdateStepCounter[0]==firstCapture);
        scheduler.tick();
        CHECK(scheduler.status().published==8);
        for (unsigned i=4;i<first.size();++i)
            CHECK(first[i]->lastGlobalGradientUpdateStepCounter[0]==firstCapture+1);
    }
}

void scheduledColdTeamQuotas()
{
    for (unsigned workers : {0u, 2u, 4u}) {
        World world;
        Scheduler scheduler(world, workers, 8);
        std::vector<Building*> buildings;
        for (int i=0; i<6; ++i) buildings.push_back(world.place(4+i*8,8));
        for (int i=0; i<2; ++i) buildings.push_back(world.place(12+i*16,40,1));
        scheduler.tick([&] {
            for (auto* b : buildings) REQUIRE(scheduler.map.requestBuildingRefresh(b,0));
        });
        CHECK(scheduler.status().pending==6);
        CHECK(scheduler.status().queued==2);
        for (auto* b : buildings) CHECK(b->globalGradient[0]==nullptr);
        scheduler.tick();
        CHECK(scheduler.status().published==6);
        CHECK(scheduler.status().pending==2);
        CHECK(scheduler.status().queued==0);
        for (unsigned i=0; i<buildings.size(); ++i)
            CHECK((buildings[i]->globalGradient[0]!=nullptr)==(i<4 || i>=6));
        scheduler.tick();
        CHECK(scheduler.status().published==8);
        CHECK(scheduler.status().pending==0);
        for (auto* b : buildings) CHECK(b->globalGradient[0]!=nullptr);
    }
}

void scheduledMixedTeamDeadlines()
{
    for (unsigned workers : {0u, 2u, 4u}) {
        World world;
        Scheduler scheduler(world, workers, 8);
        std::vector<Building*> warm;
        for (int i=0; i<5; ++i) {
            warm.push_back(world.place(4+i*8,8));
            world.warm(warm.back());
        }
        auto* cold=world.place(12,32);
        auto* other=world.place(32,32,1);
        scheduler.game.stepCounter=600;
        scheduler.tick([&] {
            for (unsigned i=0; i<4; ++i) REQUIRE(scheduler.map.requestBuildingRefresh(warm[i],0));
        });
        for (unsigned i=0; i<6; ++i) scheduler.tick();
        scheduler.tick([&] {
            // Cold team-0 work collides with its four warm deadlines. The later
            // warm request could fit, but must not pass it; team 1 can proceed.
            REQUIRE(scheduler.map.requestBuildingRefresh(cold,0));
            REQUIRE(scheduler.map.requestBuildingRefresh(warm[4],0));
            REQUIRE(scheduler.map.requestBuildingRefresh(other,0));
        });
        CHECK(scheduler.status().jobs==5);
        CHECK(scheduler.status().queued==2);
        CHECK(scheduler.status().pending==5);
        scheduler.tick();
        CHECK(other->globalGradient[0]!=nullptr);
        CHECK(cold->globalGradient[0]==nullptr);
        CHECK(scheduler.status().published==5);
        CHECK(scheduler.status().queued==0);
        CHECK(scheduler.status().pending==2);
        scheduler.tick();
        CHECK(cold->globalGradient[0]!=nullptr);
        CHECK(scheduler.status().published==6);
        for (unsigned i=0; i<7; ++i) scheduler.tick();
        CHECK(scheduler.status().published==7);
        CHECK(scheduler.status().pending==0);
    }
}

void scheduledWidePendingSave(bool text)
{
    World source,restored;
    std::vector<Building*> buildings;
    for (World* world : {&source,&restored}) {
        for (int team=2;team<9;++team) {
            world->game.addTeam(team);
            world->game.players[team]=new Player(team,"wide-save",world->game.teams[team],BasePlayer::P_LOCAL);
        }
        world->game.gameHeader.setNumberOfPlayers(9);
        world->game.map.setMapDiscovered();
        for (int team=0;team<9;++team) for (int i=0;i<32;++i) {
            const int index=team*32+i;
            auto* b=world->place(2+(index%20)*(world->siteW+1),
                2+(index/20)*(world->siteH+1),team);
            world->warm(b);
            if (world==&source) buildings.push_back(b);
        }
    }
    Scheduler scheduler(source,0,8),resume(restored,0,8);
    scheduler.game.stepCounter=600;
    for (unsigned tick=0;tick<8;++tick)
        scheduler.tick([&] {
            for (unsigned team=0;team<9;++team) for (unsigned i=0;i<4;++i)
                REQUIRE(scheduler.map.requestBuildingRefresh(buildings[team*32+tick*4+i],0));
        });
    REQUIRE(scheduler.status().pending==288);
    REQUIRE(scheduler.status().queued==0);
    resume.game.stepCounter=scheduler.game.stepCounter;
    const auto bytes=saveRuntime(scheduler.map,text);
    loadRuntime(resume.map,bytes,text);
    REQUIRE(resume.status().pending==288);
    CHECK(saveRuntime(resume.map,text)==bytes);
    for (unsigned tick=0;tick<8;++tick) {
        scheduler.tick(); resume.tick();
        CHECK(scheduler.status().pending==resume.status().pending);
        CHECK(scheduler.status().published==36*(tick+1));
        CHECK(resume.status().published==scheduler.status().published);
        for (auto* b : buildings) {
            auto* copy=restored.game.teams[b->owner->teamNumber]->myBuildings[Building::GIDtoID(b->gid)];
            CHECK(b->lastGlobalGradientUpdateStepCounter[0]==copy->lastGlobalGradientUpdateStepCounter[0]);
            CHECK(b->refreshRequested==copy->refreshRequested);
        }
    }
    CHECK(scheduler.status().pending==0);
    CHECK(resume.status().pending==0);
    CHECK(saveRuntime(scheduler.map,text)==saveRuntime(resume.map,text));
}

void scheduledLegacyPendingSave(int version)
{
    World source,restored;
    Scheduler scheduler(source,0,4),resume(restored,0,4);
    for (World* world : {&source,&restored}) {
        auto* b=world->place(24,24);
        world->warm(b);
    }
    auto* b=source.game.teams[0]->myBuildings[0];
    REQUIRE(b);
    scheduler.game.stepCounter=600;
    scheduler.tick([&] { REQUIRE(scheduler.map.requestBuildingRefresh(b,0)); });
    REQUIRE(scheduler.status().pending==1);
    const auto bytes=saveLegacyRuntime(scheduler.map,version);
    resume.game.stepCounter=scheduler.game.stepCounter;
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    resume.map.loadRuntimeState(&input,version);
    CHECK(input.readUint32("followingSection")==RuntimeSentinel);
    CHECK(resume.status().pending==1);
    CHECK(saveRuntime(scheduler.map)==saveRuntime(resume.map));
    for (unsigned i=0;i<4;++i) { scheduler.tick(); resume.tick(); }
    CHECK(scheduler.status().published==1);
    CHECK(resume.status().published==1);
    CHECK(saveRuntime(scheduler.map)==saveRuntime(resume.map));
}

// 2. A request made during team stepping is captured at the boundary.
void scheduledCaptureAtBoundary()
{
	World world;
	Scheduler s(world, 2, 4);
	Building* b = world.place(24, 24);
	world.warm(b);
	require(world.available(b, 2, 2), "reference warm field");
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

// 3. Cold queries only enroll demand. The complete generation becomes visible
// at the same deadline with serial execution or any worker count.
void scheduledPartialResume()
{
	std::vector<Uint16> reference;
	for (unsigned workers : {0u, 2u})
	{
		World world(8);
		Scheduler s(world, workers, 4);
		const int pool = globalContainer->buildingsTypes.getTypeNum("swimmingpool", 0, true);
		require(pool >= 0, "swimming pool site type");
		Building* b = s.game.addBuilding(100, 100, pool, 0, 1, 1);
		require(b != nullptr, "pool site placed");
		int dist = 0;
		world.warm(b);
		require(s.map.buildingAvailable(b, 0, 98, 98, &dist), "reference warm field");
		const int depth = s.map.predictBuildingDepth(b, 0);
		s.game.stepCounter = DIRTY_GRACE_TICKS + 1;
		s.map.addForbidden(10, 10, 0);
		s.tick([&] { s.map.buildingAvailable(b, 0, 98, 98, &dist); });
		for (int i = 1; i < 4; ++i) s.tick();
		s.map.advanceGradientPipeline();
		require(s.status().published == 1, "published");
		auto* search = b->globalGradientSearch[0].get();
		// The production predictor may settle a small map entirely.
		const bool partial = !search || !search->complete();
		require(search && (!partial || search->settledCost() > depth), "the published field carries its frontier");
		const auto workerCost = search->settledCost();
		require(s.map.buildingAvailable(b, 0, 230, 230, &dist), "far query resolves on the owner");
		require(!partial || search->settledCost() > workerCost, "the owner continues the transferred search");
		const auto resumed = s.finished(b, 0);
		const auto resumedCost = b->globalGradientSearch[0]->settledCost();
		s.map.updateGlobalGradient(b, 0);
		const auto full = s.finished(b, 0);
		require(resumed == full, "resumed values equal a full build");
		require(resumedCost == b->globalGradientSearch[0]->settledCost(), "the finished frontier matches a full build");
		if (reference.empty()) reference = resumed;
		require(resumed == reference, "serial and parallel refreshes publish the same values");
		std::printf("PASS scheduled field (depth %d, worker frontier %llu of %llu) resumes from transferred buckets\n",
			depth, (unsigned long long)workerCost, (unsigned long long)resumedCost);
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
	for (Building* x : {a, b, c, d}) { world.warm(x); require(world.available(x, 2, 2), "reference warm field"); }
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
					for (Building* b : list) { w.warm(b); require(w.available(b, 2, 2), "reference warm field"); }
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
		world.warm(old);
		require(world.available(old, 2, 2), "reference warm field");
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
		world.warm(replacement);
		require(world.available(replacement, 2, 2), "replacement reference warm field");
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
	world.warm(b); // Isolate the two access variants.
	int dist = 0;
	for (int swim : {1, 3}) { world.warm(b, swim); require(s.map.buildingAvailable(b, swim, 2, 2, &dist), "reference warm field"); }
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
		require(!b->isLocked(access), "synchronous build sees the open fence");
	});
	for (int i = 2; i < 4; ++i) s.tick();
	s.map.advanceGradientPipeline();
	require(s.status().published == 1 && !b->isLocked(access), "an older capture cannot overwrite newer access metadata");
	s.game.stepCounter += DIRTY_GRACE_TICKS;
	paint(true);
	s.tick([&] { s.map.buildingAvailable(b, 1, 2, 2, &dist); });
	for (int i = 1; i < 4; ++i) s.tick();
	s.map.advanceGradientPipeline();
	require(s.status().published == 2 && b->isLocked(access), "the newest capture writes access metadata");
	const auto bytes = saveRuntime(s.map);
	loadRuntime(s.map, bytes);
	require(b->isLocked(access), "saves keep the published metadata");
	std::puts("PASS access metadata follows the newest capture across swim classes and saves");
}

// 8. With the pipeline on, a team-wide reset or a forbidden-area paint keeps
// the building's walking field serving and refreshes it on schedule, at once
// and with no owner build. A building's own change still drops it. Creation
// itself enables delayed publication even without an explicit Scheduler.
void scheduledResetsKeepStaleFields()
{
	for (auto cause : {Building::GradientDrop::Area, Building::GradientDrop::Team})
	for (unsigned workers : {0u, 2u})
	{
		World world;
		Scheduler s(world, workers, 4);
		Building* b = world.place(24, 24);
		world.warm(b);
		require(world.available(b, 2, 2), "reference warm walking field");
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
		require(s.map.requestBuildingField(b, 0) == FieldReadiness::Pending, "hard reset enrolls a cold delayed field");
		require(s.status().synchronous == after.synchronous + 1, "only the explicit reference build ran synchronously");
	}
	{
		World world;
		Building* b = world.place(24, 24);
		world.warm(b);
		require(world.available(b, 2, 2), "reference warm field");
		require(world.game.map.buildingGradientPipelineActive(), "first route demand enables delayed simulation publication");
		const auto* walking=b->globalGradient[0];
		b->resetPathfindGradients(Building::GradientDrop::Area);
		require(b->globalGradient[0]==walking && b->dirtyGradient[0], "a simulation area edit retains dirty published guidance");
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
			world.warm(b);
	require(world.available(b, 2, 2), "reference warm field");
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
			world.warm(b);
	require(world.available(b, 2, 2), "reference warm field");
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
		world.warm(rival);
		require(world.available(rival, 2, 2), "reference warm field");
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
	world.warm(rival);
		require(world.available(rival, 2, 2), "reference warm field");
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
		for (int swim : {1, 3}) { world.warm(b, swim); require(world.game.map.buildingAvailable(b, swim, 2, 2, &dist), "reference warm field"); }
		const int access = b->routeAccess(1, BuildingRoute::Footprint);
		auto paint = [&](bool on) { fence(world, on); };
		world.game.stepCounter = DIRTY_GRACE_TICKS + 1;
		paint(true);
		world.warm(b,1);
		world.game.map.buildingAvailable(b, 1, 2, 2, &dist);
		require(b->isLocked(access), "synchronous: the fenced build locks");
		paint(false);
		world.warm(b,3);
		world.game.map.buildingAvailable(b, 3, 2, 2, &dist);
		require(!b->isLocked(access), "synchronous: the later build of the same tick wins");
	}
	World world;
	Scheduler s(world, 2, 4);
	Building* b = world.place(24, 24);
	world.warm(b); // This case isolates classes 1 and 3.
	for (int swim : {1, 3}) { world.warm(b, swim); require(s.map.buildingAvailable(b, swim, 2, 2, &dist), "reference warm field"); }
	const int access = b->routeAccess(1, BuildingRoute::Footprint);
	require(access == b->routeAccess(3, BuildingRoute::Footprint), "classes 1 and 3 share one access variant");
	auto paint = [&](bool on) { fence(world, on); };
	s.game.stepCounter = DIRTY_GRACE_TICKS + 1;
	paint(true);
	s.tick([&] { s.map.buildingAvailable(b, 1, 2, 2, &dist); }); // captures the fence
	paint(false);
	s.map.buildingAvailable(b, 3, 2, 2, &dist); // same tick, open fence
	s.map.stagePeriodicGradientPreparation();
	s.map.game->snapshotStore().invalidateBoundary(); s.map.preparePendingGradient(s.map.game->snapshotStore().captureBoundary(*s.map.game));
	require(s.status().pending == 2, "both swim classes are pending with one capture tick");
	require(b->lastGlobalGradientUpdateStepCounter[b->routeSlot(1, BuildingRoute::Footprint)] < s.game.stepCounter, "nothing published yet");
	for (int i = 1; i < 4; ++i) s.tick();
	s.map.advanceGradientPipeline();
	require(s.status().published == 2, "both published at one deadline");
	require(!b->isLocked(access), "scheduled: the later capture of the same tick wins, like the synchronous build");
	std::puts("PASS same-tick swim classes write access metadata in request order");
}

}

TEST_SUITE("BuildingGradientInvalidation")
{
    TEST_CASE("owner observation at tick zero publishes cold fields and access metadata at tick one")
    {
        glob2test::HeadlessGlobals globals;
        constexpr unsigned delay=4, initialDelay=1;
        for (unsigned workers:{0u,2u}) {
            CAPTURE(workers);
            World world;
            Scheduler scheduler(world,workers,delay);
            world.game.gameHeader.setResourceGrowthDisabled(true);
            auto* site=world.place(24,24);
            for (int y=23;y<=24+world.siteH;++y)
                for (int x=23;x<=24+world.siteW;++x)
                    if (x==23 || y==23 || x==24+world.siteW || y==24+world.siteH)
                        world.game.map.addForbidden(x,y,0);
            const auto type=world.game.buildingsTypes.getTypeNum("clearingflag",0,false);
            auto* flag=world.game.addBuilding(44,44,type,0); REQUIRE(flag);
            flag->unitStayRange=4; flag->setClearsMaterial(WOOD,true);
            world.game.map.setResourceByIndex(46,44,WOOD,1);
            auto& game=world.game; auto& map=game.map;
            const auto access=site->routeAccess(0,BuildingRoute::Footprint);
            const auto variant=swimAccessVariant(0);
            REQUIRE(game.stepCounter==0);
            REQUIRE_FALSE(site->isLocked(access));
            REQUIRE(flag->anyResourceToClear[variant]==0);
            GradientExecutionAudit audit;
            REQUIRE(map.requestBuildingField(site,0,BuildingRoute::Footprint)==FieldReadiness::Pending);
            REQUIRE(map.requestBuildingField(flag,0,BuildingRoute::Clearing)==FieldReadiness::Pending);
            game.observeCompletedWorld();
            REQUIRE(map.buildingGradientPipelineStatus().pending==2);
            map.finishGradientPipeline(); // Force early private completion without publication.
            for (unsigned tick=0;tick<initialDelay;++tick) {
                CAPTURE(tick);
                if (tick) { ++game.stepCounter; game.observeCompletedWorld(); }
                CHECK(map.lookupBuildingField(site,0,BuildingRoute::Footprint)==nullptr);
                CHECK(map.lookupBuildingField(flag,0,BuildingRoute::Clearing)==nullptr);
                CHECK_FALSE(site->isLocked(access));
                CHECK(flag->anyResourceToClear[variant]==0);
            }
            ++game.stepCounter; game.observeCompletedWorld();
            REQUIRE(game.stepCounter==initialDelay);
            REQUIRE(map.lookupBuildingField(site,0,BuildingRoute::Footprint)!=nullptr);
            REQUIRE(map.lookupBuildingField(flag,0,BuildingRoute::Clearing)!=nullptr);
            CHECK(site->lastGlobalGradientUpdateStepCounter[site->routeSlot(0,BuildingRoute::Footprint)]==0);
            CHECK(flag->lastGlobalGradientUpdateStepCounter[flag->routeSlot(0,BuildingRoute::Clearing)]==0);
            CHECK(site->isLocked(access));
            CHECK(flag->anyResourceToClear[variant]==1);
            map.finishGradientPipeline();
            if (workers && GAGCore::ThreadSupport::available) CHECK(audit.totals().owner==0);
            else CHECK(audit.totals().owner>0);
        }
    }

    TEST_CASE("synthetic runtime rejects resource-only adaptive layouts instead of dropping their state [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        for (int version : {174,176,177}) {
            World world;
            world.game.mapHeader.privateAdaptive174=(version==174);
            // No current unified state can truthfully supply the historical
            // resource-policy record; real fixtures exercise those loader paths.
            CHECK_THROWS_AS(glob2test::LegacyPopulationOutput(new GAGCore::MemoryStreamBackend,&world.game,version),std::logic_error);
        }
    }

    TEST_CASE("released and building-draft format lineages select independent layouts [save-format]")
    {
        struct Layout { unsigned version; bool movement, snapshot, delayed, initial; };
        for (const auto layout : {
            Layout{168,false,true,false,false}, Layout{169,false,true,true,false},
            Layout{170,false,false,false,true}, Layout{171,true,false,false,false},
            Layout{172,true,true,false,false}, Layout{173,true,true,true,false},
            Layout{174,false,false,false,true}, Layout{175,true,true,true,true}}) {
            CAPTURE(layout.version);
            CHECK(hasUnifiedMovement(layout.version)==layout.movement);
            CHECK(hasSnapshotChecksumMetadata(layout.version)==layout.snapshot);
            CHECK(hasDelayedGradientState(layout.version)==layout.delayed);
            CHECK(hasInitialBuildingGradient(layout.version)==layout.initial);
        }
    }

    TEST_CASE("released173 and building drafts preserve queued and admitted building routes [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        for (int version : {170,173,174}) for (bool admitted : {false,true}) {
            CAPTURE(version); CAPTURE(admitted);
            World source, restored;
            Scheduler scheduled(source,0,4), restoredScheduler(restored,0,4);
            source.game.gameHeader.setResourceGrowthDisabled(true);
            restored.game.gameHeader.setResourceGrowthDisabled(true);
            const auto type=source.game.buildingsTypes.getTypeNum("clearingflag",0,false);
            auto* original=source.game.addBuilding(24,24,type,0);
            auto* loaded=restored.game.addBuilding(24,24,type,0);
            REQUIRE(original); REQUIRE(loaded);
            for (auto* flag : {original,loaded}) {
                flag->unitStayRange=4; flag->setClearsMaterial(WOOD,true);
            }
            source.game.map.setResourceByIndex(26,24,WOOD,1);
            restored.game.map.setResourceByIndex(26,24,WOOD,1);
            const auto slot=original->routeSlot(0,BuildingRoute::Clearing);
            // Master173 could queue only warm building refreshes. Published
            // building drafts170/174 additionally permit an initial cold field.
            if (!hasInitialBuildingGradient(version)) source.warm(original);
            REQUIRE(source.game.map.requestBuildingRefresh(original,slot));
            REQUIRE(source.game.map.buildingGradientPipelineStatus().queued==1);
            if (admitted) {
                scheduled.tick();
                REQUIRE(source.game.map.buildingGradientPipelineStatus().pending==1);
            }
            restored.game.stepCounter=source.game.stepCounter;
            const auto bytes=saveLegacyRuntime(source.game.map,version);
            auto* backend=new GAGCore::MemoryStreamBackend;
            backend->write(bytes.data(),bytes.size()); backend->seekFromStart(0);
            GAGCore::BinaryInputStream input(backend);
            restored.game.map.loadRuntimeState(&input,version);
            CHECK(input.readUint32("followingSection")==RuntimeSentinel);
            CHECK(restored.game.map.buildingGradientPipelineStatus().queued==
                source.game.map.buildingGradientPipelineStatus().queued);
            CHECK(restored.game.map.buildingGradientPipelineStatus().pending==
                source.game.map.buildingGradientPipelineStatus().pending);
            CHECK(loaded->refreshRequested.test(slot));
            CHECK(bool(loaded->globalGradient[slot])==bool(original->globalGradient[slot]));
            CHECK(saveRuntime(restored.game.map)==saveRuntime(source.game.map));
            // This uses Clearing rather than Footprint to pin the old8-to-new9
            // movement-class slot remap as well as the 16/32-bit queue counts.
        }
    }

    TEST_CASE("empty building-draft174 runtime uses its widened queue count [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        World source, restored;
        auto* backend=new GAGCore::MemoryStreamBackend;
        glob2test::LegacyPopulationOutput output(backend,&source.game,174);
        source.game.map.saveRuntimeState(&output);
        output.writeUint32(RuntimeSentinel,"followingSection"); output.flush();
        const auto bytes=backend->takeContents();
        auto* saved=new GAGCore::MemoryStreamBackend;
        saved->write(bytes.data(),bytes.size()); saved->seekFromStart(0);
        GAGCore::BinaryInputStream input(saved);
        restored.game.map.loadRuntimeState(&input,174);
        CHECK(input.readUint32("followingSection")==RuntimeSentinel);
        CHECK(restored.game.map.buildingGradientPipelineStatus().queued==0);
        CHECK(restored.game.map.buildingGradientPipelineStatus().pending==0);
    }

    TEST_CASE("observation phase is covered by both current checksum tiers")
    {
        glob2test::HeadlessGlobals globals;
        for (bool heavy : {false,true}) {
            CAPTURE(heavy);
            World world;
            auto& map = world.game.map;
            const auto current = map.checkSum(heavy,false,false);
            const auto legacy = map.checkSum(heavy,false,true);
            // This empty world's publication changes only the owner phase.
            map.publishWorldResultsDue();
            CHECK(map.checkSum(heavy,false,false) != current);
            CHECK(map.checkSum(heavy,false,true) == legacy);
        }
    }

    TEST_CASE("released saves through format168 bootstrap only when their observation was not admitted")
    {
        glob2test::HeadlessGlobals globals;
        for (int legacyVersion : {164,165,166,167,168})
        for (bool admitted : {false,true}) {
            CAPTURE(legacyVersion); CAPTURE(admitted);
            World source, restored;
            source.game.map.setResourceByIndex(20,20,WHEAT,1);
            restored.game.map.setResourceByIndex(20,20,WHEAT,1);
            REQUIRE(source.game.stepCounter == 0);
            if (admitted) {
                source.game.map.publishWorldResultsDue();
                source.game.map.submitWorldWork(source.game.snapshotStore().captureBoundary(source.game));
            }
            const auto bytes = saveLegacyRuntime(source.game.map, legacyVersion);
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
            input.seekFromStart(0);
            restored.game.map.loadRuntimeState(&input,legacyVersion);
            CHECK(input.readUint32("followingSection") == RuntimeSentinel);
            restored.game.map.publishWorldResultsDue();
            restored.game.map.submitWorldWork(restored.game.snapshotStore().captureBoundary(restored.game));
            CHECK(restored.game.map.resourceGrowthMetrics().submitted == (admitted ? 0 : 1));
            REQUIRE(restored.game.map.resourceGrowthMetrics().published == 0);
            if (admitted) {
                CHECK(saveRuntime(restored.game.map) == saveRuntime(source.game.map));
            }
        }
    }

    TEST_CASE("world observation publication and admission survive save boundaries")
    {
        glob2test::HeadlessGlobals globals;
        for (int legacyVersion : {164,165,166,167,168}) {
            CAPTURE(legacyVersion);
            World original, restored, legacy;
            for (World* world : {&original, &restored, &legacy}) {
                world->game.stepCounter = 37;
                world->game.map.setResourceByIndex(20, 20, WHEAT, 1);
                glob2test::LegacyPopulationOutput::primeMaterialGradient(world->game.map,0,WHEAT,0);
                world->game.map.configureGradientPipeline(0, 2);
            }
            auto& map = original.game.map;
            // Request explicitly: warm natural fields now require demand rather
            // than participating in the legacy periodic round automatically.
            map.updateMaterialGradient(0,WHEAT,0);
            map.publishWorldResultsDue();
            auto observation = original.game.snapshotStore().captureBoundary(original.game);
            map.submitWorldWork(observation);
            const auto pending = map.gradientPipelineStatus().pending;
            REQUIRE(pending == 1);
            map.publishWorldResultsDue();
            map.submitWorldWork(observation);
            CHECK(map.gradientPipelineStatus().pending == pending);
            CHECK(map.gradientPipelineStatus().jobs == 1);
            const auto bytes = saveRuntime(map);
            loadRuntime(restored.game.map, bytes);
            restored.game.map.publishWorldResultsDue();
            restored.game.map.submitWorldWork(restored.game.snapshotStore().captureBoundary(restored.game));
            CHECK(restored.game.map.gradientPipelineStatus().pending == pending);
            CHECK(restored.game.map.gradientPipelineStatus().published == 0);
            CHECK(restored.game.map.resourceGrowthMetrics().submitted == 0);
            // Formats 164–166 have no publication/submission markers. Retain
            // their original outputs and verify that the next section aligns.
            const auto historicalBytes = saveLegacyRuntime(map, legacyVersion);
            auto backend = std::make_unique<GAGCore::MemoryStreamBackend>();
            backend->write(historicalBytes.data(), historicalBytes.size()); backend->seekFromStart(0);
            GAGCore::BinaryInputStream input(backend.release());
            legacy.game.map.loadRuntimeState(&input, legacyVersion);
            CHECK(input.readUint32("followingSection") == RuntimeSentinel);
            // A migration may be saved again before its first owner observation.
            const auto migratedBytes = saveRuntime(legacy.game.map);
            loadRuntime(legacy.game.map, migratedBytes);
            legacy.game.map.publishWorldResultsDue();
            legacy.game.map.submitWorldWork(legacy.game.snapshotStore().captureBoundary(legacy.game));
            CHECK(legacy.game.map.resourceGrowthMetrics().submitted == 0);
            CHECK(legacy.game.map.gradientPipelineStatus().published == 0);
            ++original.game.stepCounter;
            ++restored.game.stepCounter;
            ++legacy.game.stepCounter;
            map.publishWorldResultsDue();
            restored.game.map.publishWorldResultsDue();
            legacy.game.map.publishWorldResultsDue();
            CHECK(map.gradientPipelineStatus().published == 0);
            CHECK(restored.game.map.gradientPipelineStatus().published == 0);
            CHECK(legacy.game.map.gradientPipelineStatus().published == (legacyVersion<FILE_FORMAT_VERSION_SINGLE_OBSERVATION ? 1 : 0));
            ++original.game.stepCounter;
            ++restored.game.stepCounter;
            ++legacy.game.stepCounter;
            map.publishWorldResultsDue();
            restored.game.map.publishWorldResultsDue();
            legacy.game.map.publishWorldResultsDue();
            CHECK(map.gradientPipelineStatus().published == 1);
            CHECK(restored.game.map.gradientPipelineStatus().published == 1);
            CHECK(legacy.game.map.gradientPipelineStatus().published == 1);
            const auto* a = map.getMaterialGradientSlot(0,WHEAT,0);
            const auto* b = restored.game.map.getMaterialGradientSlot(0,WHEAT,0);
            CHECK(std::equal(a, a + map.getW() * map.getH(), b));
            const auto* historical = legacy.game.map.getMaterialGradientSlot(0,WHEAT,0);
            REQUIRE(historical != nullptr);
            CHECK(std::equal(a, a + map.getW() * map.getH(), historical));
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
    TEST_CASE("a saturated team cannot block another team or borrow its unused quota")
    {
        glob2test::HeadlessGlobals globals;
        scheduledPerTeamQuotas();
        scheduledColdTeamQuotas();
    }
    TEST_CASE("mixed cold and warm deadlines preserve team FIFO without blocking other teams")
    {
        glob2test::HeadlessGlobals globals;
        scheduledMixedTeamDeadlines();
    }
    TEST_CASE("more than 255 pending building gradients survive binary and text saves [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        scheduledWidePendingSave(false);
        scheduledWidePendingSave(true);
    }
    TEST_CASE("historical byte-sized pending building jobs retain their deadlines [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        scheduledLegacyPendingSave(167);
        scheduledLegacyPendingSave(185);
    }
	TEST_CASE("scheduled building fields publish at fixed deadlines across worker counts and delays")
	{
		glob2test::HeadlessGlobals globals;
		scheduledFixedDeadlines();
	}
    TEST_CASE("more than 64 building refreshes retain their FIFO without synchronous overflow across saves")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned workers : {0u, 2u}) for (bool text : {false, true}) {
            CAPTURE(workers); CAPTURE(text);
            World source, restored;
            Scheduler s(source, workers, 4), r(restored, workers, 4);
            std::vector<Building*> buildings, loaded;
            for (unsigned i=0; i<65; ++i) {
                const int x=4+6*(i%8), y=4+6*(i/8);
                buildings.push_back(source.place(x,y));
                loaded.push_back(restored.place(x,y));
            }
            for (auto* b:buildings) {
                source.warm(b);
                REQUIRE(source.available(b,2,2));
            }
            const auto before=s.status();
            for (unsigned i=0; i<64; ++i) REQUIRE(s.map.requestBuildingRefresh(buildings[i],0));
            CHECK(s.status().queued==64);
            CHECK(s.status().synchronous==before.synchronous);
            // Coalescing an existing destination must not trigger overflow.
            REQUIRE(s.map.requestBuildingRefresh(buildings[1],0));
            CHECK(s.status().queued==64);
            CHECK(s.status().synchronous==before.synchronous);
            REQUIRE(s.map.requestBuildingRefresh(buildings[64],0));
            CHECK(s.status().queued==65);
            CHECK(s.status().synchronous==before.synchronous);
            CHECK(s.status().synchronousByReason[std::size_t(Map::BuildingSyncReason::Overflow)]
                ==before.synchronousByReason[std::size_t(Map::BuildingSyncReason::Overflow)]);
            for (auto* building:buildings) CHECK(building->refreshRequested.test(0));

            const auto bytes=saveRuntime(s.map,text);
            loadRuntime(r.map,bytes,text);
            CHECK(r.status().queued==65);
            CHECK(saveRuntime(r.map,text)==bytes);
            for (auto* building:loaded) CHECK(building->refreshRequested.test(0));
            // Coalescing the oldest destination preserves every queued refresh,
            // including after loading, rather than evicting a published field.
            REQUIRE(s.map.requestBuildingRefresh(buildings.front(),0));
            REQUIRE(r.map.requestBuildingRefresh(loaded.front(),0));
            CHECK(buildings[1]->refreshRequested.test(0));
            CHECK(loaded[1]->refreshRequested.test(0));
            CHECK(s.status().queued==65);
            CHECK(r.status().queued==65);
            CHECK(saveRuntime(s.map,text)==saveRuntime(r.map,text));
        }
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
    TEST_CASE("retained format126 access flags restore walking and swimming locks into the footprint route and survive current saves [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        const auto original=glob2test::readFile(glob2test::inflated("team-limit/pre-v127-maxima.game.gz"));
        GameGUI observed;
        LegacyAccessOffsetReader tracker(original);
        REQUIRE(observed.game.load(&tracker));
        REQUIRE(observed.game.mapHeader.getVersionMinor()==126);
        REQUIRE(tracker.flags.size()==8); // Four real swarms, two old access rows each.
        for(const auto& flag:tracker.flags) {
            REQUIRE(flag.offset<original.size());
            REQUIRE(flag.swim>=0); REQUIRE(flag.swim<2);
            const auto* building=observed.game.teams[flag.team]->myBuildings[flag.building];
            REQUIRE(building!=nullptr);
            REQUIRE(std::string(building->type->type)=="swarm");
            REQUIRE(building->resolveRoute(BuildingRoute::Automatic)==BuildingRoute::Footprint);
        }
        const auto load=[](Game& game,const std::string& bytes) {
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
            input.seekFromStart(0); return game.load(&input);
        };
        const auto save=[](Game& game) {
            auto* memory=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(memory);
            game.save(&output,false,"legacy access continuation"); return memory->takeContents();
        };
        const auto state=[](Game& game) {
            std::vector<Uint32> fields,buildings,units;
            game.checkSum(&fields,&buildings,&units,true);
            return std::make_tuple(fields,buildings,units);
        };
        for(unsigned pattern=0;pattern<4;++pattern) {
            auto patched=original;
            for(const auto& flag:tracker.flags) patched[flag.offset]=char((pattern>>flag.swim)&1u);
            GameGUI legacy;
            REQUIRE(load(legacy.game,patched));
            for(const auto& flag:tracker.flags) {
                const auto* building=legacy.game.teams[flag.team]->myBuildings[flag.building];
                // Concrete row zero is independent of the Automatic sentinel's
                // numeric value. Other routes and the new water-only row remain clear.
                for(int index=0;index<BUILDING_ACCESS_COUNT;++index)
                    REQUIRE(building->isLocked(index)==(index<2 && bool((pattern>>index)&1u)));
            }
            GameGUI current,repeated;
            REQUIRE(load(current.game,save(legacy.game)));
            REQUIRE(current.game.mapHeader.getVersionMinor()==VERSION_MINOR);
            REQUIRE(saveRuntime(current.game.map)==saveRuntime(legacy.game.map));
            for(const auto& flag:tracker.flags) {
                const auto* a=legacy.game.teams[flag.team]->myBuildings[flag.building];
                const auto* b=current.game.teams[flag.team]->myBuildings[flag.building];
                for(int index=0;index<BUILDING_ACCESS_COUNT;++index) REQUIRE(a->isLocked(index)==b->isLocked(index));
            }
            REQUIRE(load(repeated.game,save(current.game)));
            current.game.setWaitingOnMask(0); repeated.game.setWaitingOnMask(0);
            REQUIRE(state(current.game)==state(repeated.game));
            for(int tick=0;tick<16;++tick) {
                current.game.step(0); repeated.game.step(0);
                REQUIRE(state(current.game)==state(repeated.game));
                REQUIRE(current.game.syncRandom==repeated.game.syncRandom);
                REQUIRE(saveRuntime(current.game.map)==saveRuntime(repeated.game.map));
            }
        }
    }



}
