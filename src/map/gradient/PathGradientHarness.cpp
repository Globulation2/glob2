// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real Uint16 pathfinding kernel against an independent heap oracle.
#include "Glob2Test.h"
#include <array>
#include "GlobalContainer.h"
#include "Map.h"
#include "BuildingGradientSearch.h"
#include "field/TerrainGradient.h"
#include "field/TerrainTravel.h"

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
        importLegacyTerrain();
	}
	void changeTerrain() { for (size_t i=0; i<size; ++i) setTerrain(i & wMask, i >> wDec, tiles[i].terrain == 256 ? 0 : 256); }
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
        map.getTile(0,0).terrain=sprite;
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
