// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real Uint16 pathfinding kernel against an independent heap oracle.
#include "Glob2Test.h"
#include <array>
#include "GlobalContainer.h"
#include "Map.h"
#include "BuildingGradientSearch.h"
#include "GradientPipeline.h"
#include "field/TerrainGradient.h"
#include "field/TerrainTravel.h"

#include <algorithm>
#include <cinttypes>
#include <chrono>
#include <ctime>
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
		// Test-only private access bootstraps this partial map.
		tiles.assign(size, Tile());
		for (size_t i = 0; i < size; ++i)
		{
			auto cell = getTile(i);
			cell.terrain = terrain[i];
			replaceTile(i, cell);
		}
        importLegacyTerrain();
	}
	void changeTerrain() { for (size_t i=0; i<size; ++i) setTerrain(i & wMask, i >> wDec, getTile(i).terrain == 256 ? 0 : 256); }
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
	const std::vector<Uint16>& terrain, int width, int height, int swimClass, int maxCost, const std::vector<TerrainType>* semantic = nullptr)
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
		int cardinal = water ? waterSteps[swimClass] : 10;
        if (semantic)
        {
            const auto &p = terrainProperties((*semantic)[i]);
            const int base = p.swimmable ? waterSteps[swimClass] : 10;
            cardinal = std::max(1, (base*256 + p.groundSpeedQ8/2)/p.groundSpeedQ8);
        }
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
TEST_CASE("immutable water snapshots share storage and retain their captured terrain [pathfinding]")
{
	std::vector<Uint16> terrain(16*16,0); terrain[0]=256;
	PathMap map(4,4,terrain);
	auto first=map.frozenWaterSnapshot();
	auto second=map.frozenWaterSnapshot();
	REQUIRE(first==second);
	map.setTerrain(0,0,257); REQUIRE(map.frozenWaterSnapshot()==first);
	map.setTerrain(0,0,0); auto changed=map.frozenWaterSnapshot();
	REQUIRE(changed!=first); REQUIRE((*first)[0]==1); REQUIRE((*changed)[0]==0);
	map.setTerrain(1,0,256); auto next=map.frozenWaterSnapshot();
	REQUIRE((*changed)[1]==0); REQUIRE((*next)[1]==1);
}


	TEST_CASE("analytic oracle") { analyticOracleCheck(); }
	TEST_CASE("parallel searches") { parallelChecks(); }
	TEST_CASE("lazy building fields") { lazyBuildingChecks(); }
	TEST_CASE("boundary; random and exhaustive seeds against the heap oracle")
	{
		std::mt19937 random(0x71A6D19u);
		constexpr int caps[] = {INT_MIN, -1, 0, 1, 4, 5, 7, 9, 10, 13, 14, 29, 30, 41, 42, 43, 44, 120, 65490, 65491, 65492, INT_MAX};
		constexpr Uint16 terrainKinds[] = {0, 15, 128, 255, 256, 257, 271};
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

TEST_CASE("mixed terrain costs match heap oracle and immutable lazy searches [pathfinding]")
{
    std::mt19937 random(1623);
    for (int trial=0;trial<90;++trial)
    {
        const int ws=4+trial%3,hs=4+trial%2,w=1<<ws,h=1<<hs,sw=trial%7;
        const size_t count=size_t(w)*h;
        std::vector<Uint16> oldTerrain(count,0),seeds(count,Unreached);
        std::vector<TerrainType> terrain(count);
        PathMap map(ws,hs,oldTerrain);
        for (size_t i=0;i<count;++i)
        {
            terrain[i]=static_cast<TerrainType>(random()%TERRAIN_COUNT);
            map.setCellTerrain(i,terrain[i]);
            seeds[i]=random()%6?Unreached:Blocked;
            if(random()%37==0)seeds[i]=Goal;
        }
        seeds[0]=Goal;
        const auto expected=oracle(seeds,oldTerrain,w,h,sw,CostLimit,&terrain);
        auto actual=seeds;
        map.propagateGradient(actual.data(),sw);
        REQUIRE(actual==expected);
        actual=seeds;
        BuildingGradientSearch search;
        search.begin(map,actual.data(),sw);
        search.resolve(1);
        for(size_t i=0;i<count;++i)map.setCellTerrain(i,GRASS);
        search.finish();
        REQUIRE(actual==expected);
    }
}

TEST_CASE("general terrain queue supports colliding costs and wrapped thin grids [pathfinding]")
{
    std::mt19937 random(7721);
    for (const auto shape : {std::pair{31,1},std::pair{2,17},std::pair{32,16}})
        for(int sw=0;sw<7;++sw)
        {
            const int w=shape.first,h=shape.second;
            std::vector<TerrainType> terrain(w*h);
            std::vector<Uint16> oldTerrain(w*h,0),seeds(w*h,Unreached);
            for(size_t i=0;i<seeds.size();++i)
            {
                terrain[i]=static_cast<TerrainType>(random()%TERRAIN_COUNT);
                if(random()%13==0) seeds[i]=Goal-(random()%90);
            }
            seeds[0]=Goal;
            auto expected=oracle(seeds,oldTerrain,w,h,sw,60,&terrain);
            auto actual=seeds;
            GradientWorkspace workspace;
            gradient_kernel::propagateTerrainField(actual.data(),sw,60,{w,h},workspace,
                [&](size_t i){return terrain[i];},true);
            REQUIRE(actual==expected);
            // Artificial equal cardinal/diagonal costs exercise one shared cursor
            // for all eight neighbors independently of the registered terrain set.
            gradient_kernel::TerrainEntryCosts aliasCosts;
            aliasCosts.fill({1,1});
            actual.assign(w*h,Unreached);actual[0]=Goal;
            for(auto &bucket:workspace.buckets)bucket.clear();
            workspace.buckets[0].push(0);size_t pending=1;
            for(int cost=0;pending;++cost)
                gradient_kernel::expandTerrainBucket(actual.data(),workspace.buckets.data(),pending,
                    cost,CostLimit,{w,h},aliasCosts,[&](size_t i){return terrain[i];});
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)
                REQUIRE(actual[y*w+x]==Goal-std::max(std::min(x,w-x),std::min(y,h-y)));
        }
}

TEST_CASE("legacy terrain import rejects unregistered sprite IDs [pathfinding]")
{
    for (Uint16 sprite : {Uint16(272),Uint16(65535)})
    {
        bool rejected=false;
        PathMap map(4,4,std::vector<Uint16>(256,0));
        auto tile=map.getTile(0,0);tile.terrain=sprite;map.replaceTile(0,0,tile);
        try { map.importLegacyTerrain(); }
        catch(const std::invalid_argument&) { rejected=true; }
        CHECK(rejected);
    }
}

TEST_CASE("strategic terrain distances retain wide costs until publishing tile estimates [pathfinding]")
{
    std::mt19937 random(8931);
    for(int trial=0;trial<30;++trial)
    {
        constexpr int w=32,h=16,infinity=INT_MAX;
        std::vector<TerrainType> terrain(w*h);
        std::vector<int> expected(w*h,infinity);
        std::vector<std::int16_t> values(w*h,0);
        for(int i=0;i<w*h;++i)
        {
            terrain[i]=static_cast<TerrainType>(random()%TERRAIN_COUNT);
            if(random()%5==0) values[i]=1;
            if(random()%29==0) {expected[i]=0;values[i]=2;}
        }
        const auto markers=values;
        // Independent Bellman-Ford oracle. Strategic fields preserve their
        // Chebyshev metric: diagonal and cardinal edges share the same base
        // cost, unlike the engine unit-navigation oracle above. Reverse edges
        // pay entry into the sourceward cell and keep fractional tile costs.
        for(int pass=0;pass<w*h;++pass)
        {
            bool changed=false;
            for(int from=0;from<w*h;++from)
            {
                if(expected[from]==infinity)continue;
                const int speed=terrainProperties(terrain[from]).groundSpeedQ8;
                const int step=std::max(1,(GRADIENT_STEP*256+speed/2)/speed);
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
                {
                    if(!dx&&!dy)continue;
                    const int next=((from/w+dy+h)%h)*w+(from%w+dx+w)%w;
                    if(markers[next]==1)continue;
                    if(expected[from]+step<expected[next])
                    {expected[next]=expected[from]+step;changed=true;}
                }
            }
            if(!changed)break;
        }
        field::expandTerrainTravel(values,w,h,field::TerrainTravel::Swim,
            [&](std::size_t i){return terrain[i];});
        for(int i=0;i<w*h;++i)
            CHECK_EQ(values[i],markers[i]==1?1:expected[i]==infinity?0:
                2+(expected[i]+GRADIENT_STEP-1)/GRADIENT_STEP);
    }
}


TEST_CASE("terrain snapshots survive paused searches and release obsolete generations [pathfinding]")
{
    for (int swim = 0; swim < 7; ++swim)
    {
        PathMap map(5, 5, std::vector<Uint16>(1024, 0));
        map.setCellTerrain(0, TRAIL);
        auto captured = map.frozenTerrainSnapshot();
        REQUIRE(captured == map.frozenTerrainSnapshot());
        std::weak_ptr<const std::vector<TerrainType>> old = captured;
        std::vector<Uint16> first(1024, Unreached), second(1024, Unreached);
        first[0] = second[0] = Goal;
        const auto expected = oracle(first, std::vector<Uint16>(1024, 0), 32, 32,
            swim, CostLimit, captured.get());
        BuildingGradientSearch a, b;
        a.begin(map, first.data(), swim);
        b.begin(map, second.data(), swim);
        a.resolve(1);
        REQUIRE_FALSE(a.complete());
        map.setCellTerrain(0, ICE);
        auto replacement = map.frozenTerrainSnapshot();
        REQUIRE(replacement != captured);
        REQUIRE((*captured)[0] == TRAIL);
        REQUIRE((*replacement)[0] == ICE);
        captured.reset();
        REQUIRE_FALSE(old.expired());
        a.finish();
        REQUIRE(first == expected);
        REQUIRE_FALSE(old.expired()); // The other search still owns this generation.
        b.clearForReuse();
        REQUIRE(old.expired());
        // Reusing the same scratch storage must capture the new cost profile.
        second.assign(1024, Unreached); second[0] = Goal;
        b.begin(map, second.data(), swim); b.finish();
        auto seeds = std::vector<Uint16>(1024, Unreached); seeds[0] = Goal;
        REQUIRE(second == oracle(seeds, std::vector<Uint16>(1024, 0), 32, 32,
            swim, CostLimit, replacement.get()));
    }
}

// Opt-in production search benchmark. Measurements intentionally have no timing
// assertions: compare matching compiler/input runs, not timings from CI hosts.
TEST_CASE("production lazy gradient phases [benchmark][pathfinding]")
{
    using Clock = std::chrono::steady_clock;
    auto elapsed = [](Clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
    };
    std::printf("lazy_gradient,width,layout,swim,query,repeat,snapshot_ns,begin_ns,resolve_ns,cpu_ns,popped,retained_bytes,snapshot_bytes,digest\n");
    for (int shift : {5, 7, 9}) for (int layout = 0; layout < 5; ++layout)
    {
        const int width = 1 << shift;
        const std::size_t count = std::size_t(width) * width;
        PathMap map(shift, shift, std::vector<Uint16>(count, 0));
        // Classic, connected roads, dense mixes, uniform road, and uniform ice.
        if (layout) for (std::size_t i = 0; i < count; ++i)
            map.setCellTerrain(i, layout == 1
                ? ((i % width) % 16 == 0 || (i / width) % 16 == 0 ? TRAIL : GRASS)
                : layout == 3 ? TRAIL : layout == 4 ? ICE
                : static_cast<TerrainType>((i * 37 + i / width * 19) % TERRAIN_COUNT));
        const auto captureStart = Clock::now();
        auto snapshot = map.frozenTerrainSnapshot();
        const auto captureNs = elapsed(captureStart);
        for (int swim = 0; swim < 7; ++swim) for (int query = 0; query < 3; ++query)
        {
            std::vector<Uint16> seeds(count, Unreached), actual;
            const std::size_t target = query == 0 ? 1 : (count + width) / 2;
            // An unblocked island surrounded by eight obstacles forces exhaustion.
            if (query == 2) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy) seeds[target + dy * width + dx] = Blocked;
            seeds[0] = Goal;
            const auto expected = oracle(seeds, std::vector<Uint16>(count, 0),
                width, width, swim, CostLimit, snapshot.get());
            BuildingGradientSearch search;
            for (int repeat = 0; repeat < 5; ++repeat)
            {
                actual = seeds;
                const auto cpu = std::clock();
                const auto start = Clock::now();
                search.begin(map, actual.data(), swim);
                const auto beginNs = elapsed(start);
                const auto resolveStart = Clock::now();
                search.resolve(target);
                const auto resolveNs = elapsed(resolveStart);
                const auto cpuNs = std::int64_t((std::clock()-cpu) * (1000000000.0/CLOCKS_PER_SEC));
                REQUIRE(actual[target] == expected[target]);
                std::uint64_t hash = 1469598103934665603ULL;
                for (auto value : actual) hash = (hash ^ value) * 1099511628211ULL;
                std::printf("lazy_gradient,%d,%d,%d,%d,%d,%lld,%lld,%lld,%lld,%llu,%zu,%zu,%llu\n",
                    width,layout,swim,query,repeat,static_cast<long long>(captureNs),
                    static_cast<long long>(beginNs),static_cast<long long>(resolveNs),
                    static_cast<long long>(cpuNs),static_cast<unsigned long long>(search.poppedEntries()),
                    search.retainedBytes(),snapshot->size()*sizeof(TerrainType),
                    static_cast<unsigned long long>(hash));
            }
        }
    }
}


TEST_CASE("queued mixed gradients retain terrain costs until fixed publication [pathfinding]")
{
    for (unsigned workers : {0u, 2u}) for (int swim = 0; swim < 7; ++swim)
    for (unsigned seedCost : {0u, 50u})
    {
        constexpr unsigned width = 32, count = width * width;
        PathMap map(5, 5, std::vector<Uint16>(count, 0));
        for (unsigned i = 0; i < count; ++i)
            map.setCellTerrain(i, static_cast<TerrainType>(i % TERRAIN_COUNT));
        auto snapshot = map.frozenTerrainSnapshot();
        std::vector<Uint16> seeds(count, Unreached); seeds[0] = Goal - seedCost;
        for (unsigned i = 7; i < count; i += 17) seeds[i] = Blocked;
        // Market sources start at cost 50; round-trip sources can also be
        // deferred beyond a complete bucket-ring revolution.
        if (seedCost) seeds[count / 2] = Goal - (gradient_kernel::BUCKETS + seedCost);
        const auto expected = oracle(seeds, std::vector<Uint16>(count, 0), width,
            width, swim, CostLimit, snapshot.get());
        auto *published = new Uint16[count]{};
        GradientPipeline pipeline;
        pipeline.configure(workers, 2, count, [](auto &job, auto &workspace) {
            const auto *types = job.terrain->data();
            gradient_kernel::propagateTerrainField(job.data.get(), job.swim,
                CostLimit, {width, width}, workspace,
                [types](std::size_t i) { return types[i]; }, job.modifiedCosts);
        });
        pipeline.advance();
        pipeline.submit(&published, swim, [&](auto &job) {
            std::copy(seeds.begin(), seeds.end(), job.data.get());
            job.terrain = snapshot; job.modifiedCosts = true;
        });
        for (unsigned i = 0; i < count; ++i) map.setCellTerrain(i, GRASS);
        snapshot.reset();
        pipeline.finish();
        REQUIRE(published[0] == 0);
        pipeline.advance(); REQUIRE(published[0] == 0);
        pipeline.advance();
        REQUIRE(std::equal(expected.begin(), expected.end(), published));
        pipeline.reset(); delete[] published;
    }
}

TEST_CASE("strategic terrain queue matches independent wide heap across boundaries and saturation [pathfinding]")
{
    auto checkTravel = [](int width, int height, const std::vector<TerrainType> &terrain,
        const std::vector<std::int16_t> &markers, field::TerrainTravel mode) {
        constexpr std::uint64_t infinity = std::numeric_limits<std::uint64_t>::max();
        using Entry = std::pair<std::uint64_t, std::size_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
        std::vector<std::uint64_t> distance(markers.size(), infinity);
        for (std::size_t i = 0; i < markers.size(); ++i) if (markers[i] == 2)
        { distance[i] = 0; queue.push({0, i}); }
        while (!queue.empty())
        {
            const auto [cost, index] = queue.top(); queue.pop();
            if (cost != distance[index]) continue;
            const auto &properties = terrainProperties(terrain[index]);
            const unsigned speed = mode == field::TerrainTravel::Fly
                ? properties.airSpeedQ8 : properties.groundSpeedQ8;
            const unsigned step = std::max(1u, (10u * 256u + speed / 2u) / speed);
            const int x = index % width, y = index / width;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            {
                if (!dx && !dy) continue;
                const std::size_t next = ((y + dy + height) % height) * width + (x + dx + width) % width;
                if (markers[next] == 1 || cost + step >= distance[next]) continue;
                distance[next] = cost + step; queue.push({distance[next], next});
            }
        }
        auto actual = markers;
        field::expandTerrainTravel(actual, width, height, mode,
            [&](std::size_t i) { return terrain[i]; });
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            const auto expected = markers[i] == 1 ? 1 : distance[i] == infinity ? 0
                : 2 + std::min<std::uint64_t>(32765, (distance[i] + 9) / 10);
            REQUIRE(actual[i] == expected);
        }
        return actual;
    };
    std::mt19937 random(0x853bb7u);
    for (const auto [width, height] : {std::pair{1,1}, {1,31}, {33,1}, {2,17}, {31,33}, {32,64}})
        for (int pattern = 0; pattern < 4; ++pattern)
            for (auto mode : {field::TerrainTravel::Walk, field::TerrainTravel::Swim, field::TerrainTravel::Fly})
            {
                std::vector<TerrainType> terrain(width * height);
                std::vector<std::int16_t> markers(width * height, 0);
                for (std::size_t i = 0; i < markers.size(); ++i)
                {
                    terrain[i] = static_cast<TerrainType>(random() % TERRAIN_COUNT);
                    if (pattern == 1) markers[i] = 2; // All sources.
                    if (pattern >= 2)
                        markers[i] = random() % 5 == 0 ? 1 : random() % 13 == 0 ? 2 : 0;
                }
                if (pattern == 3)
                {
                    // Source markers override terrain traversal eligibility. The
                    // caller already froze blockers, so the queue must honor them.
                    terrain[0] = WATER; markers[0] = 2;
                }
                checkTravel(width, height, terrain, markers, mode);
                std::reverse(terrain.begin(), terrain.end());
                std::reverse(markers.begin(), markers.end());
                checkTravel(width, height, terrain, markers, mode);
            }
    constexpr int width = 512, height = 512;
    std::vector<TerrainType> terrain(width * height, ICE);
    std::vector<std::int16_t> markers(width * height, 1);
    // A long isolated snake crosses the signed-short publication limit. Queue
    // ordering must retain full cost even after the public distance saturates.
    for (int y = 1; y < height - 1; ++y)
        if (y % 2) for (int x = 1; x < width - 1; ++x) markers[y * width + x] = 0;
        else markers[y * width + ((y / 2) % 2 ? width - 2 : 1)] = 0;
    markers[width + 1] = 2;
    const auto actual = checkTravel(width, height, terrain, markers, field::TerrainTravel::Walk);
    REQUIRE(std::count(actual.begin(), actual.end(), std::int16_t(32767)) > 1000);
}

TEST_CASE("lazy terrain costs preserve uniform materials and source costs [pathfinding]")
{
    for (int swim = 0; swim < 7; ++swim)
        for (TerrainType material : {TRAIL, ICE, GRASS})
            for (bool differentGoal : {false, true})
            {
                constexpr int width = 32, count = width * width;
                PathMap map(5, 5, std::vector<Uint16>(count, 0));
                std::vector<TerrainType> terrain(count, material);
                std::vector<Uint16> seeds(count, Unreached);
                terrain[count - 1] = TRAIL; seeds[count - 1] = Blocked;
                if (differentGoal) terrain[0] = material == ICE ? TRAIL : ICE;
                seeds[0] = Goal;
                for (int i = 0; i < count; ++i) map.setCellTerrain(i, terrain[i]);
                const auto expected = oracle(seeds, std::vector<Uint16>(count, 0),
                    width, width, swim, CostLimit, &terrain);
                // Map dispatch and resumable searches are separate entry points;
                // both must charge the goal's terrain in the reverse field.
                auto actual = seeds;
                map.propagateGradient(actual.data(), swim);
                REQUIRE(actual == expected);
                actual = seeds;
                BuildingGradientSearch search;
                search.begin(map, actual.data(), swim);
                search.resolve(1);
                REQUIRE(actual[1] == expected[1]);
                map.setCellTerrain(0, material == ICE ? TRAIL : ICE);
                search.finish();
                REQUIRE(actual == expected);
            }
}

TEST_CASE("prepared terrain profiles deduplicate pairs and alias queue destinations [pathfinding]")
{
    constexpr gradient_kernel::PreparedTerrainCosts<4> profile(
        std::array<gradient_kernel::EntrySteps,4>{{{5,7}, {5,7}, {7,10}, {3,3}}});
    REQUIRE(profile.classCount == 3);
    REQUIRE(profile.stepCount == 4);
    REQUIRE(profile.terrainClasses[0] == profile.terrainClasses[1]);
    const auto a = profile.terrainClasses[0], b = profile.terrainClasses[2], c = profile.terrainClasses[3];
    REQUIRE(profile.diagonalSlots[a] == profile.cardinalSlots[b]);
    REQUIRE(profile.cardinalSlots[c] == profile.diagonalSlots[c]);
    for (unsigned s = 0; s < profile.stepCount; ++s)
    {
        REQUIRE(profile.steps[s] > 0);
        REQUIRE(profile.steps[s] < gradient_kernel::BUCKETS);
        REQUIRE(profile.maxAppends[s] == (profile.steps[s] == 3 ? 8 : 4));
    }
    for (const auto &registered : gradient_kernel::PREPARED_TERRAIN_COSTS)
        for (unsigned s = 0; s < registered.stepCount; ++s)
        {
            REQUIRE(registered.steps[s] > 0);
            REQUIRE(registered.steps[s] < gradient_kernel::BUCKETS);
        }
}

TEST_CASE("eager terrain specialization proves uniform costs across the whole field [pathfinding]")
{
    enum class Variation
    {
        Uniform,
        DifferentGoal,
        BlockedOutlier,
        DistantOutlier,
        DeferredSeed,
        NoGoals,
        AllBlocked
    };
    // Reuse one workspace across uniform/general dispatch, deferred seeds and
    // empty solves so retained queues cannot supply stale results.
    GradientWorkspace workspace;
    for (const auto [width, height] : {std::pair{32, 32}, {17, 5}, {1, 31}})
        for (int swim = 0; swim < 7; ++swim)
            for (const auto variation : {Variation::Uniform, Variation::DifferentGoal,
                Variation::BlockedOutlier, Variation::DistantOutlier,
                Variation::DeferredSeed, Variation::NoGoals, Variation::AllBlocked})
                for (int cap : {0, 127, CostLimit})
                {
                    INFO("shape=" << width << "x" << height << " swim=" << swim
                        << " cap=" << cap << " variation=" << static_cast<int>(variation));
                    const auto count = std::size_t(width) * height;
                    // Stay beyond the seed neighborhood but before the torus
                    // midpoint, so a cheaper cell can improve routes beyond it.
                    const auto distant = std::size_t(height / 3) * width + width / 3;
                    std::vector<TerrainType> terrain(count, ICE);
                    std::vector<Uint16> seeds(count, Unreached), legacy(count, 0);
                    seeds[0] = Goal;
                    switch (variation)
                    {
                    case Variation::DifferentGoal:
                        terrain[0] = TRAIL;
                        break;
                    case Variation::BlockedOutlier:
                        terrain[distant] = TRAIL;
                        seeds[distant] = Blocked;
                        break;
                    case Variation::DistantOutlier:
                        // A distant cheaper cell is still relevant: fields must
                        // not specialize from the goals or a local sample alone.
                        terrain[distant] = TRAIL;
                        break;
                    case Variation::DeferredSeed:
                        seeds[0] = Goal - (gradient_kernel::BUCKETS + 31);
                        break;
                    case Variation::NoGoals:
                        seeds[0] = Unreached;
                        break;
                    case Variation::AllBlocked:
                        terrain[distant] = TRAIL;
                        std::fill(seeds.begin(), seeds.end(), Blocked);
                        break;
                    case Variation::Uniform:
                        break;
                    }
                    const auto expected = oracle(seeds, legacy, width, height,
                        swim, cap, &terrain);
                    auto actual = seeds;
                    gradient_kernel::propagateTerrainField(actual.data(), swim, cap,
                        {width, height}, workspace,
                        [&](std::size_t i) { return terrain[i]; }, true);
                    REQUIRE(actual == expected);
                    if (variation == Variation::DistantOutlier && cap == CostLimit)
                    {
                        const std::vector<TerrainType> uniform(count, ICE);
                        // Prove that this fixture actually exposes a missed road,
                        // rather than merely including an irrelevant outlier.
                        REQUIRE(expected != oracle(seeds, legacy, width, height,
                            swim, cap, &uniform));
                    }
                }
}
