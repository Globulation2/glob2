// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine measurement and regression for guard-area balancing.
//
// Free warriors used to walk to the nearest painted guard area whatever was
// already there, so how many warriors a guard area ended up with was anyone's
// guess: one area got the whole batch and the others got nobody. Since the
// crowding-seeded guard gradient (docs/guard-area-balancing/README.md) they
// spread between areas in proportion to painted size. This harness runs the
// real simulation on a blank map and measures that, so a change in tuning or
// in the movement code shows up as numbers rather than as a feeling.
//
//   GuardAreaBalanceHarness [check|report] [options]
//
//   check (default)        assert the balancing behaviour; exit 1 on failure
//   report                 print the same tables without asserting, so the
//                          harness built against an older engine gives the
//                          before-numbers
//   --scenario NAME        run one scenario (see the list in main)
//   --ticks N              simulation length of the behaviour scenarios (4000)
//   --warriors N           warriors spawned (24)
//   --resource-gradients   allocate the resource fields a small game has, so
//                          the gradient round-robin refreshes the guard field
//                          about every 10 ticks instead of every tick
//   --slow-cadence         allocate what a large game has: about every 170 ticks
//   --screenshots DIR      render the "spawn" and "drain" stories to PNG files
//                          in DIR instead of running the headless scenarios
//                          (needs a display; CI uses xvfb)
//
// Every scenario uses one team on a 64x64 grass map. Counts are warriors within
// a few tiles of an area's centre, so a warrior milling around a full area still
// counts as guarding it. Warriors are kept fed so hunger never takes them away.
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Team.h"
#include "IntBuildingType.h"
#include "MapInternal.h"
#include "Race.h"
#include "Ressource.h"
#include "Player.h"
#include "BasePlayer.h"
#include "Brush.h"
#include "Order.h"
#include "Utilities.h"
#include "GraphicContext.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <SDL_image.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace GAGCore;

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Options
{
	bool check = true;
	int ticks = 4000;
	int warriors = 24;
	bool resourceGradients = false;
	bool slowCadence = false;
	std::string screenshots;
};

//! A painted square and how it is counted.
struct Zone
{
	const char* name;
	int x, y;        //!< centre
	int half;        //!< painted box is (2*half+1) square
	int countRadius; //!< warriors within this many tiles count as "at" the zone
};

//! Simulation checksum of everything that matters, for continuation checks.
static std::vector<Uint32> simulationState(Game& game)
{
	std::vector<Uint32> result, buildings, units;
	game.checkSum(&result, &buildings, &units, true);
	result.erase(result.begin()); // the save path upgrades the map format header
	result.insert(result.end(), buildings.begin(), buildings.end());
	result.insert(result.end(), units.begin(), units.end());
	return result;
}

//! Steps a game, keeping every warrior fed so the scenarios stay about
//! guarding rather than eating. Returns the mean wall time of a step in
//! microseconds; movedTiles, if given, accumulates the number of tile changes.
static double stepGame(Game& game, Team* team, int ticks, int* movedTiles = nullptr)
{
	std::vector<int> before(Unit::MAX_COUNT, -1);
	int moved = 0;
	auto start = std::chrono::steady_clock::now();
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
	auto end = std::chrono::steady_clock::now();
	if (movedTiles)
		*movedTiles = moved;
	return std::chrono::duration<double, std::micro>(end - start).count() / std::max(1, ticks);
}

struct World
{
	GameGUI gui;
	Game& game = gui.game;
	Team* team = nullptr;

	explicit World(const Options& options, int sizeShift = 6)
	{
		game.map.setSize(sizeShift, sizeShift, GRASS);
		game.map.setGame(&game);
		game.addTeam(0);
		team = game.teams[0];
		// One local player owns the team, installed the way the game loader
		// does it, so area orders apply to it, the painted tiles show in the
		// displayed view the renderer reads, and a save of this world reloads
		// to the same checksums.
		GameHeader header;
		header.setNumberOfPlayers(1);
		header.getBasePlayer(0) = BasePlayer(0, "harness", 0, BasePlayer::P_LOCAL);
		game.setGameHeader(header, true);
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

	//! Paint or erase a zone with the same order a player's brush sends.
	void paint(const Zone& zone, BrushTool::Mode mode = BrushTool::MODE_ADD)
	{
		BrushAccumulator acc;
		for (int dy = -zone.half; dy <= zone.half; ++dy)
			for (int dx = -zone.half; dx <= zone.half; ++dx)
				acc.applyBrush(BrushApplication((zone.x + dx) & game.map.wMask, (zone.y + dy) & game.map.hMask, 0), &game.map);
		std::shared_ptr<Order> order(new OrderAlterGuardArea(0, mode, &acc, &game.map));
		order->sender = 0;
		game.executeOrder(order, 0);
		require(game.map.isGuardArea(zone.x, zone.y, team->me) == (mode == BrushTool::MODE_ADD), "paint applied");
	}

	//! Spawn level-0 warriors in a compact block around (x, y), the way a
	//! barracks batch stands together.
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
		require((int)units.size() == count, "all warriors spawned");
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

static void printHeader(const char* scenario, const Zone& a, const Zone& b)
{
	std::printf("[%s] tick  %8s  %8s  elsewhere  free\n", scenario, a.name, b.name);
}

static void printRow(World& world, const char* scenario, int tick, const Zone& a, const Zone& b, int total)
{
	int na = world.countNear(a), nb = world.countNear(b);
	std::printf("[%s] %5d  %8d  %8d  %9d  %4d\n", scenario, tick, na, nb, total - na - nb, world.countFree());
}

// The two areas every scenario below shares: "near" is 18 tiles east of the
// spawn block, "far" 32 tiles south of it.
static const Zone NEAR{"near", 30, 12, 2, 7};
static const Zone FAR{"far", 12, 44, 2, 7};
static const int SPAWN_X = 12, SPAWN_Y = 12;

// Warriors spawn near the base and two areas are painted at once, the second
// one farther away. Legacy: everyone walks to the near one.
static void spawnSplitsBetweenAreas(const Options& options)
{
	World world(options);
	world.paint(NEAR);
	world.paint(FAR);
	world.spawnWarriors(SPAWN_X, SPAWN_Y, options.warriors);
	require(world.game.integrity(), "spawn scenario setup is consistent");

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
	require(world.game.integrity(), "spawn scenario integrity after the run");

	if (options.check)
	{
		int na = world.countNear(NEAR), nb = world.countNear(FAR);
		require(na + nb >= options.warriors * 9 / 10, "nearly every warrior reaches an area");
		require(nb >= options.warriors / 4, "at least a quarter of the warriors take the far area");
		require(na >= options.warriors / 4, "at least a quarter of the warriors take the near area");
		require((double)moved / options.warriors < 40.0, "warriors settle instead of shuttling between the areas");
		std::puts("PASS warriors split between a near and a far guard area");
	}
}

// Warriors already stand in a full area when a second one is painted; some
// must migrate, and the first area must not empty out and refill (the herd
// effect). Legacy: nobody ever leaves an area.
static void clumpDrainsIntoNewArea(const Options& options)
{
	World world(options);
	world.paint(NEAR);
	world.spawnWarriors(NEAR.x, NEAR.y, options.warriors);
	world.run(300);
	require(world.countNear(NEAR) == options.warriors, "the clump starts inside the first area");
	world.paint(FAR);
	require(world.game.integrity(), "drain scenario setup is consistent");

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
	require(world.game.integrity(), "drain scenario integrity after the run");

	if (options.check)
	{
		require(world.countNear(FAR) >= options.warriors / 4, "at least a quarter of the clump migrates to the new area");
		require(world.countNear(NEAR) >= options.warriors / 4, "the first area keeps at least a quarter");
		require(lowestNear >= world.countNear(NEAR) - 2, "the first area drains to its final count without overshooting");
		std::puts("PASS an existing clump drains into a newly painted area");
	}
}

// Two unconnected patches three tiles apart count as one position of their
// combined painted size (18 tiles): they must neither pull the whole team nor
// be starved next to a 25-tile area.
static void nearbyPatchesShareWarriors(const Options& options)
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
	int nfar = world.countNear(FAR);
	std::printf("[patches] near pair %d  far %d  elsewhere %d\n", nab, nfar, options.warriors - nab - nfar);
	if (options.check)
	{
		require(nfar >= options.warriors / 4, "the far area still gets its share next to a pair of patches");
		require(nab >= options.warriors / 4, "the pair of patches gets its share");
		std::puts("PASS two nearby patches count as one position");
	}
}

// A larger painted area takes more warriors, even when it is the farther one.
// Legacy: the nearer area takes everyone whatever the sizes.
static void largerAreaTakesMore(const Options& options)
{
	World world(options);
	const Zone small{"small", 30, 12, 2, 7};  // 5x5, 18 tiles away
	const Zone large{"large", 12, 44, 4, 9};  // 9x9, 32 tiles away
	world.paint(small);
	world.paint(large);
	world.spawnWarriors(SPAWN_X, SPAWN_Y, options.warriors);
	world.run(options.ticks);
	int ns = world.countNear(small), nl = world.countNear(large);
	std::printf("[size] small 5x5 %d  large 9x9 %d  elsewhere %d\n", ns, nl, options.warriors - ns - nl);
	if (options.check)
	{
		require(nl > ns, "the larger area holds more warriors than the smaller nearer one");
		require(ns >= options.warriors / 8, "the smaller area is not abandoned");
		std::puts("PASS a larger painted area takes the larger share");
	}
}

// Three areas at increasing distance all get guarded.
static void threeAreasAllGuarded(const Options& options)
{
	World world(options);
	const Zone c{"third", 44, 44, 2, 7}; // about 45 tiles from the spawn block
	world.paint(NEAR);
	world.paint(FAR);
	world.paint(c);
	world.spawnWarriors(SPAWN_X, SPAWN_Y, options.warriors);
	world.run(options.ticks);
	int na = world.countNear(NEAR), nb = world.countNear(FAR), nc = world.countNear(c);
	std::printf("[three] near %d  far %d  third %d  elsewhere %d\n", na, nb, nc, options.warriors - na - nb - nc);
	if (options.check)
	{
		require(na + nb + nc >= options.warriors * 9 / 10, "nearly every warrior reaches one of the three areas");
		require(na >= options.warriors / 8 && nb >= options.warriors / 8 && nc >= options.warriors / 8, "every area gets at least an eighth");
		std::puts("PASS three areas are all guarded");
	}
}

// Erasing an area sends its warriors to the remaining one: the erased tiles
// are no longer painted, so their guards are ordinary free warriors again.
static void erasedAreaReleasesItsWarriors(const Options& options)
{
	World world(options);
	world.paint(NEAR);
	world.paint(FAR);
	world.spawnWarriors(SPAWN_X, SPAWN_Y, options.warriors);
	world.run(options.ticks);
	int beforeFar = world.countNear(FAR);
	world.paint(FAR, BrushTool::MODE_DEL);
	world.run(options.ticks);
	int na = world.countNear(NEAR), nb = world.countNear(FAR);
	std::printf("[erase] far held %d before erasing; after: near %d  far %d  elsewhere %d\n", beforeFar, na, nb, options.warriors - na - nb);
	if (options.check)
	{
		require(beforeFar >= options.warriors / 4, "the far area was guarded before it was erased");
		require(nb == 0, "nobody stays on the erased area");
		require(na >= options.warriors * 9 / 10, "the remaining area takes nearly everyone");
		std::puts("PASS erasing an area releases its warriors to the other one");
	}
}

// A game saved mid-balancing continues identically after loading: the guard
// field and its refresh flag are saved, and the crowding counts are recomputed
// from unit positions at the next rebuild, so no derived state is lost.
static void savedGameContinuesIdentically(const Options& options)
{
	World world(options);
	world.paint(NEAR);
	world.paint(FAR);
	world.spawnWarriors(SPAWN_X, SPAWN_Y, options.warriors);
	world.run(1500);

	auto* backend = new MemoryStreamBackend();
	BinaryOutputStream output(backend);
	world.gui.save(&output, "guard continuation");
	const std::string bytes(backend->getBuffer(), backend->getPosition());
	const auto savedRandom = randomGenerator;

	std::vector<std::vector<Uint32>> continuation;
	for (int i = 0; i < 1000; ++i)
	{
		world.run(1);
		continuation.push_back(simulationState(world.game));
	}

	GameGUI restored;
	auto* source = new MemoryStreamBackend(bytes.data(), bytes.size());
	source->seekFromStart(0);
	BinaryInputStream input(source);
	require(restored.load(&input), "the saved game loads");
	// The saved-game loader replaces the player header after loading.
	restored.game.setGameHeader(world.game.gameHeader, true);
	randomGenerator = savedRandom;
	Team* restoredTeam = restored.game.teams[0];
	for (int i = 0; i < 1000; ++i)
	{
		stepGame(restored.game, restoredTeam, 1);
		const auto actual = simulationState(restored.game);
		if (actual != continuation[i])
		{
			std::fprintf(stderr, "continuation mismatch at step %d after loading (sizes %zu/%zu)\n", i, actual.size(), continuation[i].size());
			require(false, "the loaded game continues identically");
		}
	}
	std::printf("[saveload] near %d  far %d after 2500 ticks, 1000 of them after a load\n",
		world.countNear(NEAR), world.countNear(FAR));
	std::puts("PASS a saved game continues identically for 1000 ticks after loading");
}

// The separable box sum must equal a brute-force count, including across the
// torus seam.
static void crowdingMatchesBruteForce(const Options& options)
{
	World world(options);
	Map& map = world.game.map;
	world.spawnWarriors(1, 1, 5);      // straddles the seam
	world.spawnWarriors(62, 40, 4);
	world.spawnWarriors(30, 30, options.warriors);
	std::vector<Uint16> crowd(map.getW() * map.getH());
	map.computeWarriorCrowding(0, crowd.data());
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
			require(crowd[map.coordToIndex(x, y)] == expected, "crowding equals the brute-force count on every tile");
		}
	std::puts("PASS crowding box sum matches brute force across the torus seam");
}

template <typename F>
static void timeIt(const char* label, int w, int warriors, F&& f)
{
	const int repeats = 200;
	std::vector<double> samples;
	for (int i = 0; i < repeats; ++i)
	{
		auto start = std::chrono::steady_clock::now();
		f();
		auto end = std::chrono::steady_clock::now();
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
// with no warriors, which is the legacy seeding. Run this alone on a quiet
// machine: a second process on the same cores inflates the numbers.
static void rebuildTiming(const Options& options)
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

// Screenshots: the same stories rendered by the real map renderer, for the
// pull request and the documentation. The view is the map's top-left 32x24
// tiles at 1024x768, so the scenario is laid out inside that window: spawn
// block at (4,4), a 5x5 area 10 tiles east and another 16 tiles south.
namespace screenshots
{
	const int SCREEN_W = 1024, SCREEN_H = 768;
	const Zone NEAR_SHOT{"near", 14, 4, 2, 7};
	const Zone FAR_SHOT{"far", 4, 20, 2, 7};

	void save(const std::string& dir, const std::string& name)
	{
		Sprite::flushBatches(globalContainer->gfx);
		glFinish();
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		const int w = viewport[2], h = viewport[3];
		std::vector<unsigned char> data(w * h * 4), flipped(w * h * 4);
		glReadPixels(viewport[0], viewport[1], w, h, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
		require(glGetError() == GL_NO_ERROR, "read back the framebuffer");
		for (int y = 0; y < h; ++y)
			std::copy_n(data.data() + y * w * 4, w * 4, flipped.data() + (h - 1 - y) * w * 4);
		auto* surface = SDL_CreateRGBSurfaceWithFormatFrom(flipped.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
		require(surface != nullptr, "wrap the framebuffer for PNG output");
		require(IMG_SavePNG(surface, (dir + "/" + name + ".png").c_str()) == 0, "write the PNG");
		SDL_FreeSurface(surface);
		std::printf("[screenshot] %s/%s.png\n", dir.c_str(), name.c_str());
	}

	void render(World& world, Game::ViewState& view, const std::string& dir, const std::string& name)
	{
		auto* gfx = globalContainer->gfx;
		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), 0, 0, 0);
		world.game.drawMap(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 0, 0, view,
			Game::DRAW_AREA | Game::DRAW_WHOLE_MAP | Game::DRAW_NO_CLOUD_LAYER);
		save(dir, name);
	}

	void run(const Options& options)
	{
		std::filesystem::create_directories(options.screenshots);
		Game::ViewState view;
		{
			// spawn: both areas painted, the batch walks out of the base
			World world(options);
			world.paint(NEAR_SHOT);
			world.paint(FAR_SHOT);
			world.spawnWarriors(4, 4, options.warriors);
			int tick = 0;
			for (int next : {0, 750, 1500, 4500})
			{
				world.run(next - tick);
				tick = next;
				render(world, view, options.screenshots, "spawn-tick" + std::to_string(tick));
				std::printf("[screenshot] spawn tick %d: near %d far %d\n", tick, world.countNear(NEAR_SHOT), world.countNear(FAR_SHOT));
			}
		}
		{
			// drain: the batch already fills the near area when the far one is painted
			World world(options);
			world.paint(NEAR_SHOT);
			world.spawnWarriors(NEAR_SHOT.x, NEAR_SHOT.y, options.warriors);
			world.run(300);
			world.paint(FAR_SHOT);
			int tick = 0;
			for (int next : {0, 1000, 2000, 4000})
			{
				world.run(next - tick);
				tick = next;
				render(world, view, options.screenshots, "drain-tick" + std::to_string(tick));
				std::printf("[screenshot] drain tick %d: near %d far %d\n", tick, world.countNear(NEAR_SHOT), world.countNear(FAR_SHOT));
			}
		}
	}
}

int main(int argc, char** argv)
{
	Options options;
	std::string scenario = "all";
	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "report") options.check = false;
		else if (arg == "check") options.check = true;
		else if (arg == "--ticks" && i + 1 < argc) options.ticks = std::atoi(argv[++i]);
		else if (arg == "--warriors" && i + 1 < argc) options.warriors = std::atoi(argv[++i]);
		else if (arg == "--resource-gradients") options.resourceGradients = true;
		else if (arg == "--slow-cadence") options.slowCadence = true;
		else if (arg == "--scenario" && i + 1 < argc) scenario = argv[++i];
		else if (arg == "--screenshots" && i + 1 < argc) options.screenshots = argv[++i];
		else require(false, "usage: GuardAreaBalanceHarness [check|report] [--ticks N] [--warriors N] [--resource-gradients] [--slow-cadence] [--scenario crowding|timing|spawn|drain|patches|size|three|erase|saveload] [--screenshots DIR]");
	}
	if (!options.screenshots.empty())
	{
		GlobalContainer globals("glob2-guard-area-balance-test");
		globalContainer = &globals;
		globals.settings.screenWidth = screenshots::SCREEN_W;
		globals.settings.screenHeight = screenshots::SCREEN_H;
		globals.settings.screenFlags = GraphicContext::USEGPU;
		globals.settings.rememberUnit = false;
		globals.settings.mute = 1;
		globals.load();
		IntBuildingType::init();
		screenshots::run(options);
		return 0;
	}
	GlobalContainer globals;
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	auto wanted = [&](const char* name) { return scenario == "all" || scenario == name; };
	if (wanted("crowding")) crowdingMatchesBruteForce(options);
	if (wanted("timing")) rebuildTiming(options);
	if (wanted("spawn")) spawnSplitsBetweenAreas(options);
	if (wanted("drain")) clumpDrainsIntoNewArea(options);
	if (wanted("patches")) nearbyPatchesShareWarriors(options);
	if (wanted("size")) largerAreaTakesMore(options);
	if (wanted("three")) threeAreasAllGuarded(options);
	if (wanted("erase")) erasedAreaReleasesItsWarriors(options);
	if (wanted("saveload")) savedGameContinuesIdentically(options);
	if (options.check)
		std::puts("Guard area balance regressions passed");
	return 0;
}
