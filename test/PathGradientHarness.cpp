// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real Uint16 pathfinding kernel against an independent heap oracle.
#include "Glob2Test.h"
#include <array>
#include "GlobalContainer.h"
#include "Map.h"
#include "BuildingGradientSearch.h"

#include <algorithm>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <queue>
#include <random>
#include <utility>
#include <vector>


namespace
{
constexpr Uint16 Blocked = 0, Unreached = 1, Goal = 65535;
constexpr int CostLimit = 65491;
static_assert(Map::GRADIENT_COST_LIMIT == CostLimit, "Update the oracle contract if the engine cap changes");

// Geometry and terrain are enough; avoid game state, Sector allocation, and data files.
struct PathMap : Map
{
	PathMap(int widthShift, int heightShift, const std::vector<Uint16>& terrain)
	{
		wDec = widthShift; hDec = heightShift;
		w = 1 << wDec; h = 1 << hDec;
		wMask = w - 1; hMask = h - 1;
		size = static_cast<size_t>(w) * h;
		tiles.assign(size, Tile());
		for (size_t i = 0; i < size; ++i) tiles[i].terrain = terrain[i];
	}
	void changeTerrain() { for (auto &tile : tiles) tile.terrain = tile.terrain == 256 ? 0 : 256; }
	~PathMap()
	{
		// Map::clear expects zero geometry when setSize has not built its arrays.
		w = h = wMask = hMask = wDec = hDec = 0;
		size = 0;
	}
};

uint64_t cases = 0, cellsChecked = 0, digest = 1469598103934665603ULL;
void require(bool condition, const char* message)
{
	GLOB2_REQUIRE(condition, message);
}

std::vector<Uint16> oracle(const std::vector<Uint16>& seeds,
	const std::vector<Uint16>& terrain, int width, int height, int swimClass, int maxCost)
{
	constexpr int waterSteps[] = {10, 5, 7, 10, 13, 20, 30};
	constexpr int Infinity = INT_MAX / 2;
	const int limit = std::min(maxCost, CostLimit);
	std::vector<int> distance(seeds.size(), Infinity);
	using Item = std::pair<int, size_t>;
	std::priority_queue<Item, std::vector<Item>, std::greater<Item>> frontier;
	for (size_t i = 0; i < seeds.size(); ++i)
		if (seeds[i] > Unreached)
		{
			distance[i] = Goal - seeds[i];
			frontier.emplace(distance[i], i);
		}
	while (!frontier.empty())
	{
		const auto [cost, i] = frontier.top(); frontier.pop();
		if (cost > limit) break;
		if (cost != distance[i]) continue;
		const int x = static_cast<int>(i % width), y = static_cast<int>(i / width);
		// Reverse traversal enters this settled cell in the forward path.
		const bool water = terrain[i] >= 256 && terrain[i] <= 271;
		const int cardinal = water ? waterSteps[swimClass] : 10;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (dx == 0 && dy == 0) continue;
				const int nx = (x + dx + width) % width;
				const int ny = (y + dy + height) % height;
				const size_t next = static_cast<size_t>(ny) * width + nx;
				if (seeds[next] == Blocked) continue;
				const int candidate = cost + (dx && dy ? cardinal * 14 / 10 : cardinal);
				if (candidate <= limit && candidate < distance[next])
				{
					distance[next] = candidate;
					frontier.emplace(candidate, next);
				}
			}
	}
	auto output = seeds; // Even seeds beyond the cap retain their original value.
	for (size_t i = 0; i < seeds.size(); ++i)
		if (seeds[i] != Blocked && distance[i] != Infinity)
			output[i] = static_cast<Uint16>(Goal - distance[i]);
	return output;
}

void check(int widthShift, int heightShift, const std::vector<Uint16>& seeds,
	const std::vector<Uint16>& terrain, int swimClass, int cap, const char* label)
{
	const int width = 1 << widthShift, height = 1 << heightShift;
	require(seeds.size() == static_cast<size_t>(width) * height && terrain.size() == seeds.size(), "fixture shape");
	const auto expected = oracle(seeds, terrain, width, height, swimClass, cap);
	PathMap map(widthShift, heightShift, terrain);
	auto actual = seeds;
	map.propagateGradient(actual.data(), swimClass, cap);
	for (size_t i = 0; i < actual.size(); ++i)
	{
		if (actual[i] != expected[i])
		{
			std::fprintf(stderr, "PathGradientHarness mismatch case=%" PRIu64 " label=%s shape=%dx%d class=%d cap=%d cell=%zu actual=%u expected=%u\n",
				cases, label, width, height, swimClass, cap, i, unsigned(actual[i]), unsigned(expected[i]));
			FAIL("gradient mismatch at cell " << i << " of case " << cases);
		}
		digest = (digest ^ actual[i]) * 1099511628211ULL;
	}
	// Reseed and repeat after an unrelated solve on another Map: shared queue
	// capacity/deferred seeds must never leak logical state between calls.
	std::vector<Uint16> otherTerrain(4, 256), otherSeeds{Goal, 1, 0, 65000};
	PathMap other(1, 1, otherTerrain);
	other.propagateGradient(otherSeeds.data(), (swimClass + 1) % 7, 43);
	actual = seeds;
	map.propagateGradient(actual.data(), swimClass, cap);
	require(actual == expected, "repeat after different Map/class/cap diverged");
	++cases; cellsChecked += actual.size();
}

void lazyBuildingChecks()
{
	std::mt19937 random(0xA975B13u);
	for (int swim = 0; swim < 7; ++swim)
	{
		// Stop near a source, change live terrain, then resume far away. The
		// snapshot must still equal the original eager solve, for every class.
		std::vector<Uint16> terrain(4096, 256), seeds(4096, Unreached);
		seeds[0] = Goal;
		PathMap map(6, 6, terrain);
		const auto expected = oracle(seeds, terrain, 64, 64, swim, CostLimit);
		auto actual = seeds;
		BuildingGradientSearch search;
		search.begin(map, actual.data(), swim);
		search.resolve(1);
		require(actual[1] == expected[1] && !search.complete(), "nearby lazy query must stop early");
		map.changeTerrain();
		search.resolve(32 + 32 * 64);
		require(actual[32 + 32 * 64] == expected[32 + 32 * 64], "lazy weighted terrain snapshot changed during a pause");
		search.finish();
		require(search.complete() && actual == expected, "completed lazy field differs from eager snapshot");
	}
	for (int trial = 0; trial < 140; ++trial)
	{
		const int ws = random() % 7, hs = random() % 7, swim = trial % 7;
		const int w = 1 << ws, h = 1 << hs;
		const size_t size = size_t(w) * h;
		std::vector<Uint16> seeds(size, Unreached), terrain(size), secondSeeds(size, Unreached);
		for (size_t i = 0; i < size; ++i)
		{
			terrain[i] = random() % 2 ? 256 : 0;
			if (random() % 4 == 0) seeds[i] = secondSeeds[i] = Blocked;
			if (trial % 11 != 0 && random() % 17 == 0) seeds[i] = Goal;
			if (random() % 23 == 0) secondSeeds[i] = Goal;
		}
		PathMap map(ws, hs, terrain), other(ws, hs, terrain);
		auto first = seeds, second = secondSeeds;
		const auto expected = oracle(seeds, terrain, w, h, swim, CostLimit);
		const auto otherExpected = oracle(secondSeeds, terrain, w, h, (swim + 1) % 7, CostLimit);
		BuildingGradientSearch a, b;
		a.begin(map, first.data(), swim);
		b.begin(other, second.data(), (swim + 1) % 7);
		for (size_t i = 0; i < size; ++i)
		{
			const size_t target = (i * 73 + 19) & (size - 1);
			a.resolve(target);
			require(a.resolved(target) && first[target] == expected[target], "lazy scalar query disagrees with heap oracle");
			// Every equal/better neighbor used by directionByGradient must be final.
			if (expected[target] > Unreached && expected[target] != Goal)
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const size_t n = (((target / w + dy) & (h - 1)) * w) + ((target % w + dx) & (w - 1));
						if (expected[n] >= expected[target])
							require(first[n] == expected[n], "equal/better neighbor was not settled before movement");
					}
			a.resolve(target); // repeated queries do not consume/drop queue entries
			b.resolve(size - 1 - target);
			require(second[size - 1 - target] == otherExpected[size - 1 - target], "interleaved Map queues contaminated one another");
		}
		a.finish(); b.finish();
		require(first == expected && second == otherExpected, "lazy final fields differ from oracle");
		// Reuse retained bucket capacities for a completely different field.
		first = secondSeeds;
		a.begin(map, first.data(), (swim + 1) % 7);
		a.finish();
		require(first == otherExpected, "rebuild retained stale frontier entries");
	}
	// Unreachable includes cells beyond the representable distance, not just
	// disconnected components. Exercise the limit without wraparound shortcuts.
	std::vector<Uint16> terrain(65536, 0), seeds(65536, Blocked);
	for (int y = 1; y < 253; ++y)
		if (y % 2) for (int x = 1; x < 254; ++x) seeds[y * 256 + x] = Unreached;
		else seeds[y * 256 + ((y / 2) % 2 ? 253 : 1)] = Unreached;
	seeds[257] = Goal;
	const auto expected = oracle(seeds, terrain, 256, 256, 0, CostLimit);
	PathMap map(8, 8, terrain);
	BuildingGradientSearch search;
	search.begin(map, seeds.data(), 0);
	search.finish();
	require(seeds == expected, "lazy distance limit differs from eager oracle");
	std::puts("PASS lazy building gradients: paused terrain snapshots, query ordering, equal-cost movement, unreachable cells, toroidal geometry, independent frontiers and rebuilds");
}

void parallelChecks()
{
	std::vector<Uint16> terrain(128 * 128, 0), seeds(terrain.size(), Unreached);
	for (size_t i = 0; i < terrain.size(); ++i)
	{
		if (i % 11 == 0) terrain[i] = 256;
		if (i % 19 == 0) seeds[i] = Blocked;
		if (i % 701 == 0) seeds[i] = Goal - (i % 80);
	}
	PathMap map(7, 7, terrain);
	std::array<std::vector<Uint16>, 7> expected;
	for (int swim = 0; swim < 7; ++swim) expected[swim] = oracle(seeds, terrain, 128, 128, swim, CostLimit);
	for (unsigned threads : {1, 2, 4, 8})
	{
		map.configureCompute(threads, 7);
		for (int repeat = 0; repeat < 5; ++repeat)
		{
			std::array<std::vector<Uint16>, 7> actual;
			for (auto &field : actual) field = seeds;
			map.computeExecutor().run(7, [&](size_t swim) { map.propagateGradient(actual[swim].data(), swim); });
			for (int swim = 0; swim < 7; ++swim) require(actual[swim] == expected[swim], "parallel propagation differs from oracle");
		}
		// Concurrent independent lazy searches, including nested water initialization.
		std::array<std::vector<Uint16>, 7> actual;
		for (auto &field : actual) { field.assign(terrain.size(), Unreached); field[0] = Goal; }
		map.computeExecutor().run(7, [&](size_t swim) {
			BuildingGradientSearch search;
			search.begin(map, actual[swim].data(), swim);
			search.resolve(123); search.finish();
		});
		std::vector<Uint16> goals(terrain.size(), Unreached); goals[0] = Goal;
		for (int swim = 0; swim < 7; ++swim)
			require(actual[swim] == oracle(goals, terrain, 128, 128, swim, CostLimit), "parallel lazy propagation differs");
	}
	std::puts("PASS parallel eager/lazy gradients at 1, 2, 4, 8 threads");
}

void analyticOracleCheck()
{
	const int width = 32, height = 16;
	std::vector<Uint16> terrain(width * height, 0), seeds(width * height, Unreached);
	seeds[0] = Goal;
	const auto values = oracle(seeds, terrain, width, height, 0, CostLimit);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			const int dx = std::min(x, width - x), dy = std::min(y, height - y);
			require(Goal - values[y * width + x] == 10 * std::max(dx, dy) + 4 * std::min(dx, dy), "oracle octile self-check");
		}
}
} // namespace

TEST_SUITE("PathGradient")
{
	TEST_CASE("analytic oracle") { analyticOracleCheck(); }
	TEST_CASE("parallel searches") { parallelChecks(); }
	TEST_CASE("lazy building fields") { lazyBuildingChecks(); }
	TEST_CASE("boundary; random and exhaustive seeds against the heap oracle")
	{
		std::mt19937 random(0x71A6D19u);
		constexpr int caps[] = {INT_MIN, -1, 0, 1, 4, 5, 7, 9, 10, 13, 14, 29, 30, 41, 42, 43, 44, 120, 65490, 65491, 65492, INT_MAX};
		constexpr Uint16 terrainKinds[] = {0, 255, 256, 257, 271, 272, 65535};
		// Explicit bucket-window and sentinel frontiers, including out-of-contract
		// expensive seeds as robustness checks of the existing preserved behavior.
		constexpr int seedCosts[] = {0, 1, 41, 42, 43, 44, 65490, 65491, 65492, 65533};
		for (const auto shape : std::vector<std::pair<int, int>>{{0,0},{0,5},{5,0},{1,1},{1,6},{6,1},{3,4}})
			for (int swim = 0; swim < 7; ++swim)
				for (int cap : caps)
				{
					const size_t size = size_t(1) << (shape.first + shape.second);
					std::vector<Uint16> seeds(size, Unreached), terrain(size);
					for (size_t i = 0; i < size; ++i)
					{
						terrain[i] = terrainKinds[i % 7];
						if (i % 5 == 0) seeds[i] = Blocked;
						if (i % 3 == 0) seeds[i] = Goal - seedCosts[(i / 3 + swim) % 10];
					}
					check(shape.first, shape.second, seeds, terrain, swim, cap, "boundary");
				}
		for (int trial = 0; trial < 420; ++trial)
		{
			const int ws = random() % 7, hs = random() % 7, swim = trial % 7;
			const size_t size = size_t(1) << (ws + hs);
			std::vector<Uint16> seeds(size, Unreached), terrain(size);
			for (size_t i = 0; i < size; ++i)
			{
				terrain[i] = terrainKinds[random() % 7];
				if (random() % 4 == 0) seeds[i] = Blocked;
				if (random() % 17 == 0) seeds[i] = Goal - random() % 65534;
			}
			if (trial % 3 == 0) seeds[random() % size] = Goal;
			check(ws, hs, seeds, terrain, swim, caps[trial % 22], "random");
		}
		for (int swim = 0; swim < 7; ++swim)
		{
			// Every possible Uint16 seed value, mixed terrain, and dense deferred queue.
			std::vector<Uint16> seeds(65536), terrain(65536);
			for (size_t i = 0; i < seeds.size(); ++i) { seeds[i] = static_cast<Uint16>(i); terrain[i] = terrainKinds[i % 7]; }
			check(8, 8, seeds, terrain, swim, CostLimit, "all seed values");
			std::fill(seeds.begin(), seeds.end(), Blocked);
			check(8, 8, seeds, terrain, swim, CostLimit, "all blocked");
			std::fill(seeds.begin(), seeds.end(), Unreached);
			check(8, 8, seeds, terrain, swim, CostLimit, "no sources");
			// Long serpentine corridor, isolated from toroidal edges. Its distant tail
			// lies beyond the cost cap; a disconnected island must remain unreachable.
			std::fill(seeds.begin(), seeds.end(), Blocked);
			std::fill(terrain.begin(), terrain.end(), 0);
			for (int y = 1; y < 253; ++y)
				if (y % 2) for (int x = 1; x < 254; ++x) seeds[y * 256 + x] = Unreached;
				else seeds[y * 256 + ((y / 2) % 2 ? 253 : 1)] = Unreached;
			seeds[257] = Goal;
			seeds[254 * 256 + 128] = Unreached;
			check(8, 8, seeds, terrain, swim, CostLimit, "long capped corridor");
		}
		MESSAGE("cases=" << cases << " exact_cells=" << cellsChecked << " digest=" << digest);
	}
}
