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
#include "Geometry.h"
#include "Settlements.h"
#include "Topology.h"
#include <cmath>

static void frameworkChecks()
{
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
