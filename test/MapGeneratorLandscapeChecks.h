// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Contracts of the landscape toolkit (symmetry groups, mask arithmetic, crop growth on a sketch, cost
// models, scattered sites and their graphs, patterns, wandering paths, channels, building room, homes
// in any region and biome kits), each on a small map built by hand. Part of the toolkit checks.
#include "Biomes.h"
#include "Channels.h"
#include "Contact.h"
#include "Drawing.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GraphMaze.h"
#include "Grid.h"
#include "Growth.h"
#include "Homes.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Patterns.h"
#include "Pipeline.h"
#include "Points.h"
#include "Roads.h"
#include "Room.h"
#include "Sketch.h"
#include "Walls.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <random>
#include <set>
#include <vector>

namespace LandscapeChecks
{
using namespace MapGeneration;

inline int count(const std::vector<unsigned char> &mask)
{
	return int(std::count_if(mask.begin(), mask.end(), [](unsigned char v) { return v != 0; }));
}

inline void landscapeGrass(Game &game, int wDec, int hDec)
{
	game.map.setSize(wDec, hDec);
	game.map.setGame(&game);
	game.map.makeHomogenMap(GRASS);
}

// Orbits: the point symmetries are the arena's; translation groups exist only for orders dividing the
// tile count, pick the roomiest lattice, and carry orbits onto themselves; stamping and summing are
// orbit invariant; topShare keeps ties; equaliseDeposits and orbitMismatch see a broken deposit.
inline void orbitChecks()
{
	assert(pointSymmetry(64, 64, 4).order() == 4 && pointSymmetry(64, 32, 8).order() == 0);
	long long spacing2 = 0;
	const Symmetry lattice = translationSymmetry(64, 64, 4, &spacing2);
	assert(lattice.order() == 4 && spacing2 == 32 * 32);
	assert(translationSymmetry(64, 64, 3).order() == 0 &&
		   translationSymmetry(64, 64, 1).order() == 0);
	const Symmetry wide = translationSymmetry(64, 32, 8, &spacing2);
	assert(wide.order() == 8 && spacing2 >= 16 * 16);
	std::set<std::pair<int, int>> moves;
	for (const Isometry &g : wide.elements)
		assert(g.a == 1 && g.d == 1 && moves.insert({g.tx, g.ty}).second);
	const Symmetry sheared = translationSymmetry(64, 64, 2);
	assert(sheared.order() == 2);
	for (const Symmetry &s : {lattice, wide, pointSymmetry(64, 64, 8)})
		for (int y = 0; y < s.height; y += 3)
			for (int x = 0; x < s.width; x += 5)
				for (int e = 0; e < s.order(); ++e)
				{
					const int image = s.tile(e, x, y);
					assert(s.orbitKey(image % s.width, image / s.width) == s.orbitKey(x, y));
				}

	const LatticeSites three = latticeSites(64, 64, 3, 5, 7);
	assert(!three.exact && three.sites.size() == 3);
	const LatticeSites four = latticeSites(64, 64, 4, 5, 7);
	assert(four.exact && four.sites.size() == 4 && four.sites[0].x == 5 && four.sites[0].y == 7);

	std::vector<unsigned char> feature(size_t(64) * 64, 0);
	feature[size_t(7) * 64 + 3] = 1;
	assert(count(stampOrbits(lattice, feature, false)) == 4);
	std::vector<int> raw(size_t(64) * 64);
	for (size_t i = 0; i < raw.size(); ++i)
		raw[i] = int(i * 7919 % 1000);
	const std::vector<int> summed = orbitSum(lattice, raw, false);
	for (int e = 0; e < lattice.order(); ++e)
		assert(summed[size_t(lattice.tile(e, 3, 7))] == summed[size_t(7) * 64 + 3]);
	std::vector<unsigned char> all(raw.size(), 1);
	const std::vector<unsigned char> top = topShare(summed, all, 0.25, true);
	for (size_t i = 0; i < raw.size(); ++i)
		assert(top[i] == top[size_t(lattice.orbitKey(int(i % 64), int(i / 64)))]);

	Game game(nullptr);
	landscapeGrass(game, 6, 6);
	for (int e = 0; e < lattice.order(); ++e)
	{
		const int i = lattice.tile(e, 10, 12);
		game.map.setResource(i % 64, i / 64, WOOD, 1);
	}
	std::string detail;
	assert(equaliseDeposits(game.map, lattice, detail) && detail.empty());
	game.map.setResource(40, 40, WHEAT, 1);
	assert(!equaliseDeposits(game.map, lattice, detail) && !detail.empty());
	game.map.setNoResource(40, 40, 1);
	game.map.setResource(40, 40, STONE, 1);
	assert(orbitMismatch(game, lattice, 4).find("Deposit") == 0);
}

// Morphology: dilate and erode undo each other on a square across the seam; the distance transform
// matches brute force; clearance measures a corridor; the widest walk finds a pinch; small regions go.
inline void morphologyChecks()
{
	const Torus t(32, 24);
	std::vector<unsigned char> dot(t.size(), 0);
	dot[t.at(0, 0)] = 1;
	const std::vector<unsigned char> square = dilate(t, dot, 2);
	assert(count(square) == 25 && square[t.at(30, 22)] && !square[t.at(3, 0)]);
	assert(erode(t, square, 2) == dot && openMask(t, square, 2) == square);
	assert(count(dilateRound(t, dot, 3)) == 29);
	std::mt19937 rng(9);
	std::vector<unsigned char> mask(t.size(), 0);
	for (int k = 0; k < 12; ++k)
		mask[rng() % t.size()] = 1;
	const std::vector<std::int64_t> d2 = distanceSquaredTo(t, mask);
	for (int i = 0; i < t.size(); ++i)
	{
		std::int64_t best = -1;
		for (int j = 0; j < t.size(); ++j)
			if (mask[j])
			{
				const std::int64_t d = t.dist2(i % t.w, i / t.w, j % t.w, j / t.w);
				best = best < 0 ? d : std::min(best, d);
			}
		assert(d2[i] == best);
	}
	// A corridor five tiles across along the width, pinched to one tile at x = 16.
	std::vector<unsigned char> corridor(t.size(), 0);
	for (int x = 0; x < t.w; ++x)
		for (int y = 10; y < 15; ++y)
			corridor[t.at(x, y)] = x != 16 || y == 12;
	assert(clearance(t, corridor)[t.at(5, 12)] == 3 && clearance(t, corridor)[t.at(5, 10)] == 1);
	std::vector<unsigned char> goal(t.size(), 0);
	goal[t.at(20, 12)] = 1;
	assert(narrowestPassage(t, corridor, {t.at(12, 12)}, goal) == 5); // the long way round
	std::vector<unsigned char> blocked = corridor;
	for (int y = 10; y < 15; ++y)
		blocked[t.at(26, y)] = y == 12;
	assert(narrowestPassage(t, blocked, {t.at(12, 12)}, goal) == 1);
	std::vector<unsigned char> specks = dot;
	for (int y = 4; y < 9; ++y)
		for (int x = 4; x < 9; ++x)
			specks[t.at(x, y)] = 1;
	assert(count(dropSmallRegions(t, specks, 5)) == 25);
	assert(count(slivers(t, corridor, 2)) > 0 && !slivers(t, corridor, 2)[t.at(5, 12)]);
}

// Growth: a pond waters what is near it and nothing more than its probe reaches; draining the dry
// zone of a region leaves it dry.
inline void growthChecks()
{
	const Torus t(64, 64);
	TerrainSketch sketch(t.size(), GRASS);
	for (int y = 28; y < 36; ++y)
		for (int x = 28; x < 36; ++x)
			sketch[t.at(x, y)] = WATER;
	layBeaches(sketch, t);
	Fertility::Field field = cropGrowthField(sketch, t);
	assert(field.at(26, 31) > 0 && field.at(0, 0) == 0);
	std::vector<unsigned char> distant(t.size(), 0), nearby(t.size(), 0);
	distant[t.at(0, 0)] = distant[t.at(63, 63)] = 1;
	nearby[t.at(25, 31)] = 1;
	assert(wetTiles(field, distant) == 0 && wetTiles(field, nearby) == 1);
	assert(wateredShare(field, nearby) == 1.0 && wateredShare(field, distant) == 0.0);
	assert(dryZone(t, nearby)[t.at(40, 31)] && !dryZone(t, nearby)[t.at(41, 31)]);
	std::vector<unsigned char> region(t.size(), 0);
	region[t.at(24, 31)] = 1;
	TerrainSketch drained(t.size(), GRASS);
	for (int y = 28; y < 36; ++y)
		for (int x = 28; x < 36; ++x)
			drained[t.at(x, y)] = WATER;
	assert(drainWithin(drained, dryZone(t, region)) == 64);
	layBeaches(drained, t);
	assert(wetTiles(cropGrowthField(drained, t), region) == 0);
	// Finished-map containment is independent of fertility: even a dry grass component
	// is included. An enclosed sand ring must isolate its interior on the torus.
	Game game(nullptr);
	landscapeGrass(game, 6, 6);
	TerrainSketch enclosed(t.size(), GRASS);
	for (int y = 20; y <= 40; ++y)
		for (int x = 20; x <= 40; ++x)
			if (x <= 22 || x >= 38 || y <= 22 || y >= 38)
				enclosed[t.at(x, y)] = SAND;
	writeUndermap(game.map, enclosed);
	game.map.setResource(5, 5, STONE, 1);
	assert(cropSpreadEnvelope(game.map).visited.empty());
	game.map.setResource(0, 0, WHEAT, 1);
	const auto spread = cropSpreadEnvelope(game.map);
	assert(spread.steps[t.at(63, 63)] == 1); // Diagonal wrapping is a growth route too.
	assert(spread.steps[t.at(30, 30)] < 0);
	std::vector<unsigned char> reserved(t.size(), 0);
	reserved[t.at(30, 30)] = 1;
	assert(cropSeedsIn(game.map, reserved) == 0);
	game.map.setResource(30, 30, WOOD, 1);
	assert(cropSeedsIn(game.map, reserved) == 1);
	assert(cropSpreadEnvelope(game.map).steps[t.at(31, 31)] == 1);
	reserved[t.at(0, 0)] = reserved[t.at(5, 5)] = 1;
	assert(cropSeedsIn(game.map, reserved) == 2); // Stone is not a crop seed.
}

// Contact: a wood wall stops walkers and costs choppers; costSpread and unevenCosts read the costs;
// equalCostSites takes each colony's own side; openColonyRoutes fords a strait between two colonies.
inline void contactChecks()
{
	Game game(nullptr);
	landscapeGrass(game, 5, 5);
	Map &map = game.map;
	const Torus t(map);
	for (int y = 0; y < t.h; ++y)
		map.setResource(16, y, WOOD, 1);
	assert(stepCost(map, 16, 3, StepCosts::walking()) == -1 &&
		   stepCost(map, 16, 3, StepCosts::chopping(5)) == 5);
	// The wall wraps, so the torus is still open round the other side: walkers go round.
	const std::vector<int> walk = costsFrom(map, t, {t.at(10, 3)}, StepCosts::walking());
	const std::vector<int> chop = costsFrom(map, t, {t.at(10, 3)}, StepCosts::chopping(5));
	assert(walk[t.at(22, 3)] == 20 && chop[t.at(22, 3)] == 12 + 4);
	assert(costSpread({3, 7, 5}) == 4 && costSpread({3, -1}) == -1 && costSpread({}) == 0);
	assert(unevenCosts({3, 7}, 4, "the hub").empty() && !unevenCosts({3, 9}, 4, "the hub").empty());
	const std::vector<int> fromA = costsFrom(map, t, {t.at(4, 4)}, StepCosts::walking());
	const std::vector<int> fromB = costsFrom(map, t, {t.at(28, 4)}, StepCosts::walking());
	std::vector<unsigned char> eligible(t.size(), 1);
	const std::vector<int> sites = equalCostSites({fromA, fromB}, eligible, 6, 0);
	assert(sites.size() == 2 && fromA[sites[0]] == 6 && fromB[sites[1]] == 6);
	assert(fromA[sites[0]] < fromB[sites[0]] && fromB[sites[1]] < fromA[sites[1]]);

	Game strait(nullptr);
	landscapeGrass(strait, 6, 5);
	const Torus s(strait.map);
	TerrainSketch sketch(s.size(), GRASS);
	for (int y = 0; y < s.h; ++y)
		for (int x = 0; x < s.w; ++x)
			if ((x >= 20 && x < 26) || (x >= 52 && x < 58))
				sketch[s.at(x, y)] = WATER;
	layBeaches(sketch, s);
	writeUndermap(strait.map, sketch);
	GenerationRequest request;
	request.nbTeams = 2;
	GenerationContext context(request);
	context.bootX[0] = 8;
	context.bootY[0] = 12;
	context.bootX[1] = 38;
	context.bootY[1] = 12;
	assert(openColonyRoutes(strait.map, context, s, StepCosts{1, 3, 8, 25, -1}));
	const std::vector<int> after =
		stepsFrom(s, tileMask(s, {s.at(7, 11)}), walkableTiles(strait.map));
	assert(after[s.at(42, 12)] >= 0);
	assert(!openColonyRoutes(strait.map, context, s, StepCosts{1, 3, 8, 25, -1}));
}

// Points: darts keep their spacing; unwarped labels are the brute-force nearest site; relaxation keeps
// every site and evens the cells; the site graph is symmetric and a maze carved on it spans it.
inline void pointChecks()
{
	const Torus t(96, 64);
	GenerationRequest request;
	request.seed = 12;
	GenerationContext context(request), again(request);
	const std::vector<Site> sites = spreadPoints(t, 20, context, "sites");
	assert(sites.size() >= 8 && spreadPoints(t, 20, again, "sites").size() == sites.size());
	for (size_t a = 0; a < sites.size(); ++a)
		for (size_t b = a + 1; b < sites.size(); ++b)
			assert(t.dist2(sites[a].x, sites[a].y, sites[b].x, sites[b].y) >= 16 * 16);
	const std::vector<int> labels = nearestSiteLabels(t, sites, 20, context, "warp", 150, 0);
	for (int i = 0; i < t.size(); i += 7)
	{
		int best = 0;
		for (size_t s = 1; s < sites.size(); ++s)
		{
			const int x = i % t.w, y = i / t.w;
			if (t.dist2(x, y, sites[s].x, sites[s].y) < t.dist2(x, y, sites[best].x, sites[best].y))
				best = int(s);
		}
		const int x = i % t.w, y = i / t.w;
		assert(t.dist2(x, y, sites[labels[i]].x, sites[labels[i]].y) ==
			   t.dist2(x, y, sites[best].x, sites[best].y));
	}
	const auto spread = [&](const std::vector<Site> &s)
	{
		const std::vector<int> l = nearestSiteLabels(t, s, 20, context, "warp", 150, 0);
		std::vector<int> size(s.size(), 0);
		for (int v : l)
			++size[v];
		return *std::max_element(size.begin(), size.end()) -
			   *std::min_element(size.begin(), size.end());
	};
	const std::vector<Site> relaxed = relaxPoints(t, sites, 3);
	assert(relaxed.size() == sites.size() && spread(relaxed) <= spread(sites));
	const auto graph = siteNeighbours(t, labels, int(sites.size()));
	for (size_t a = 0; a < graph.size(); ++a)
	{
		assert(!graph[a].empty());
		for (int b : graph[a])
			assert(std::count(graph[b].begin(), graph[b].end(), int(a)) == 1);
	}
	const CellGraph cells = cellGraph(t, sites, graph);
	std::vector<unsigned char> blocked(sites.size(), 0), open(cells.edgeCells.size(), 0);
	assert(carveSpanningTree(cells, context, "maze", blocked, open));
	assert(count(open) == int(sites.size()) - 1);
	assert(spreadPockets(cells, 2).size() == 2);
}

// Patterns: Turing fields repeat per seed, fill both signs and settle near their wavelength; stripes
// wrap exactly across both seams; the upwind shadow counts steps against the wind; a streamline in a
// constant field is straight.
inline void patternChecks()
{
	const Torus t(128, 128);
	std::mt19937 a(3), b(3);
	TuringStyle style;
	style.wavelength = 16;
	const std::vector<int> field = turingPattern(t, style, a);
	assert(field == turingPattern(t, style, b));
	int crossings = 0, positive = 0;
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int v = field[size_t(t.at(x, y))];
			assert(v >= -32768 && v <= 32768);
			positive += v > 0;
			crossings += (v > 0) != (field[size_t(t.at(x + 1, y))] > 0);
		}
	const double period = 2.0 * t.size() / std::max(1, crossings);
	assert(positive > t.size() / 5 && positive < t.size() * 4 / 5 && period > 10 && period < 26);

	StripeStyle stripes;
	stripes.acrossX = 3;
	stripes.acrossY = 2;
	stripes.warpPercent = 0;
	const Torus r(64, 32);
	const std::vector<int> phase = stripePhase(r, stripes, a);
	// One step along x climbs 3/64 of a turn, one step down 2/32: the same across the seam.
	for (int y = 0; y < r.h; ++y)
	{
		const int step =
			((phase[size_t(r.at(0, y))] - phase[size_t(r.at(r.w - 1, y))]) % 65536 + 65536) % 65536;
		assert(std::abs(step - 3 * 65536 / 64) <= 1);
	}
	assert(std::abs(stripeSpacing(r, stripes) - 1 / std::hypot(3.0 / 64, 2.0 / 32)) < 1e-9);
	assert(stripeDistance(100) == 100 && stripeDistance(65436) == 100);

	std::vector<unsigned char> ridge(r.size(), 0);
	for (int y = 0; y < r.h; ++y)
		ridge[size_t(r.at(10, y))] = 1;
	const std::vector<int> shadow = upwindSteps(r, ridge, 1, 0, 8);
	assert(shadow[size_t(r.at(10, 4))] == 0 && shadow[size_t(r.at(13, 4))] == 3 &&
		   shadow[size_t(r.at(19, 4))] == -1 && shadow[size_t(r.at(7, 4))] == -1);
	const std::vector<StrokePoint> line =
		traceStreamline(r, {60, 5}, [](double, double) { return 0.0; }, 10, 1.0, 2.0);
	assert(line.size() == 11 && std::abs(line.back().x - 70) < 1e-9 && line.back().y == 5);
}

// Wandering paths leave and arrive where asked, even the short way across a seam, and a carved
// corridor joins its ends.
inline void wanderChecks()
{
	const Torus t(64, 64);
	std::mt19937 rng(5);
	const std::vector<StrokePoint> path = wanderingPath(t, {60, 10}, {6, 12}, 1.5, 6, 0.3, rng);
	assert(path.front().x == 60 && path.front().y == 10);
	assert(std::abs(path.back().x - 70) < 1e-9 && std::abs(path.back().y - 12) < 1e-9);
	assert(path.size() == 12); // a point about every tile: ceil(hypot(10, 2)) segments
	std::vector<unsigned char> mask(t.size(), 0);
	carveCorridor(mask, t, {10, 40}, {50, 20}, 1.5, 8, 0.2, rng);
	assert(mask[t.at(10, 40)] && mask[t.at(50, 20)]);
	assert(stepsFrom(t, tileMask(t, {t.at(10, 40)}), mask)[t.at(50, 20)] > 0);
}

// Channels: a channel w corners wide spoils w + 3 tiles of grass and puts its banks bankToBank(w)
// apart, which towerReach agrees with; the widest crossable channel is in range and the next is not;
// a bridge carries units across and is counted once per side.
inline void channelChecks()
{
	const Torus t(64, 8);
	for (int w = 1; w <= 8; ++w)
	{
		TerrainSketch sketch(t.size(), GRASS);
		for (int y = 0; y < t.h; ++y)
			for (int x = 20; x < 20 + w; ++x)
				sketch[t.at(x, y)] = WATER;
		layBeaches(sketch, t);
		const std::vector<unsigned char> grass = pureTiles(sketch, t, GRASS);
		const std::vector<unsigned char> beach = beachTiles(sketch, t);
		int spoiled = 0;
		for (int x = 0; x < t.w; ++x)
			spoiled += !grass[t.at(x, 3)];
		assert(spoiled == w + kChannelSpoiledTiles);
		assert(count(beach) == (spoiled - std::max(0, w - 1)) * t.h);
		std::vector<unsigned char> west(t.size(), 0), east(t.size(), 0);
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
			{
				west[t.at(x, y)] = grass[t.at(x, y)] && x < 20;
				east[t.at(x, y)] = grass[t.at(x, y)] && x >= 20 + w && x < 30 + w;
			}
		assert(towerReach(t, west, east) == bankToBank(w));
	}
	for (int level = 1; level <= 3; ++level)
	{
		const int widest = widestChannelTowersCross(level);
		assert(widest >= 1 && bankToBank(widest) <= kTowerRange[level - 1] &&
			   bankToBank(narrowestChannelTowersMiss(level)) > kTowerRange[level - 1]);
	}
	assert(widestChannelTowersCross(3, 3) == widestChannelTowersCross(3) - 2 &&
		   widestChannelTowersCross(1, 3) == 0);

	TerrainSketch sketch(t.size(), GRASS);
	for (int y = 0; y < t.h; ++y)
		for (int x = 20; x < 26; ++x)
			sketch[t.at(x, y)] = WATER;
	layBeaches(sketch, t);
	assert(bridgeAcross(sketch, t, {18, 4}, {28, 4}, 0.5) == 6);
	const std::vector<unsigned char> water = pureTiles(sketch, t, WATER);
	std::vector<unsigned char> open(t.size(), 0), bridge(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		open[i] = !water[i];
	for (int x = 20; x < 26; ++x)
		bridge[t.at(x, 4)] = 1;
	// The wrap joins the banks round the far side too, so only the bridge's own row is checked.
	std::vector<unsigned char> nearby(t.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 16; x < 30; ++x)
			nearby[t.at(x, y)] = open[t.at(x, y)];
	assert(stepsFrom(t, tileMask(t, {t.at(17, 4)}), nearby)[t.at(28, 4)] > 0);
	std::vector<int> sides(t.size(), -1);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			sides[t.at(x, y)] = x < 20 ? 0 : x >= 26 ? 1 : -1;
	assert(crossingsPerLabel(t, bridge, sides, 2) == std::vector<int>({1, 1}));
}

// Room: footprints wrap, a blocked tile removes the sixteen that cover it, a region bounds them, and a
// chamber grows until it holds what it was asked for.
inline void roomChecks()
{
	const Torus t(16, 16);
	std::vector<unsigned char> buildable(t.size(), 1), all(t.size(), 1);
	assert(count(buildAnchors(t, buildable)) == 256);
	buildable[t.at(0, 0)] = 0;
	assert(count(buildAnchors(t, buildable)) == 256 - 16);
	std::vector<unsigned char> region(t.size(), 0);
	for (int y = 4; y < 12; ++y)
		for (int x = 4; x < 12; ++x)
			region[t.at(x, y)] = 1;
	assert(buildSites(t, buildable, region) == 25);
	std::vector<unsigned char> chamber(t.size(), 0);
	for (int y = 6; y < 10; ++y)
		for (int x = 6; x < 10; ++x)
			chamber[t.at(x, y)] = 1;
	const int grown = growUntilSites(t, chamber, buildable, all, 9, 200,
									 [&](int i) { return t.dist2(i % t.w, i / t.w, 8, 8); });
	assert(grown >= 9 && buildSites(t, buildable, chamber) == grown && count(chamber) < 60);

	Game game(nullptr);
	landscapeGrass(game, 4, 4);
	assert(count(buildableTiles(game.map)) == 256);
	game.map.setResource(3, 3, WOOD, 1);
	assert(count(buildableTiles(game.map)) == 255);
}

// A home in a rectangle with its door on the left: the swarm stands its depth in, the axis points away
// from the door, and the footprint is centred on the site.
inline void regionHomeChecks()
{
	const Torus t(64, 64);
	std::vector<unsigned char> region(t.size(), 0);
	for (int y = 10; y < 30; ++y)
		for (int x = 10; x < 50; ++x)
			region[t.at(x, y)] = 1;
	std::vector<int> door;
	for (int y = 18; y < 22; ++y)
		door.push_back(t.at(10, y));
	const RegionHome home = regionHome(t, region, door, 12, 1, 4);
	assert(home.site >= 0);
	const int sx = home.site % t.w, sy = home.site / t.w;
	assert(std::abs(sx - 22) <= 1 && sy >= 14 && sy <= 25);
	assert(std::cos(home.axis) > 0.7 && home.swarm.x == sx - 2 && home.swarm.y == sy - 2);
	assert(home.kitCentre.x > sx);
	assert(regionHome(t, region, {}, 12, 1, 4).site < 0);
}

// Biome kits: worth is relative to a fertile plain; an orchard island rises out of its lake; a
// fortress rings its ground in stone except at the door; a forest covers most of its open ground.
inline void biomeChecks()
{
	assert(std::abs(biomeWorth(fertilePlain()) - 1) < 1e-12);
	assert(biomeWorth(stoneFortress()) > 0 && biomeWorth(forest()) > 0 &&
		   biomeWorth(orchardIsland()) > 0);
	for (const BiomeKit &kit : {fertilePlain(), stoneFortress(), orchardIsland(), forest()})
	{
		Game game(nullptr);
		landscapeGrass(game, 7, 7);
		const Torus t(game.map);
		std::vector<unsigned char> region(t.size(), 0), doors(t.size(), 0), keep(t.size(), 0);
		for (int y = 20; y < 108; ++y)
			for (int x = 20; x < 108; ++x)
				region[t.at(x, y)] = 1;
		for (int y = 60; y < 68; ++y)
			doors[t.at(20, y)] = doors[t.at(21, y)] = 1;
		GenerationRequest request;
		request.seed = 31;
		GenerationContext context(request);
		TerrainSketch sketch(t.size(), GRASS);
		const BiomeTerrain terrain = sketchBiome(sketch, t, region, doors, kit, context, "biome");
		assert(count(terrain.water) > 0);
		for (int i = 0; i < t.size(); ++i)
			assert(!terrain.water[i] || region[i]);
		if (kit.orchardIsland)
		{
			assert(count(terrain.island) > 0);
			const std::vector<unsigned char> ring = dilate(t, terrain.island, 1);
			for (int i = 0; i < t.size(); ++i)
				assert(!ring[i] || terrain.island[i] || terrain.water[i]);
		}
		assert((count(terrain.wall) > 0) == (kit.wallThickness > 0));
		for (int i = 0; i < t.size(); ++i)
			assert(!terrain.wall[i] || !doors[i]);
		layBeaches(sketch, t);
		writeUndermap(game.map, sketch);
		furnishBiome(game.map, t, context, region, terrain, kit, keep, "biome");
		int stone = 0, wood = 0, fruit = 0, outside = 0;
		for (int i = 0; i < t.size(); ++i)
		{
			const Resource &r = game.map.getResource(i % t.w, i / t.w);
			if (r.type == NO_RES_TYPE)
				continue;
			outside += !region[i];
			stone += r.type == STONE && terrain.wall[i];
			wood += r.type == WOOD;
			fruit += r.type >= CHERRY && r.type <= CHERRY + 2 && terrain.island[i];
		}
		assert(outside == 0);
		assert(kit.wallThickness == 0 || stone > count(terrain.wall) / 2);
		assert(!kit.orchardIsland || fruit > 0);
		assert(kit.coverPercent == 0 || wood > count(region) / 4);
	}
}

// dealStarts: a permutation, the same again from the same request, and not the identity for every
// seed (so a design's first site is not always colony 0's).
inline void dealChecks()
{
	GenerationRequest request;
	request.seed = 21;
	std::vector<int> sites{0, 1, 2, 3, 4, 5, 6, 7};
	GenerationContext context(request), again(request);
	dealStarts(context, sites);
	std::vector<int> repeat{0, 1, 2, 3, 4, 5, 6, 7};
	dealStarts(again, repeat);
	assert(sites == repeat);
	std::vector<int> sorted(sites);
	std::sort(sorted.begin(), sorted.end());
	assert(sorted == std::vector<int>({0, 1, 2, 3, 4, 5, 6, 7}));
	int moved = 0;
	for (std::uint32_t seed = 1; seed <= 12; ++seed)
	{
		GenerationRequest other;
		other.seed = seed;
		GenerationContext c(other);
		std::vector<int> dealt{0, 1, 2, 3};
		dealStarts(c, dealt);
		moved += dealt[0] != 0;
	}
	assert(moved >= 6);
}

// Circular farm caps must block future crop spread, including across the torus seam.
// Test narrow bands and arbitrary crossing angles after beach conversion, not merely
// that the primitive painted some water. The sand core must join inner and outer caps.
inline void contourFarmChecks()
{
	// The owner/boundary query needs exact ties and sensible missing-site sentinels.
	const Torus rectangle{128, 64};
	assert(nearestTwoSites(rectangle, {}, 0, 0).first == -1);
	const auto lone = nearestTwoSites(rectangle, {{127, 63}}, 0, 0);
	assert(lone.first == 0 && lone.firstDistanceSquared == 2 && lone.second == -1);
	const auto tie = nearestTwoSites(rectangle, {{1, 0}, {127, 0}, {60, 30}}, 0, 0);
	assert(tie.first == 0 && tie.second == 1 && tie.firstDistanceSquared == 1 &&
		   tie.secondDistanceSquared == 1);
	const auto duplicate = nearestTwoSites(rectangle, {{0, 0}, {0, 0}}, 0, 0);
	assert(duplicate.first == 0 && duplicate.second == 1 && duplicate.secondDistanceSquared == 0);
	const Torus t{128, 128};
	for (double phase : {0.0, 0.37, kPi / 4})
		for (int crossings : {2, 3, 4})
		{
			TerrainSketch sketch(t.size(), GRASS);
			ContourFarmStyle style;
			style.innerRadius = 12;
			style.rows = {8, 6.4};
			style.crossings = crossings;
			style.phase = phase;
			// One disc wraps both seams; the other tests ordinary translation.
			const auto contours = layContourFarm(sketch, t, {{0, 0}, {64, 64}}, style);
			layBeaches(sketch, t);
			const auto grass = pureTiles(sketch, t, GRASS);
			const auto water = pureTiles(sketch, t, WATER);
			const auto sand = pureTiles(sketch, t, SAND);
			assert(count(water) > 100 && contours.farm.rows == 1);
			const auto growth = stepsFrom(t, tileMask(t, {t.at(0, 0), t.at(64, 64)}), grass);
			for (int i = 0; i < t.size(); ++i)
			{
				assert(contours.farm.row[i] < 0 || growth[i] < 0);
				assert(sketch[i] == sketch[t.at(i % t.w + 64, i / t.w + 64)]);
			}
			for (int s = 0; s < crossings; ++s)
			{
				const double angle = phase + s * 2 * kPi / crossings;
				const auto atRadius = [&](double radius)
				{
					return t.at(int(std::lround(radius * std::cos(angle))),
								int(std::lround(radius * std::sin(angle))));
				};
				const int entry = atRadius(style.innerRadius + 1);
				const int exit = atRadius(style.outerRadius() - 1);
				// Keep the search on crossing cores only: going around a water band
				// via another stair would hide a broken diagonal crossing here.
				std::vector<unsigned char> walk(t.size(), 0);
				for (int i = 0; i < t.size(); ++i)
					walk[i] = sand[i] && contours.crossings[i];
				const int source =
					seedNear(t, entry % t.w, entry / t.w, 3, [&](int i) { return walk[i]; });
				const int target =
					seedNear(t, exit % t.w, exit / t.w, 3, [&](int i) { return walk[i]; });
				assert(source >= 0 && target >= 0);
				const auto reached = stepsFrom(t, tileMask(t, {source}), walk);
				assert(reached[target] >= 0);
			}
			int cores = 0;
			for (int i = 0; i < t.size(); ++i)
				cores += sand[i] && contours.crossings[i];
			assert(cores > 50);
		}
	// Lobed contours: the summit and its cap stay round, no band reaches farther than the
	// amplitude past the circular outer radius, some band ground lies beyond that circle, and
	// the layout is still a function of the whole-corner translation.
	{
		TerrainSketch sketch(t.size(), GRASS);
		ContourFarmStyle style;
		style.innerRadius = 12;
		style.rows = {8, 6.4};
		style.bands = 2;
		style.wobbles = {{3, {0.3, 1.1, 2.0}}};
		assert(std::abs(style.reach() - style.outerRadius() - 3) < 1e-9);
		assert(contourNominal(style, 0, style.innerRadius + style.cap, 1.0) ==
			   style.innerRadius + style.cap);
		assert(contourNominal(style, 5, 30, 1.0) == 30);
		// One hill on the seam: two bands plus the wobble reach farther than half the 64-tile
		// spacing the circular cases use, and discs must not overlap.
		const auto contours = layContourFarm(sketch, t, {{0, 0}}, style);
		assert(contours.farm.rows == 2);
		int lobed = 0;
		for (int i = 0; i < t.size(); ++i)
		{
			const double d = std::hypot(t.offsetX(0, i % t.w), t.offsetY(0, i / t.w));
			if (d < style.innerRadius || d > style.outerRadius() + 3)
				assert(contours.farm.row[i] < 0 && !contours.farm.sand[i]);
			else if (d < style.innerRadius + style.cap)
				assert(contours.farm.row[i] < 0 && contours.farm.sand[i]);
			lobed += contours.farm.row[i] >= 0 && d > style.outerRadius();
		}
		assert(lobed > 0);
	}
}

inline void landscapeChecks()
{
	contourFarmChecks();
	dealChecks();
	orbitChecks();
	morphologyChecks();
	growthChecks();
	contactChecks();
	pointChecks();
	patternChecks();
	wanderChecks();
	channelChecks();
	roomChecks();
	regionHomeChecks();
	biomeChecks();
	puts(
		"PASS landscape toolkit: symmetry groups, morphology, crop growth, cost models and routes, "
		"sites and cell graphs, patterns, wandering paths, channels, building room, region homes, "
		"biome kits");
}
} // namespace LandscapeChecks
