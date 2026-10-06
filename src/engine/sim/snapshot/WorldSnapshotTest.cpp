// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "WorldSnapshot.h"
#include "SnapshotStore.h"
#include "Building.h"
#include "Unit.h"
#include "Team.h"
#include <span>
#include <atomic>
#include <thread>
#include <ThreadSupport.h>

TEST_SUITE("WorldSnapshot")
{
	TEST_CASE("component leases and weak control blocks outlive the owning store")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1}};
		SimulationSnapshot::Handle held;
		std::weak_ptr<const SimulationSnapshot::Resources> weak;
		{
			SimulationSnapshot::Store store;
			held = store.captureBoundary(fixture.game, SimulationSnapshot::bit(SimulationSnapshot::Component::Resources));
			weak = held.resources;
		}
		REQUIRE(held.resources);
		CHECK(held.resources->cells.size() == 1024);
		held = {};
		CHECK(weak.expired());
		// The alias allocator must still own its resource while this final weak
		// reference deallocates the control block, after LeaseOwner destruction.
		weak.reset();
	}
	TEST_CASE("lease control block storage stops allocating after warmup")
	{
		SimulationSnapshot::BufferPool<std::vector<int>> pool;
		Uint64 allocations = 0;
		for (unsigned i = 0; i < 32; ++i) { auto lease = pool.acquire(allocations); lease->resize(32); }
		const auto upstream = pool.leaseUpstreamAllocations();
		REQUIRE(upstream > 0);
		CHECK(pool.leaseRetainedBytes() > 0);
		for (unsigned i = 0; i < 256; ++i) { auto lease = pool.acquire(allocations); CHECK(lease->size() == 32); }
		CHECK(pool.leaseUpstreamAllocations() == upstream);
		CHECK(allocations == 1);
	}
	TEST_CASE("worker final lease release synchronizes owner buffer reuse")
	{
		if constexpr (GAGCore::ThreadSupport::available)
		{
			SimulationSnapshot::BufferPool<std::vector<int>> pool;
			Uint64 allocations = 0;
			for (int iteration = 0; iteration < 32; ++iteration)
			{
				auto lease = pool.acquire(allocations);
				lease->assign(128, iteration);
				auto* identity = lease.get();
				std::atomic<bool> released{false};
				int sum = 0;
				auto worker = GAGCore::ThreadSupport::launch([lease = std::move(lease), &released, &sum]() mutable {
					for (int value : *lease) sum += value;
					lease.reset();
					released.store(true, std::memory_order_relaxed);
				});
				// Relaxed polling deliberately supplies no read/write barrier. Only
				// the lease retirement/acquisition mutex protects this next write.
				while (!released.load(std::memory_order_relaxed)) std::this_thread::yield();
				auto reused = pool.acquire(allocations);
				CHECK(reused.get() == identity);
				reused->assign(128, -1);
				worker.join();
				CHECK(sum == 128 * iteration);
			}
			CHECK(allocations == 1);
		}
	}

	TEST_CASE("catalog refresh survives intervening projections and rules reuse independently")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .loadDefaultRace=true}};
		auto& game=fixture.game;
		SimulationSnapshot::Store store;
		const auto first=store.captureBoundary(game,SimulationSnapshot::All);
		game.gameHeader.setUnitUpgradesDisabled(true);
		++game.stepCounter;
		store.captureBoundary(game,SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain));
		++game.stepCounter;
		const auto refreshed=store.captureBoundary(game,SimulationSnapshot::All);
		CHECK(refreshed.catalogs->buildings!=first.catalogs->buildings);
		++game.stepCounter;
		const auto rules=store.captureBoundary(game,SimulationSnapshot::bit(SimulationSnapshot::Component::Rules));
		CHECK(rules.rules==refreshed.rules);
		++game.stepCounter;
		const auto nextRules=store.captureBoundary(game,SimulationSnapshot::bit(SimulationSnapshot::Component::Rules));
		CHECK(nextRules.rules==rules.rules);
	}
	TEST_CASE("projected terrain lease cannot retain unrelated world components")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto all = SimulationSnapshot::capture(fixture.game, SimulationSnapshot::captureCatalog(fixture.game));
		std::weak_ptr<const SimulationSnapshot::Entities> entities = all.entities;
		auto terrain = all.project(SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain));
		CHECK_FALSE(terrain.entities); CHECK_FALSE(terrain.visibility); CHECK_FALSE(terrain.teams);
		all = {};
		CHECK(entities.expired()); REQUIRE(terrain.terrain);
		CHECK_THROWS_AS(terrain.project(SimulationSnapshot::bit(SimulationSnapshot::Component::Entities)), std::invalid_argument);
	}
	TEST_CASE("component mutation generations preserve isolation and reuse unchanged arrays")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game;
		const auto catalog = SimulationSnapshot::captureCatalog(game);
		auto initial = SimulationSnapshot::capture(game, catalog);
		auto unchanged = SimulationSnapshot::capture(game, catalog, SimulationSnapshot::All, &initial);
		CHECK(unchanged.resources == initial.resources); CHECK(unchanged.occupancy == initial.occupancy);
		CHECK(unchanged.areas == initial.areas); CHECK(unchanged.visibility == initial.visibility);
		game.map.setResource(5, 5, WHEAT, 1);
		auto changed = SimulationSnapshot::capture(game, catalog, SimulationSnapshot::All, &unchanged);
		CHECK(changed.resources != initial.resources); CHECK(changed.areas == initial.areas);
		CHECK(initial.tileAt(game.map.coordToIndex(5, 5)).resource.type == NO_RES_TYPE);
		CHECK(changed.tileAt(game.map.coordToIndex(5, 5)).resource.type == WHEAT);
		game.map.addForbidden(5, 5, 0);
		auto areaChanged = SimulationSnapshot::capture(game, catalog, SimulationSnapshot::All, &changed);
		CHECK(areaChanged.areas != changed.areas); CHECK(areaChanged.resources == changed.resources);
	}
	TEST_CASE("capture requirements exclude arrays that no scheduled consumer requests")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		SimulationSnapshot::Store store;
		auto terrain = store.captureBoundary(fixture.game, SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain));
		REQUIRE(terrain.terrain); CHECK_FALSE(terrain.entities); CHECK_FALSE(terrain.resources); CHECK_FALSE(terrain.growth);
		CHECK(store.metrics.captures == 1); CHECK(store.metrics.bytesCopied == 1024 * sizeof(Uint16));
		store.captureBoundary(fixture.game, SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain));
		CHECK(store.metrics.captures == 1);
	}
	TEST_CASE("warm component buffers stop allocating at a fixed population")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game;
		auto* building = fixture.addBuilding("inn", 4, 4, 0, 0);
		auto* worker = fixture.addUnit(WORKER, 12, 12, 0);
		REQUIRE(building); REQUIRE(worker);
		building->unitsWorking.push_back(worker);
		game.map.setResource(20, 20, WHEAT, 1);
		game.map.getResourceGradient(0, WHEAT, 0);
		SimulationSnapshot::Store store;
		auto mutateAndCapture = [&] {
			++game.stepCounter;
			game.map.setFertility(20, 20, Uint16(game.stepCounter));
			game.map.setGroundUnit(25, 25, NOGUID);
			game.map.addGuardArea(20, 20, 0);
			game.map.setMapDiscovered(20, 20, game.teams[0]->me);
			game.map.updateResourcesGradient(0, WHEAT, 0);
			building->priority = int(game.stepCounter);
			return store.captureBoundary(game, SimulationSnapshot::All);
		};
		for (int i = 0; i < 6; ++i) mutateAndCapture();
		const auto warmedAllocations = store.metrics.allocations;
		const auto copied = store.metrics.bytesCopied;
		for (int i = 0; i < 24; ++i) {
			auto snapshot = mutateAndCapture();
			CHECK(snapshot.entities->buildings.front().priority == int(game.stepCounter));
			CHECK(store.metrics.allocations == warmedAllocations);
		}
		CHECK(store.metrics.bytesCopied > copied);
	}
	TEST_CASE("pooled captures never overwrite a held worker projection")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game;
		auto* building = fixture.addBuilding("inn", 4, 4, 0, 0);
		REQUIRE(building);
		SimulationSnapshot::Store store;
		const auto required = SimulationSnapshot::bit(SimulationSnapshot::Component::Resources)
			| SimulationSnapshot::bit(SimulationSnapshot::Component::Entities);
		auto full = store.captureBoundary(game, SimulationSnapshot::All);
		auto held = full.project(required);
		full = {};
		const int oldPriority = held.entities->buildings.front().priority;
		const auto index = game.map.coordToIndex(20, 20);
		const auto oldResource = held.resources->cells[index].resource.getUint32();
		for (int i = 0; i < 24; ++i) {
			++game.stepCounter;
			building->priority = oldPriority + i + 1;
			game.map.setResource(20, 20, WHEAT, 1);
			auto latest = store.captureBoundary(game, SimulationSnapshot::All);
			CHECK(latest.resources != held.resources);
			CHECK(latest.entities != held.entities);
			CHECK(held.entities->buildings.front().priority == oldPriority);
			CHECK(held.resources->cells[index].resource.getUint32() == oldResource);
		}
		CHECK_FALSE(held.terrain); CHECK_FALSE(held.teams); CHECK_FALSE(held.visibility);
	}
	TEST_CASE("flat relationship ranges preserve every building list in order")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto* first = fixture.addBuilding("inn", 4, 4, 0, 0);
		auto* second = fixture.addBuilding("inn", 20, 20, 0, 0);
		auto* a = fixture.addUnit(WORKER, 12, 12, 0);
		auto* b = fixture.addUnit(WORKER, 13, 12, 0);
		REQUIRE(first); REQUIRE(second); REQUIRE(a); REQUIRE(b);
		first->unitsWorking.push_back(b); first->unitsWorking.push_back(a);
		first->unitsInside.push_back(a); second->unitsInside.push_back(b);
		auto snapshot = SimulationSnapshot::capture(fixture.game, SimulationSnapshot::captureCatalog(fixture.game));
		std::size_t cursor = 0;
		for (const auto& view : snapshot.entities->buildings) {
			const auto* live = fixture.game.resolveBuilding(view.identity);
			REQUIRE(live);
			CHECK(view.working.offset == cursor);
			CHECK(view.working.count == live->unitsWorking.size());
			for (const auto* unit : live->unitsWorking) CHECK(snapshot.entities->relationships.at(cursor++) == Game::refOf(unit));
			CHECK(view.inside.offset == cursor);
			CHECK(view.inside.count == live->unitsInside.size());
			for (const auto* unit : live->unitsInside) CHECK(snapshot.entities->relationships.at(cursor++) == Game::refOf(unit));
		}
		CHECK(cursor == snapshot.entities->relationships.size());
	}
	TEST_CASE("farm paint queries match frozen growth and resource inputs")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game;
		for (int y = 0; y < game.map.getH(); ++y) {
			for (int x = 0; x < 8; ++x) game.map.setUMatPos(x, y, WATER, 1);
			for (int x = 8; x < 16; ++x) game.map.setUMatPos(x, y, SAND, 1);
		}
		SimulationSnapshot::Store store;
		auto checkParity = [&] {
			++game.stepCounter;
			auto snapshot = store.captureBoundary(game, SimulationSnapshot::All);
			for (int y = 0; y < game.map.getH(); ++y)
				for (int x = 0; x < game.map.getW(); ++x)
					CHECK(snapshot.tileAt(game.map.coordToIndex(x, y)).canPaintFarm == game.map.canPaintFarmArea(x, y));
			return snapshot;
		};
		auto held = checkParity();
		const auto grassIndex = game.map.coordToIndex(19, 10);
		REQUIRE(held.tileAt(grassIndex).canPaintFarm);
		game.map.setResource(19, 10, STONE, 3);
		auto stone = checkParity();
		CHECK_FALSE(stone.tileAt(grassIndex).canPaintFarm);
		CHECK(held.tileAt(grassIndex).canPaintFarm);
		game.map.setResource(19, 10, WOOD, 3);
		CHECK(checkParity().tileAt(grassIndex).canPaintFarm);
		game.map.setResourcesGrow(19, 10, 0);
		CHECK_FALSE(checkParity().tileAt(grassIndex).canPaintFarm);
	}

	TEST_CASE("memory metrics distinguish held epochs from reusable high water capacity")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game;
		SimulationSnapshot::Store store;
		const auto resources = SimulationSnapshot::bit(SimulationSnapshot::Component::Resources);
		const auto areas = SimulationSnapshot::bit(SimulationSnapshot::Component::Areas);
		auto initial = store.captureBoundary(game, resources | areas);
		auto held = initial.project(resources);
		initial = {};
		const auto first = store.memoryMetrics();
		CHECK(first.allocatedBuffers == 2); CHECK(first.leasedBuffers == 2); CHECK(first.reusableBuffers == 0);
		++game.stepCounter; game.map.setResource(20, 20, WHEAT, 1);
		store.captureBoundary(game, resources);
		const auto retained = store.memoryMetrics();
		CHECK(retained.allocatedBuffers == 3); CHECK(retained.leasedBuffers == 2); CHECK(retained.reusableBuffers == 1);
		CHECK(retained.retainedBytes > first.retainedBytes);
		held = {};
		const auto released = store.memoryMetrics();
		CHECK(released.leasedBuffers == 1); CHECK(released.reusableBuffers == 2);
		CHECK(released.retainedBytes == retained.retainedBytes); CHECK(released.capacityBytes == retained.capacityBytes);
		CHECK(released.leasedBytes < retained.leasedBytes);
		CHECK(released.peakLeasedBytes == retained.peakLeasedBytes);
		CHECK(released.peakRetainedBytes == retained.retainedBytes);
	}
	TEST_CASE("same tick component expansion preserves narrow leases and captures arrays once")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		SimulationSnapshot::Store store;
		const auto terrainMask = SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain);
		auto terrain = store.captureBoundary(fixture.game, terrainMask);
		const auto terrainBytes = store.metrics.bytesCopied;
		auto full = store.captureBoundary(fixture.game, SimulationSnapshot::All);
		CHECK(full.terrain == terrain.terrain); CHECK_FALSE(terrain.resources); CHECK_FALSE(terrain.entities);
		CHECK(store.metrics.captures == 2); CHECK(store.metrics.reusedComponents >= 1);
		const auto bytes = store.metrics.bytesCopied;
		CHECK(bytes > terrainBytes);
		auto resources = store.captureBoundary(fixture.game, SimulationSnapshot::bit(SimulationSnapshot::Component::Resources));
		CHECK(resources.resources == full.resources); CHECK_FALSE(resources.terrain); CHECK_FALSE(resources.entities);
		CHECK(store.metrics.bytesCopied == bytes); CHECK(store.metrics.captures == 2);
	}

}
