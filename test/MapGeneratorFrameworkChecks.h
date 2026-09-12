// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Game.h"
#include "GenerationService.h"
#include "GenerationContext.h"
#include "Utilities.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include "GenerationValidation.h"
#include "Distances.h"
#include "Geometry.h"
#include "Regions.h"
#include "Settlements.h"
#include "Topology.h"
#include <cmath>
#include <cstdint>
#include <set>

/// Everything a generator decides that a player can see: terrain, resources and where each
/// colony starts. Two maps with the same fingerprint are the same map for golden comparisons.
inline std::uint64_t mapFingerprint(const Game &game)
{
	std::uint64_t hash = 14695981039346656037ull;
	auto add = [&](unsigned v)
	{
		hash ^= v;
		hash *= 1099511628211ull;
	};
	add(game.map.getW());
	add(game.map.getH());
	for (int y = 0; y < game.map.getH(); ++y)
		for (int x = 0; x < game.map.getW(); ++x)
		{
			add(game.map.getUMTerrain(x, y));
			add(game.map.getTerrain(x, y));
			add(game.map.getResource(x, y).type);
			add(game.map.getResource(x, y).amount);
		}
	add(game.teamsCount());
	for (int i = 0; i < game.teamsCount(); ++i)
	{
		add(game.teams[i]->startPosX);
		add(game.teams[i]->startPosY);
	}
	return hash;
}

// The whole-region dispersion ends where no point can improve its own score, the smallest
// weighted squared distance to any other point, by moving anywhere in the region; the local
// search, anywhere within its 7x7 window. Checked against a brute-force score, so the
// incremental nearest-pair bookkeeping behind the whole-region search has an oracle.
inline void dispersionChecks()
{
	using namespace MapGeneration;
	for (int variant = 0; variant < 4; ++variant)
	{
		const bool whole = variant % 2 == 0;
		const int count = variant < 2 ? 5 : 3;
		Game game(nullptr);
		game.map.setSize(5, 5);
		Map &map = game.map;
		const int w = map.getW(), h = map.getH();
		std::vector<int> grid(size_t(w) * h, 0);
		// Two blocked bands make the region ragged, so "anywhere in the region" is a real test.
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				if ((x >= 10 && x < 14 && y < 20) || (y >= 24 && y < 27 && x >= 6))
					grid[y * w + x] = 1;
		auto run = [&](std::uint32_t seed, std::vector<MapGeneratorPoint> &points,
					   std::vector<int> &weights)
		{
			GenerationRequest request;
			request.seed = seed;
			GenerationContext context(request);
			points.assign(count, MapGeneratorPoint(0, 0));
			weights.assign(count, 2);
			weights.back() = 1;
			return splitUpPoints(map, context, grid, 0, points, weights,
								 whole ? PointSearch::WholeRegion : PointSearch::Local);
		};
		std::vector<MapGeneratorPoint> points, again;
		std::vector<int> weights, weightsAgain;
		const int spread = run(1234 + variant, points, weights);
		run(1234 + variant, again, weightsAgain);
		assert(spread > 0);
		for (int i = 0; i < count; ++i)
			assert(points[i].x == again[i].x && points[i].y == again[i].y &&
				   weights[i] == weightsAgain[i]);
		auto score = [&](int i, int x, int y)
		{
			std::int64_t best = std::numeric_limits<int>::max();
			for (int j = 0; j < count; ++j)
				if (j != i)
					best = std::min(best, std::int64_t(map.warpDistSquare(x, y, points[j].x,
																			points[j].y)) *
											  weights[j]);
			return best;
		};
		std::set<std::pair<int, int>> occupied;
		for (int i = 0; i < count; ++i)
		{
			assert(grid[points[i].y * w + points[i].x] == 0);
			assert(occupied.insert({points[i].x, points[i].y}).second);
		}
		for (int i = 0; i < count; ++i)
		{
			const std::int64_t own = score(i, points[i].x, points[i].y);
			auto consider = [&](int x, int y)
			{
				if (grid[y * w + x] != 0 || occupied.count({x, y}))
					return;
				assert(score(i, x, y) <= own);
			};
			if (whole)
			{
				for (int y = 0; y < h; ++y)
					for (int x = 0; x < w; ++x)
						consider(x, y);
			}
			else
				for (int dx = -3; dx <= 3; ++dx)
					for (int dy = -3; dy <= 3; ++dy)
						consider(map.normalizeX(points[i].x + dx), map.normalizeY(points[i].y + dy));
		}
	}
}

// computeDistances floods eight-connected over the torus: a source reads 1, each ring one
// more, obstacles -1 and never crossed, unreached tiles 0.
inline void distanceChecks()
{
	using namespace MapGeneration;
	Game game(nullptr);
	game.map.setSize(5, 4);
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	std::vector<MapGeneratorPoint> sources{{3, 3}, {30, 12}}, none, wall;
	std::vector<int> heights;
	computeDistances(map, sources, none, heights);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			int nearest = std::numeric_limits<int>::max();
			for (const auto &s : sources)
			{
				const int dx = std::min(std::abs(x - s.x), w - std::abs(x - s.x));
				const int dy = std::min(std::abs(y - s.y), h - std::abs(y - s.y));
				nearest = std::min(nearest, std::max(dx, dy));
			}
			assert(heights[y * w + x] == nearest + 1);
		}
	// A ring of obstacles around the first source holds its flood; the second still spreads.
	std::vector<MapGeneratorPoint> one{{3, 3}};
	for (int dx = -1; dx <= 1; ++dx)
		for (int dy = -1; dy <= 1; ++dy)
			if (dx || dy)
				wall.push_back({3 + dx, 3 + dy});
	computeDistances(map, one, wall, heights);
	assert(heights[3 * w + 3] == 1);
	for (const auto &o : wall)
		assert(heights[o.y * w + o.x] == -1);
	assert(heights[0] == 0 && heights[10 * w + 10] == 0);
	// Repeated sources are tolerated and change nothing.
	std::vector<MapGeneratorPoint> twice{{3, 3}, {3, 3}, {30, 12}};
	std::vector<int> repeated;
	computeDistances(map, twice, none, repeated);
	computeDistances(map, sources, none, heights);
	assert(repeated == heights);
}

inline void frameworkChecks()
{
	dispersionChecks();
	distanceChecks();
	using namespace MapGeneration;
	GeneratorControl cells{"cell", "Cell", 4,		  16, 1, 8, ControlGroup::Layout,
						   false,  false,  {4, 8, 16}};
	assert(cells.normalize(6) == 8 && cells.normalize(11) == 8 && cells.normalize(12) == 16);
	assert(cells.normalize(-100) == 4 && cells.normalize(100) == 16);
	for (int v : cells.values())
		assert(cells.valueAt(cells.indexOf(v)) == v);
	GenerationRequest request;
	request.seed = 817;
	GenerationContext context(request), repeat(request);
	ShapeTransform transform({12, 23}, 0.72, 1.3);
	const ShapePoint point{3, -7};
	const auto back = transform.toShape(transform.toMap(point));
	assert(std::abs(back.x - point.x) < 1e-10 && std::abs(back.y - point.y) < 1e-10);
	RadialShape shape(12, .7, context, "coast"), same(12, .7, repeat, "coast");
	for (int i = 0; i < 720; ++i)
	{
		double r = shape.radiusAt(i * .01);
		assert(r > 0 && r <= shape.maximumRadius() && r == same.radiusAt(i * .01));
	}
	RadialShape circle(3, 0, context, "circle");
	std::vector<int> wrapped(64), clipped(64);
	stampShape(wrapped, 8, 8, 7, ShapeTransform({0, 0}, 0), circle, true);
	stampShape(clipped, 8, 8, 7, ShapeTransform({0, 0}, 0), circle, false);
	assert(wrapped[7] == 7 && clipped[7] == 0);
	std::vector<unsigned char> mask{1, 0, 1, 0, 0, 0, 0, 1, 0};
	auto cardinal = connectedRegions(mask, 3, 3, false);
	auto torus = connectedRegions(mask, 3, 3, true);
	assert(cardinal[0] != cardinal[2] && torus[0] == torus[2] && cardinal[1] == -1);
	mask = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	assert(connectedRegions(mask, 3, 3, false)[0] != connectedRegions(mask, 3, 3, false)[4]);
	assert(connectedRegions(mask, 3, 3, false, GridNeighbors::Eight)[0] ==
		   connectedRegions(mask, 3, 3, false, GridNeighbors::Eight)[8]);
	auto graph = regionAdjacency({10, 10, 30, 10, 99, 30}, 3, 2, {10, 30, 99, 400}, false);
	auto distances = graphDistances(graph, {0});
	assert(distances == std::vector<int>({0, 1, 1, -1}));
	assert(graphDistances({}, {}).empty());
	bool rejected = false;
	try
	{
		graphDistances({{5}}, {0});
	}
	catch (const std::invalid_argument &)
	{
		rejected = true;
	}
	assert(rejected);

	// A synthetic region, not a playable generator, exercises exact settlement counts
	// and callbacks without weakening the production structural validator.
	GeneratorDefinition definition{
		"test-home",
		305,
		"uniform terrain",
		1,
		false,
		{cells, {"room", "Room", 4, 8, 1, 6}},
		[](Game &game, GenerationContext &ctx)
		{
			game.map.makeHomogenMap(GRASS);
			for (int i = 0; i < ctx.request.nbTeams; ++i)
				game.addTeam();
			std::vector<unsigned char> home(size_t(game.map.getW()) * game.map.getH(), 0);
			int size = ctx.request.option("room");
			for (int y = 10; y < 10 + size; ++y)
				for (int x = 10; x < 10 + size; ++x)
					home[y * game.map.getW() + x] = 1;
			return placeSettlement(game, ctx, 0, home, {11, 11});
		},
		true,
		[](const GenerationRequest &r) -> std::string
		{ return r.option("room") > r.option("cell") ? "Room must fit inside cell" : ""; },
		[](const Game &, const GenerationContext &ctx) -> std::string
		{ return ctx.request.seed == 999 ? "Synthetic topology failure" : ""; }};
	GeneratorRegistry registry({definition});
	GenerationService service(registry);
	request.setMethodDefaults(305, registry);
	request.nbTeams = 1;
	auto surrounding = randomGenerator;
	{
		Game game(nullptr);
		assert(service.generate(game, request));
		assert(game.teams[0]->startPosX >= 10 && game.teams[0]->startPosX <= 12);
	}
	{
		Game game(nullptr);
		request.options["room"] = 4;
		auto failure = service.generate(game, request);
		assert(failure.error == GenerationError::PlacementFailed && failure.stage == "settlement");
		assert(failure.detail.find("worker tiles") != std::string::npos);
	}
	{
		Game game(nullptr);
		request.options["room"] = 6;
		request.options["cell"] = 4;
		auto failure = service.generate(game, request);
		assert(failure.error == GenerationError::InvalidRequest && game.teamsCount() == 0);
	}
	{
		Game game(nullptr);
		request.options["cell"] = 8;
		request.seed = 999;
		auto failure = service.generate(game, request);
		assert(failure.error == GenerationError::InvalidWorld &&
			   failure.stage == "generator validation");
	}
	assert(randomGenerator == surrounding);
	// Invalid weighted inputs fail explicitly; weighted metadata follows the shuffle.
	Game game(nullptr);
	game.map.setSize(6, 6);
	std::vector<int> grid(64 * 64, 0);
	std::vector<MapGeneratorPoint> points(3, {0, 0});
	std::vector<int> weights{1, 2, 2};
	GenerationContext dispersion(request);
	assert(splitUpPoints(game.map, dispersion, grid, 0, points, weights, PointSearch::WholeRegion) >
		   0);
	assert(std::count(weights.begin(), weights.end(), 1) == 1);
	for (size_t i = 0; i < points.size(); ++i)
		for (size_t j = i + 1; j < points.size(); ++j)
			assert(points[i].x != points[j].x || points[i].y != points[j].y);
	// A completed whole-region pass must remain a best response with the final
	// point/weight pairing. This detects shuffling coordinates without their weights.
	for (size_t i = 0; i < points.size(); ++i)
	{
		int score = 2147483647;
		for (size_t j = 0; j < points.size(); ++j) if (i != j)
			score = std::min(score, game.map.warpDistSquare(points[i].x, points[i].y, points[j].x, points[j].y) * weights[j]);
		for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
		{
			int candidate = 2147483647;
			for (size_t j = 0; j < points.size(); ++j) if (i != j)
				candidate = std::min(candidate, game.map.warpDistSquare(x, y, points[j].x, points[j].y) * weights[j]);
			assert(candidate <= score);
		}
	}
	weights.clear();
	rejected = false;
	try
	{
		splitUpPoints(game.map, dispersion, grid, 0, points, weights);
	}
	catch (const GenerationFailure &)
	{
		rejected = true;
	}
	assert(rejected);
	puts("PASS discrete domains, shape bounds, topology, home footprints, exact workers and custom "
		 "validation");
}
