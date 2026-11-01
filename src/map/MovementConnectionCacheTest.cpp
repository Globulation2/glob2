// SPDX-License-Identifier: GPL-3.0-or-later
#include "MovementConnections.h"
#include "EngineFixtures.h"
#include <array>
#include <atomic>
#include <thread>

namespace
{
MapState::View uncached(MapState::View view)
{
	view.terrainConnections = nullptr;
	view.movementConnections = nullptr;
	return view;
}
} // namespace

TEST_SUITE("MovementConnectionCache")
{
	TEST_CASE("cached masks match direct compilation for corners seams and obstacles")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4});
		auto &map = fixture.game.map;
		MapState::TerrainConnectionCache terrain;
		MapState::MovementConnectionCache physical;
		for (TerrainType border : {WATER, OUTCROP})
			for (unsigned pattern = 0; pattern < 16; ++pattern)
			{
				CAPTURE(border);
				CAPTURE(pattern);
				map.fillTerrain(GRASS);
				{
					auto edit = map.editTerrain();
					for (unsigned corner = 0; corner < 4; ++corner)
						map.setVertexTerrain(15 + int(corner & 1), 15 + int(corner >> 1),
											 (pattern & (1u << corner)) ? border : GRASS);
				}
				map.setResourceByIndex(4, 4, WOOD, 1);
				map.paintCell(10, 10, WATER);
				map.setResourceByIndex(10, 10, ALGA, 1);
				map.setBuilding(7, 7, 1, 1, 0);
				map.setAreaMask(map.coordToIndex(6, 7), &Tile::forbidden, 1);
				terrain.clear();
				physical.clear();
				auto view = map.cellView();
				view.terrainConnections = &terrain;
				view.movementConnections = &physical;
				const auto direct = uncached(view);
				for (int movement : {0, 1, WATER_ONLY_CLASS})
					for (Uint32 team : {0u, 1u, 2u})
						for (bool interactions : {false, true})
						{
							CAPTURE(movement);
							CAPTURE(team);
							CAPTURE(interactions);
							const auto expected = MapState::compileMovementConnections(
								direct, movement, team, interactions);
							const auto actual =
								physical.incoming(view, movement, team, interactions);
							CHECK(*actual == *expected);
							const auto grid =
								MapState::movementGrid(view, movement, team, interactions);
							for (std::size_t i = 0; i < actual->size(); ++i)
								CHECK(grid.incomingMask(i) == (*expected)[i]);
						}
			}
	}

	TEST_CASE("terrain masks preserve blocked source escapes without opening search predecessors")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4});
		auto &map = fixture.game.map;
		map.setVertexTerrain(5, 5, OUTCROP);
		MapState::TerrainConnectionCache terrain;
		MapState::MovementConnectionCache physical;
		auto view = map.cellView();
		view.terrainConnections = &terrain;
		view.movementConnections = &physical;
		REQUIRE_FALSE(MapState::terrainAllowsMovement(view, view.index(5, 5), 0));
		const auto outgoing = MapState::terrainConnectionMasks(view, 0);
		CHECK((*outgoing)[view.index(5, 5)] & (1u << field::directionIndex(1, 0)));
		const auto incoming = physical.incoming(view, 0, 0, false);
		CHECK_FALSE((*incoming)[view.index(6, 5)] & (1u << field::directionIndex(-1, 0)));
	}

	TEST_CASE("profiles teams and interaction modes have stable distinct cache owners")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4});
		auto view = fixture.game.map.cellView();
		MapState::MovementConnectionCache cache;
		const auto walk = cache.incoming(view, 0, 1, false);
		CHECK(cache.incoming(view, 0, 1, false) == walk);
		const auto swim = cache.incoming(view, 1, 1, false);
		CHECK(cache.incoming(view, 2, 1, false) == swim);
		CHECK(swim != walk);
		CHECK(cache.incoming(view, WATER_ONLY_CLASS, 1, false) != swim);
		CHECK(cache.incoming(view, 0, 2, false) != walk);
		CHECK(cache.incoming(view, 0, 1, true) != walk);
		CHECK(cache.stats().builds == 5);
		CHECK(cache.stats().hits == 2);
	}

	TEST_CASE("concurrent requests publish one immutable owner")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4});
		MapState::TerrainConnectionCache terrain;
		MapState::MovementConnectionCache cache;
		auto view = fixture.game.map.cellView();
		view.terrainConnections = &terrain;
		std::array<MapState::ConnectionMasks, 8> results;
		std::array<std::thread, 8> threads;
		std::atomic<bool> ready = false;
		for (unsigned i = 0; i < threads.size(); ++i)
			threads[i] = std::thread(
				[&, i]
				{
					while (!ready.load(std::memory_order_acquire))
						std::this_thread::yield();
					results[i] = cache.incoming(view, 0, 1, false);
				});
		ready.store(true, std::memory_order_release);
		for (auto &thread : threads)
			thread.join();
		for (const auto &result : results)
			CHECK(result == results.front());
		CHECK(cache.stats().builds == 1);
		CHECK(cache.stats().hits == threads.size() - 1);
	}

	TEST_CASE("cache budgets evict strong owners without changing retained fields")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4});
		const auto view = fixture.game.map.cellView();
		const auto count = std::size_t(view.width) * view.height;
		MapState::MovementConnectionCache cache(2 * count);
		const auto first = cache.incoming(view, 0, 1, false);
		const auto saved = *first;
		cache.incoming(view, 0, 2, false);
		cache.incoming(view, 0, 4, false);
		CHECK(cache.stats().entries == 2);
		CHECK(cache.stats().retainedBytes <= 2 * count);
		CHECK(*first == saved);
		CHECK(cache.incoming(view, 0, 1, false) != first);
		CHECK(*cache.incoming(view, 0, 1, false) == saved);
		cache.clear();
		CHECK(cache.stats().entries == 0);
		CHECK(cache.stats().retainedBytes == 0);
		CHECK(*first == saved);
		MapState::MovementConnectionCache declined(count - 1);
		CHECK(*declined.incoming(view, 0, 1, false) == saved);
		CHECK(declined.stats().entries == 0);
	}
}
