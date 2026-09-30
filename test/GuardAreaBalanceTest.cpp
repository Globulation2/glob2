// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Guard-area balancing, the "guard-area-balancing" experiment, measured on the
// real engine. Free warriors used to walk to the nearest painted guard area
// whatever was already there, so one area got the whole batch and the others
// got nobody. With the crowding-seeded guard gradient
// (docs/features/guard-area-balancing.md) they spread between areas in
// proportion to painted size. Every case runs the real simulation on a blank
// 64x64 grass map with one team, so a change in tuning or in the movement code
// shows up as numbers. Counts are warriors within a few tiles of an area's
// centre, so a warrior milling around a full area still counts as guarding it.
// Warriors are kept fed so hunger never takes them away.
//
// The experiment is baked into the game's header (GameOptions::experiments);
// the first case runs the same scenario without it and expects the old outcome,
// which is what proves the gate.

#include "EngineFixtures.h"
#include "Brush.h"
#include "ExperimentalFeatures.h"
#include "MapInternal.h"
#include "Order.h"
#include "UnitConsts.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace GAGCore;

namespace
{
	struct Options
	{
		int ticks = 4000;
		int warriors = 24;
		bool resourceGradients = false; // allocate the resource fields a small game has: the guard field refreshes about every 10 ticks
		bool slowCadence = false;       // allocate what a large game has: about every 170 ticks
		Uint32 seed = 1;
		bool experiment = true;
	};

	// A painted square and how it is counted.
	struct Zone
	{
		const char* name;
		int x, y;        // centre
		int half;        // painted box is (2*half+1) square
		int countRadius; // warriors within this many tiles count as "at" the zone
	};

	// Simulation checksum of everything that matters, for continuation checks.
	std::vector<Uint32> simulationState(Game& game)
	{
		std::vector<Uint32> result, buildings, units;
		game.checkSum(&result, &buildings, &units, true);
		result.erase(result.begin()); // the save path upgrades the map format header
		result.insert(result.end(), buildings.begin(), buildings.end());
		result.insert(result.end(), units.begin(), units.end());
		return result;
	}

	// Steps a game, keeping every warrior fed so the scenarios stay about
	// guarding rather than eating. Returns the mean wall time of a step in
	// microseconds; movedTiles, if given, accumulates the number of tile changes.
	double stepGame(Game& game, Team* team, int ticks, int* movedTiles = nullptr)
	{
		std::vector<int> before(Unit::MAX_COUNT, -1);
		int moved = 0;
		const auto start = std::chrono::steady_clock::now();
		for (int t = 0; t < ticks; ++t)
		{
			if (movedTiles)
				for (int i = 0; i < Unit::MAX_COUNT; ++i)
					if (Unit* u = team->myUnits[i])
						before[i] = game.map.coordToIndex(u->posX, u->posY);
			game.syncStep(0);
			for (int i = 0; i < Unit::MAX_COUNT; ++i)
				if (Unit* u = team->myUnits[i])
					u->hungry = Unit::HUNGRY_MAX;
			if (movedTiles)
				for (int i = 0; i < Unit::MAX_COUNT; ++i)
					if (Unit* u = team->myUnits[i])
						moved += (int)game.map.coordToIndex(u->posX, u->posY) != before[i];
		}
		const auto end = std::chrono::steady_clock::now();
		if (movedTiles)
			*movedTiles = moved;
		return std::chrono::duration<double, std::micro>(end - start).count() / std::max(1, ticks);
	}

	glob2test::GameOptions worldOptions(const Options& options, int sizeShift)
	{
		glob2test::GameOptions game;
		game.wDec = game.hDec = sizeShift;
		game.terrain = GRASS;
		game.teams = 1;
		game.header = true;
		game.seed = options.seed;
		if (options.experiment)
			game.experiments.set(ExperimentId::GuardAreaBalancing);
		return game;
	}

	struct World
	{
		glob2test::HeadlessGame headless;
		Game& game;
		Team* team;

		explicit World(const Options& options, int sizeShift = 6)
			: headless(worldOptions(options, sizeShift)), game(headless.game), team(headless.team)
		{
			if (options.resourceGradients)
				for (int r = 0; r < MAX_RESOURCES; ++r)
					game.map.getResourceGradient(0, r, 0);
			if (options.slowCadence)
			{
				// Two more teams with every resource field allocated: the round-robin
				// then visits the guard field about every 170 ticks.
				game.addTeam(1);
				game.addTeam(2);
				for (int t = 0; t < 3; ++t)
					for (int r = 0; r < MAX_RESOURCES; ++r)
						for (int c = 0; c < SWIM_CLASS_COUNT; ++c)
							game.map.getResourceGradient(t, r, c);
			}
		}

		// Paint or erase a zone with the same order a player's brush sends.
		// keep, when given, picks which tiles of the zone's box are painted.
		void paint(const Zone& zone, BrushTool::Mode mode = BrushTool::MODE_ADD, bool (*keep)(int dx, int dy) = nullptr)
		{
			BrushAccumulator acc;
			for (int dy = -zone.half; dy <= zone.half; ++dy)
				for (int dx = -zone.half; dx <= zone.half; ++dx)
					if (!keep || keep(dx, dy))
						acc.applyBrush(BrushApplication((zone.x + dx) & game.map.wMask, (zone.y + dy) & game.map.hMask, 0), &game.map);
			std::shared_ptr<Order> order(new OrderAlterGuardArea(0, mode, &acc, &game.map));
			order->sender = 0;
			game.executeOrder(order, 0);
			REQUIRE(game.map.isGuardArea(zone.x, zone.y, team->me) == (mode == BrushTool::MODE_ADD));
		}

		// Spawn level-0 warriors in a compact block around (x, y), the way a
		// barracks batch stands together.
		std::vector<Unit*> spawnWarriors(int x, int y, int count)
		{
			std::vector<Unit*> units;
			for (int r = 0; (int)units.size() < count && r < 16; ++r)
				for (int dy = -r; dy <= r && (int)units.size() < count; ++dy)
					for (int dx = -r; dx <= r && (int)units.size() < count; ++dx)
					{
						if (std::max(std::abs(dx), std::abs(dy)) != r)
							continue;
						if (Unit* u = game.addUnit(x + dx, y + dy, 0, WARRIOR, 0, 0, 0, 0))
							units.push_back(u);
					}
			REQUIRE((int)units.size() == count);
			return units;
		}

		int countNear(const Zone& zone) const
		{
			int n = 0;
			for (int i = 0; i < Unit::MAX_COUNT; ++i)
			{
				Unit* u = team->myUnits[i];
				if (!u || u->isDead || u->typeNum != WARRIOR)
					continue;
				if (game.map.warpDistSquare(u->posX, u->posY, zone.x, zone.y) <= zone.countRadius * zone.countRadius)
					++n;
			}
			return n;
		}

		int countFree() const
		{
			int n = 0;
			for (int i = 0; i < Unit::MAX_COUNT; ++i)
			{
				Unit* u = team->myUnits[i];
				if (u && !u->isDead && u->typeNum == WARRIOR && u->medical == Unit::MED_FREE && u->activity == Unit::ACT_RANDOM)
					++n;
			}
			return n;
		}

		double run(int ticks, int* movedTiles = nullptr)
		{
			return stepGame(game, team, ticks, movedTiles);
		}
	};

	void printHeader(const char* scenario, const Zone& a, const Zone& b)
	{
		std::printf("[%s] tick  %8s  %8s  elsewhere  free\n", scenario, a.name, b.name);
	}

	void printRow(World& world, const char* scenario, int tick, const Zone& a, const Zone& b, int total)
	{
		const int na = world.countNear(a), nb = world.countNear(b);
		std::printf("[%s] %5d  %8d  %8d  %9d  %4d\n", scenario, tick, na, nb, total - na - nb, world.countFree());
	}

	// The two areas every scenario below shares: "near" is 18 tiles east of the
	// spawn block, "far" 32 tiles south of it.
	const Zone NEAR{"near", 30, 12, 2, 7};
	const Zone FAR{"far", 12, 44, 2, 7};
	const int SPAWN_X = 12, SPAWN_Y = 12;

	// The three guard-field refresh cadences a game can have.
	Options cadence(int which)
	{
		Options options;
		options.resourceGradients = which == 1;
		options.slowCadence = which == 2;
		return options;
	}
	const char* cadenceName(int which)
	{
		return which == 0 ? "every tick" : which == 1 ? "about every 10 ticks" : "about every 170 ticks";
	}

	// Warriors spawn near the base and two areas are painted at once, the second
	// one farther away. Legacy: everyone walks to the near one.
	void spawnSplitsBetweenAreas(const Options& options)
	{
		World world(options);
		world.paint(NEAR);
		world.paint(FAR);
		world.spawnWarriors(SPAWN_X, SPAWN_Y, options.warriors);
		REQUIRE(world.game.integrity());

		printHeader("spawn", NEAR, FAR);
		const int sample = 250;
		double stepMicros = 0;
		int arrived = -1;
		for (int tick = 0; tick < options.ticks; tick += sample)
		{
			printRow(world, "spawn", tick, NEAR, FAR, options.warriors);
			if (arrived < 0 && world.countNear(NEAR) + world.countNear(FAR) >= options.warriors * 9 / 10)
				arrived = tick;
			stepMicros += world.run(std::min(sample, options.ticks - tick)) * std::min(sample, options.ticks - tick);
		}
		int moved = 0;
		stepMicros += world.run(500, &moved) * 500;
		printRow(world, "spawn", options.ticks + 500, NEAR, FAR, options.warriors);
		std::printf("[spawn] average step %.1f us, tile moves per warrior over the last 500 ticks %.2f, 90%% arrived by tick %d\n",
			stepMicros / (options.ticks + 500), (double)moved / options.warriors, arrived);
		REQUIRE(world.game.integrity());

		const int na = world.countNear(NEAR), nb = world.countNear(FAR);
		if (!options.experiment)
		{
			// The game as it always was: the whole batch takes the near area.
			GLOB2_CHECK(na >= options.warriors * 9 / 10, "without the experiment nearly every warrior takes the near area");
			GLOB2_CHECK(nb == 0, "without the experiment nobody takes the far area");
			return;
		}
		GLOB2_CHECK(na + nb >= options.warriors * 9 / 10, "nearly every warrior reaches an area");
		GLOB2_CHECK(nb >= options.warriors / 4, "at least a quarter of the warriors take the far area");
		GLOB2_CHECK(na >= options.warriors / 4, "at least a quarter of the warriors take the near area");
		GLOB2_CHECK((double)moved / options.warriors < 40.0, "warriors settle instead of shuttling between the areas");
	}

	// Warriors already stand in a full area when a second one is painted; some
	// must migrate, and the first area must not empty out and refill (the herd
	// effect). Legacy: nobody ever leaves an area.
	void clumpDrainsIntoNewArea(const Options& options)
	{
		World world(options);
		world.paint(NEAR);
		world.spawnWarriors(NEAR.x, NEAR.y, options.warriors);
		world.run(300);
		REQUIRE(world.countNear(NEAR) == options.warriors);
		world.paint(FAR);
		REQUIRE(world.game.integrity());

		printHeader("drain", NEAR, FAR);
		const int sample = 250;
		int lowestNear = options.warriors;
		for (int tick = 0; tick < options.ticks; tick += 50)
		{
			if (tick % sample == 0)
				printRow(world, "drain", tick, NEAR, FAR, options.warriors);
			lowestNear = std::min(lowestNear, world.countNear(NEAR));
			world.run(std::min(50, options.ticks - tick));
		}
		int moved = 0;
		world.run(500, &moved);
		printRow(world, "drain", options.ticks + 500, NEAR, FAR, options.warriors);
		std::printf("[drain] tile moves per warrior over the last 500 ticks %.2f, lowest count in the first area %d, final %d\n",
			(double)moved / options.warriors, lowestNear, world.countNear(NEAR));
		REQUIRE(world.game.integrity());

		if (!options.experiment)
		{
			GLOB2_CHECK(world.countNear(FAR) == 0, "without the experiment nobody leaves the first area");
			return;
		}
		GLOB2_CHECK(world.countNear(FAR) >= options.warriors / 4, "at least a quarter of the clump migrates to the new area");
		GLOB2_CHECK(world.countNear(NEAR) >= options.warriors / 4, "the first area keeps at least a quarter");
		// A warrior with no free painted neighbour takes an ordinary random step
		// off the paint, where the guard field may send it on to the other area,
		// so the first area can dip a few warriors below where it settles before
		// they come back: 1 to 5 of 24 over seeds 1-10 at all three cadences.
		// What this guards against is the herd effect, an area emptying out and
		// refilling, so the bound is a quarter of the warriors.
		GLOB2_CHECK(lowestNear >= world.countNear(NEAR) - options.warriors / 4, "the first area drains without emptying out and refilling");
	}

	// Two unconnected patches three tiles apart count as one position of their
	// combined painted size (18 tiles): they must neither pull the whole team nor
	// be starved next to a 25-tile area.
	void nearbyPatchesShareWarriors(const Options& options)
	{
		World world(options);
		const Zone a{"patchA", 30, 12, 1, 5};
		const Zone b{"patchB", 30, 17, 1, 5};
		world.paint(a);
		world.paint(b);
		world.paint(FAR);
		world.spawnWarriors(SPAWN_X, SPAWN_Y, options.warriors);
		world.run(options.ticks);
		int nab = 0;
		for (int i = 0; i < Unit::MAX_COUNT; ++i)
			if (Unit* u = world.team->myUnits[i])
				if (world.game.map.warpDistSquare(u->posX, u->posY, 30, 14) <= 8 * 8)
					++nab;
		const int nfar = world.countNear(FAR);
		std::printf("[patches] near pair %d  far %d  elsewhere %d\n", nab, nfar, options.warriors - nab - nfar);
		GLOB2_CHECK(nfar >= options.warriors / 4, "the far area still gets its share next to a pair of patches");
		GLOB2_CHECK(nab >= options.warriors / 4, "the pair of patches gets its share");
	}

	// A larger painted area takes more warriors, even when it is the farther one.
	// Legacy: the nearer area takes everyone whatever the sizes.
	void largerAreaTakesMore(const Options& options)
	{
		World world(options);
		const Zone small{"small", 30, 12, 2, 7};  // 5x5, 18 tiles away
		const Zone large{"large", 12, 44, 4, 9};  // 9x9, 32 tiles away
		world.paint(small);
		world.paint(large);
		world.spawnWarriors(SPAWN_X, SPAWN_Y, options.warriors);
		world.run(options.ticks);
		const int ns = world.countNear(small), nl = world.countNear(large);
		std::printf("[size] small 5x5 %d  large 9x9 %d  elsewhere %d\n", ns, nl, options.warriors - ns - nl);
		GLOB2_CHECK(nl > ns, "the larger area holds more warriors than the smaller nearer one");
		GLOB2_CHECK(ns >= options.warriors / 8, "the smaller area is not abandoned");
	}

	// Three areas at increasing distance all get guarded.
	void threeAreasAllGuarded(const Options& options)
	{
		World world(options);
		const Zone c{"third", 44, 44, 2, 7}; // about 45 tiles from the spawn block
		world.paint(NEAR);
		world.paint(FAR);
		world.paint(c);
		world.spawnWarriors(SPAWN_X, SPAWN_Y, options.warriors);
		world.run(options.ticks);
		const int na = world.countNear(NEAR), nb = world.countNear(FAR), nc = world.countNear(c);
		std::printf("[three] near %d  far %d  third %d  elsewhere %d\n", na, nb, nc, options.warriors - na - nb - nc);
		GLOB2_CHECK(na + nb + nc >= options.warriors * 9 / 10, "nearly every warrior reaches one of the three areas");
		GLOB2_CHECK(na >= options.warriors / 8 && nb >= options.warriors / 8 && nc >= options.warriors / 8, "every area gets at least an eighth");
	}

	// Erasing an area sends its warriors to the remaining one: the erased tiles
	// are no longer painted, so their guards are ordinary free warriors again.
	void erasedAreaReleasesItsWarriors(const Options& options)
	{
		World world(options);
		world.paint(NEAR);
		world.paint(FAR);
		world.spawnWarriors(SPAWN_X, SPAWN_Y, options.warriors);
		world.run(options.ticks);
		const int beforeFar = world.countNear(FAR);
		world.paint(FAR, BrushTool::MODE_DEL);
		world.run(options.ticks);
		const int na = world.countNear(NEAR), nb = world.countNear(FAR);
		std::printf("[erase] far held %d before erasing; after: near %d  far %d  elsewhere %d\n", beforeFar, na, nb, options.warriors - na - nb);
		GLOB2_CHECK(beforeFar >= options.warriors / 4, "the far area was guarded before it was erased");
		GLOB2_CHECK(nb == 0, "nobody stays on the erased area");
		GLOB2_CHECK(na >= options.warriors * 9 / 10, "the remaining area takes nearly everyone");
	}

	// Warriors that have settled on an area keep moving. A warrior with no free
	// painted neighbour takes an ordinary random step rather than standing with
	// no direction, which both renderers draw as a unit spinning in place.
	// Every layout still keeps its warriors around the area.
	//
	// A warrior boxed in by other units on all eight sides still has no step to
	// take, as it always had. Only a 1x1 area sees that often: the whole crowd
	// presses in around its one tile, so it gets a looser bound. Before this
	// fallback the 1x1 and sparse layouts sat at 100%, checker at about 80% and a
	// packed 5x5 at about 73%, with every guard on a sparse area never moving.
	void settledGuardsDoNotSpin(const Options& options)
	{
		struct Layout { const char* name; int half; int warriors; bool (*keep)(int, int); double maxNoDirection; };
		const Layout layouts[] = {
			{"1x1", 0, options.warriors, nullptr, 50.0},
			{"sparse 9x9", 4, options.warriors, [](int dx, int dy) { return dx % 2 == 0 && dy % 2 == 0; }, 5.0},
			{"checker 7x7", 3, options.warriors, [](int dx, int dy) { return (dx + dy) % 2 == 0; }, 5.0},
			{"solid 5x5", 2, options.warriors, nullptr, 5.0},
			{"solid 5x5, few", 2, options.warriors / 3, nullptr, 5.0},
			{"solid 9x9", 4, options.warriors, nullptr, 5.0},
		};
		std::printf("[spins] %-15s  on-paint ticks  no direction  moves/warrior  held still  within %d tiles\n", "paint", NEAR.countRadius);
		for (const Layout& layout : layouts)
		{
			World world(options);
			const Zone zone{layout.name, NEAR.x, NEAR.y, layout.half, NEAR.countRadius};
			world.paint(zone, BrushTool::MODE_ADD, layout.keep);
			world.spawnWarriors(SPAWN_X, SPAWN_Y, layout.warriors);
			world.run(options.ticks);
			long onPaint = 0, noDirection = 0;
			int moved = 0;
			// Tile each warrior stood on at the first sample while on paint, or -1
			// once it has left that tile or the paint.
			std::vector<int> stillOn(Unit::MAX_COUNT, -2);
			for (int t = 0; t < 500; ++t)
			{
				int stepMoves = 0;
				world.run(1, &stepMoves);
				moved += stepMoves;
				for (int i = 0; i < Unit::MAX_COUNT; ++i)
				{
					Unit* u = world.team->myUnits[i];
					if (!u || u->isDead || u->typeNum != WARRIOR)
						continue;
					const int tile = (int)world.game.map.coordToIndex(u->posX, u->posY);
					if (!world.game.map.isGuardArea(u->posX, u->posY, world.team->me))
					{
						stillOn[i] = -1;
						continue;
					}
					if (stillOn[i] == -2)
						stillOn[i] = t == 0 ? tile : -1;
					else if (stillOn[i] != tile)
						stillOn[i] = -1;
					++onPaint;
					noDirection += u->direction == UNIT_DIRECTION_NONE;
				}
			}
			const int held = world.countNear(zone);
			const int heldStill = (int)std::count_if(stillOn.begin(), stillOn.end(), [](int tile) { return tile >= 0; });
			const double share = onPaint ? 100.0 * noDirection / onPaint : 0.0;
			std::printf("[spins] %-15s  %14ld  %11.1f%%  %13.1f  %10d  %d of %d\n", layout.name, onPaint, share, (double)moved / layout.warriors, heldStill, held, layout.warriors);
			GLOB2_CHECK(onPaint > 0, std::string(layout.name) + ": some warrior stands on the paint");
			GLOB2_CHECK(share < layout.maxNoDirection, std::string(layout.name) + ": warriors on the paint are not left with no direction");
			GLOB2_CHECK(heldStill == 0, std::string(layout.name) + ": no warrior stands on one painted tile for the whole sample");
			GLOB2_CHECK(held >= layout.warriors * 3 / 4, std::string(layout.name) + ": the area keeps most of its warriors around it");
		}
	}

	// A game saved mid-balancing continues identically after loading: the guard
	// field and its refresh flag are saved with the live RNG, and the crowding
	// counts are recomputed from unit positions at the next rebuild, so no
	// derived state is lost. The experiment travels in the header, so the loaded
	// game balances too.
	void savedGameContinuesIdentically(const Options& options)
	{
		World world(options);
		world.paint(NEAR);
		world.paint(FAR);
		world.spawnWarriors(SPAWN_X, SPAWN_Y, options.warriors);
		world.run(1500);

		auto* backend = new MemoryStreamBackend();
		BinaryOutputStream output(backend);
		world.headless.gui.save(&output, "guard continuation");
		const std::string bytes(backend->getBuffer(), backend->getPosition());

		std::vector<std::vector<Uint32>> continuation;
		for (int i = 0; i < 1000; ++i)
		{
			world.run(1);
			continuation.push_back(simulationState(world.game));
		}

		GameGUI restored(false);
		auto* source = new MemoryStreamBackend(bytes.data(), bytes.size());
		source->seekFromStart(0);
		BinaryInputStream input(source);
		REQUIRE(restored.load(&input));
		REQUIRE(restored.game.gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing));
		// The saved-game loader replaces the player header after loading.
		restored.game.setGameHeader(world.game.gameHeader, true);
		Team* restoredTeam = restored.game.teams[0];
		for (int i = 0; i < 1000; ++i)
		{
			stepGame(restored.game, restoredTeam, 1);
			const auto actual = simulationState(restored.game);
			if (actual != continuation[i])
			{
				std::fprintf(stderr, "continuation mismatch at step %d after loading (sizes %zu/%zu)\n", i, actual.size(), continuation[i].size());
				REQUIRE_MESSAGE(false, "the loaded game continues identically");
			}
		}
		std::printf("[saveload] near %d  far %d after 2500 ticks, 1000 of them after a load\n",
			world.countNear(NEAR), world.countNear(FAR));
	}

	// The separable box sum must equal a brute-force count, including across the
	// torus seam.
	void crowdingMatchesBruteForce(const Options& options)
	{
		World world(options);
		Map& map = world.game.map;
		world.spawnWarriors(1, 1, 5);      // straddles the seam
		world.spawnWarriors(62, 40, 4);
		world.spawnWarriors(30, 30, options.warriors);
		std::vector<Uint16> crowd(map.getW() * map.getH());
		REQUIRE(map.computeWarriorCrowding(0, crowd.data()));
		int mismatches = 0;
		for (int y = 0; y < map.getH(); ++y)
			for (int x = 0; x < map.getW(); ++x)
			{
				int expected = 0;
				for (int i = 0; i < Unit::MAX_COUNT; ++i)
					if (Unit* u = world.team->myUnits[i])
					{
						int ddx = std::abs(((u->posX - x) & map.wMask));
						int ddy = std::abs(((u->posY - y) & map.hMask));
						ddx = std::min(ddx, map.getW() - ddx);
						ddy = std::min(ddy, map.getH() - ddy);
						expected += std::max(ddx, ddy) <= GUARD_CROWD_RADIUS;
					}
				mismatches += crowd[map.coordToIndex(x, y)] != expected;
			}
		CHECK(mismatches == 0);
		// A team without warriors reports so and leaves the buffer alone.
		World empty(options);
		std::vector<Uint16> untouched(map.getW() * map.getH(), 7);
		CHECK(!empty.game.map.computeWarriorCrowding(0, untouched.data()));
		CHECK(untouched[0] == 7);
	}

	template <typename F>
	void timeIt(const char* label, int w, int warriors, F&& f)
	{
		const int repeats = 200;
		std::vector<double> samples;
		for (int i = 0; i < repeats; ++i)
		{
			const auto start = std::chrono::steady_clock::now();
			f();
			const auto end = std::chrono::steady_clock::now();
			samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
		}
		std::sort(samples.begin(), samples.end());
		double mean = 0;
		for (double s : samples) mean += s;
		mean /= repeats;
		std::printf("[timing] %dx%d map, %d warriors: %s median %.1f us, mean %.1f us, p90 %.1f us\n",
			w, w, warriors, label, samples[repeats / 2], mean, samples[(repeats * 9) / 10]);
	}

	// Wall time of one guard-gradient rebuild, with warriors on the map, for the
	// scenario map and for a large map; the crowding pass alone; and the rebuild
	// with no warriors, which is the legacy seeding. Timing evidence, not pass/fail.
	void rebuildTiming(const Options& options)
	{
		for (int shift : {6, 8})
		{
			const int w = 1 << shift;
			const Zone a{"a", w / 2, w / 4, 2, 7};
			const Zone b{"b", w / 4, (3 * w) / 4, 2, 7};
			{
				World world(options, shift);
				world.paint(a);
				world.paint(b);
				world.game.map.getGuardAreasGradient(0, 0);
				timeIt("rebuild without warriors", w, 0, [&] { world.game.map.updateGuardAreasGradient(0, 0); });
			}
			World world(options, shift);
			world.paint(a);
			world.paint(b);
			world.spawnWarriors(w / 4, w / 4, options.warriors);
			world.game.map.getGuardAreasGradient(0, 0);
			std::vector<Uint16> crowd(w * w);
			timeIt("crowding pass", w, options.warriors, [&] { world.game.map.computeWarriorCrowding(0, crowd.data()); });
			timeIt("guard gradient rebuild", w, options.warriors, [&] { world.game.map.updateGuardAreasGradient(0, 0); });
		}
	}
}

TEST_SUITE("GuardAreaBalance")
{
	TEST_CASE("without the experiment every warrior takes the nearest area and nobody leaves it")
	{
		glob2test::HeadlessGlobals globals;
		Options options;
		options.experiment = false;
		spawnSplitsBetweenAreas(options);
		clumpDrainsIntoNewArea(options);
	}
	TEST_CASE("crowding box sum matches brute force across the torus seam")
	{
		glob2test::HeadlessGlobals globals;
		crowdingMatchesBruteForce(Options());
	}
	TEST_CASE("warriors split between a near and a far guard area at every refresh cadence")
	{
		glob2test::HeadlessGlobals globals;
		for (int which = 0; which < 3; ++which)
		{
			std::printf("[spawn] guard field refreshed %s\n", cadenceName(which));
			spawnSplitsBetweenAreas(cadence(which));
		}
	}
	TEST_CASE("an existing clump drains into a newly painted area at every refresh cadence")
	{
		glob2test::HeadlessGlobals globals;
		for (int which = 0; which < 3; ++which)
		{
			std::printf("[drain] guard field refreshed %s\n", cadenceName(which));
			clumpDrainsIntoNewArea(cadence(which));
		}
	}
	TEST_CASE("two nearby patches count as one position")
	{
		glob2test::HeadlessGlobals globals;
		nearbyPatchesShareWarriors(Options());
	}
	TEST_CASE("a larger painted area takes the larger share")
	{
		glob2test::HeadlessGlobals globals;
		largerAreaTakesMore(Options());
	}
	TEST_CASE("three areas are all guarded")
	{
		glob2test::HeadlessGlobals globals;
		threeAreasAllGuarded(Options());
	}
	TEST_CASE("erasing an area releases its warriors to the other one")
	{
		glob2test::HeadlessGlobals globals;
		erasedAreaReleasesItsWarriors(Options());
	}
	TEST_CASE("settled guards keep moving on every paint layout and stay around the area")
	{
		glob2test::HeadlessGlobals globals;
		settledGuardsDoNotSpin(Options());
	}
	TEST_CASE("a saved game continues identically for 1000 ticks after loading [save-format]")
	{
		glob2test::HeadlessGlobals globals;
		savedGameContinuesIdentically(Options());
	}
	TEST_CASE("guard gradient rebuild timing [benchmark]")
	{
		glob2test::HeadlessGlobals globals;
		rebuildTiming(Options());
	}
}
