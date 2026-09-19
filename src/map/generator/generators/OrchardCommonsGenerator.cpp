// SPDX-License-Identifier: GPL-3.0-or-later
#include "OrchardCommonsGenerator.h"
#include "Contact.h"
#include "Drawing.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Growth.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Room.h"
#include "Sketch.h"
#include <algorithm>
#include <array>
#include <limits>
using namespace MapGeneration;

// Fruit is permanent geography: three separate varieties, renewable where planted, never
// spreading. The shared valley supplies diets; ordinary homes supply survival. See
// docs/map-generators/ORCHARD_COMMONS.md for the conversion and containment contracts.
namespace
{
struct Plot
{
	std::vector<int> tiles;
	int type, minimum, extra;
};
struct Grove
{
	ShapePoint centre;
	int kind;
	std::vector<int> tiles;
};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<ShapePoint> homes, courts;
	std::vector<Plot> plots;
	std::vector<Grove> groves;
	std::vector<int> plotOf, groveOf;
	std::vector<unsigned char> woodland;
	std::string failure;
};
std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 2 || r.nbTeams > 8)
		return "Orchard Commons supports 2 to 8 colonies.";
	if (r.wDec < 7 || r.hDec < 7 || r.wDec > 9 || r.hDec > 9 || std::abs(r.wDec - r.hDec) > 1)
		return "Orchard Commons needs 128 to 512 tile sides and an aspect ratio at most 2:1.";
	if (r.nbTeams > 4 && std::min(r.wDec, r.hDec) < 8)
		return "Five or more colonies need at least 256 by 256 tiles.";
	return "";
}
void addPlot(Layout &L, GenerationContext &c, ShapePoint p, double radius, int type, int minimum,
			 int extra, const Stretch &stretch = {})
{
	std::vector<int> corners;
	RadialShape shape(radius, .32, c, "orchard-plots");
	forEachTileInShape(
		L.t, p.x, p.y, shape, 0, [&](int i, double, double) { corners.push_back(i); }, stretch);
	Plot plot{stampContainedPlot(L.terrain, L.t, corners), type, minimum, extra};
	for (int i : plot.tiles)
		L.plotOf[i] = int(L.plots.size());
	L.plots.push_back(std::move(plot));
}
Layout design(const GenerationRequest &r, GenerationContext &c)
{
	Layout L;
	L.failure = validateRequest(r);
	if (!L.failure.empty())
		return L;
	L.t = {1 << r.wDec, 1 << r.hDec};
	const Torus &t = L.t;
	const OrchardCommonsOptions o(r);
	L.terrain.assign(t.size(), GRASS);
	L.plotOf.assign(t.size(), -1);
	L.groveOf.assign(t.size(), -1);
	L.woodland.assign(t.size(), 0);
	const bool vertical = t.h > t.w || (t.h == t.w && c.bounded("orchard-heading", 2));
	const int length = vertical ? t.h : t.w, breadth = vertical ? t.w : t.h;
	const int pairs = (r.nbTeams + 1) / 2;
	const int span = std::min(length, 64 * pairs);
	const double begin = (length - span) / 2.;
	const double phase = c.bounded("orchard-valley-phase", 6283) / 1000.;
	const double amplitude =
		std::min(length, 256) / 128.0 * (1 + c.bounded("orchard-valley-bend", 2));
	const auto valley = [&](double x)
	{ return breadth / 2. + amplitude * std::sin(2 * kPi * x / std::min(length, 256) + phase); };
	const auto at = [&](double x, double y)
	{ return vertical ? ShapePoint{y, x} : ShapePoint{x, y}; };
	std::array<int, 3> kinds{{CHERRY, ORANGE, PRUNE}};
	c.shuffle(kinds.begin(), kinds.end(), "orchard-fruit-order");
	const int gap = 16 + o.spacing * 4;
	// The valley bends continuously. Groves vary independently inside their separation budget.
	// A group is a spacing device only: no cell, perimeter or owned home is stamped.
	for (int group = 0; group < pairs; ++group)
	{
		const double x = begin + (group + .5) * span / pairs;
		for (int f = 0; f < 3; ++f)
		{
			const double gx = x + (f - 1) * gap + int(c.bounded("orchard-grove-offsets", 3)) - 1;
			const double gy = valley(gx) + int(c.bounded("orchard-grove-offsets", 3)) - 1;
			Grove g{at(gx, gy), kinds[f], {}};
			RadialShape shape(5.0, .22, c, "orchard-grove-shapes");
			forEachTileInShape(t, g.centre.x, g.centre.y, shape, 0,
							   [&](int i, double, double)
							   {
								   g.tiles.push_back(i);
								   L.groveOf[i] = int(L.groves.size());
							   });
			L.groves.push_back(std::move(g));
		}
	}
	// Odd counts stagger the less populous bank along the whole commons. Leaving the
	// final bank position empty would give the last pair of groves a private owner.
	for (int side : {-1, 1})
	{
		const int count = side < 0 ? (r.nbTeams + 1) / 2 : r.nbTeams / 2;
		for (int k = 0; k < count; ++k)
		{
			const double x = begin + (k + .5) * span / count;
			L.homes.push_back(at(x, valley(x) + side * 42));
		}
	}

	// A narrow sinuous tributary follows each bank. Frequent broad gravel fords keep the
	// commons connected without swimming; the bed does not run through a court or grove.
	for (int side : {-1, 1})
	{
		std::vector<int> fords;

		for (int x = 0; x < length; x += 24)
			fords.push_back(x + int(c.bounded("orchard-fords", 9)) - 4);
		std::vector<ShapePoint> points;
		for (int x = 0; x <= length; x += 8)
			points.push_back(at(x, valley(x) + side * (28 + 2 * std::sin(x * .09 + phase + side))));
		auto path = splinePath(points, 1.);
		for (auto &p : path)
			p.halfWidth = 2.5;
		std::vector<unsigned char> water(t.size(), 0);
		strokePath(water, t, path);
		for (int i = 0; i < t.size(); ++i)
			if (water[i])
			{
				const int along = vertical ? i / t.w : i % t.w;
				bool ford = false;
				for (int x : fords)
					ford |= std::abs(along - x) <= 4 || std::abs(along - x) >= length - 4;
				L.terrain[i] = ford ? SAND : WATER;
			}
	}
	// Found homes: fruit access chooses a roomy site near each bank guide, then the nearby farm
	// is fitted to it. No circular town boundary, wall, or identical home stencil.
	const auto noise = periodicNoise(t.w, t.h, 17, c.stream("orchard-country"));
	for (auto &h : L.homes)
	{
		ShapePoint chosen = h;
		int best = std::numeric_limits<int>::min();
		for (int dy = -6; dy <= 6; ++dy)
			for (int dx = -6; dx <= 6; ++dx)
			{
				int x = int(h.x) + dx, y = int(h.y) + dy;
				bool fits = true;
				for (int yy = -5; yy <= 5; ++yy)
					for (int xx = -5; xx <= 5; ++xx)
						fits &= L.terrain[t.at(x + xx, y + yy)] == GRASS;
				int error = 0;
				for (int kind : {CHERRY, ORANGE, PRUNE})
				{
					int nearest = t.size();
					for (const Grove &g : L.groves)
						if (g.kind == kind)
						{
							int gx = std::abs(x - int(g.centre.x)),
								gy = std::abs(y - int(g.centre.y));
							gx = std::min(gx, t.w - gx);
							gy = std::min(gy, t.h - gy);
							nearest = std::min(nearest, std::max(gx, gy));
						}
					error += std::abs(nearest - 44);
				}
				const int score =
					-error * 10000 - 10 * (dx * dx + dy * dy) + (noise[t.at(x, y)] % 10);
				if (fits && score > best)
				{
					best = score;
					chosen = {double(t.x(x)), double(t.y(y))};
				}
			}
		h = chosen;
	}
	dealStarts(c, L.homes);
	const int farmForm = c.bounded("orchard-farm-form", 3);
	const double elongation = 1.05 + .05 * farmForm;
	const Stretch fieldStretch =
		vertical ? Stretch{1 / elongation, elongation} : Stretch{elongation, 1 / elongation};
	const Stretch pocketStretch = vertical ? Stretch{.8, 1.25} : Stretch{1.25, .8};
	c.telemetry.measure("orchard.farm.form", farmForm);
	for (const ShapePoint h : L.homes)
	{
		// Farm on the outside of town; open ground faces the orchards. The pond is between
		// wheat and wood, keeping both renewable and the wheat close to the starting swarm.
		const double along = vertical ? h.y : h.x;
		const double across = vertical ? h.x : h.y;
		const int side = across < valley(along) ? -1 : 1;
		const ShapePoint wheat = at(along - 16, across + side * 8);
		const ShapePoint wood = at(along + 16, across + side * 8);
		addPlot(L, c, wheat, 9.0, WHEAT, 48, 32, fieldStretch);
		addPlot(L, c, wood, 7.0, WOOD, 28, 18, fieldStretch);
		const ShapePoint pond = at(along - 2 * farmForm, across + side * (16 + farmForm));
		RadialShape shape(7.0, .2, c, "orchard-home-ponds");
		fillShape(L.terrain, t, pond.x, pond.y, shape, 0, WATER);
		addPlot(L, c, at(along + 14, across - 3 * side), 3.0, STONE, 4, 3);
	}
	// Two separate 8x8 courts per grove, with farm pockets toward the stream. The courts
	// face one another across the fruit; their grass joins the valley's open ground.
	for (const Grove &g : L.groves)
	{
		const double along = vertical ? g.centre.y : g.centre.x;
		const double across = vertical ? g.centre.x : g.centre.y;
		for (int side : {-1, 1})
		{
			L.courts.push_back(at(along, across + side * 10));
			addPlot(L, c, at(along, across + side * 21), 3.5, WHEAT, 8, 6, pocketStretch);
		}
	}
	// Beyond the working valley, broad lake basins and sandy uplands make the spare
	// territory useful geography: lakes interrupt outer flanks; sandy ground stays open
	// for movement. Fade features away from farms and the commons instead of cutting
	// them off along a straight reservation boundary. Independent periodic fields keep
	// shorelines irregular and continuous across the map seam.
	const auto basins = fractalNoise(t.w, t.h, 72, 3, c.stream("orchard-outer-basins"));
	// Stretch the dry landforms along the valley, then interrupt their grain with
	// broad winding grass swales. Their open cores can support staging buildings;
	// they are land reservations, not seeded renewable farms.
	const PeriodicNoise uplands(length, breadth, 56, c.stream("orchard-outer-uplands"));
	const PeriodicNoise swales(t.w, t.h, 88, c.stream("orchard-outer-swales"));
	int lakeCorners = 0, desertCorners = 0, swaleCorners = 0;
	// The outer slopes carry broad, coherent woodland instead of individual sand rings.
	// Dry woodland cannot spread. Fertile wood is planted only in the sealed home woodlots.
	const auto forest = periodicNoise(t.w, t.h, 32, c.stream("orchard-forest"));
	std::vector<int> sorted = forest;
	std::sort(sorted.begin(), sorted.end());
	const int threshold = sorted[sorted.size() * 55 / 100];
	for (int i = 0; i < t.size(); ++i)
	{
		const double along = vertical ? i / t.w : i % t.w, across = vertical ? i % t.w : i / t.w;
		int homeDistance2 = t.size();
		for (const ShapePoint h : L.homes)
			homeDistance2 = std::min(homeDistance2, t.dist2(i % t.w, i / t.w, int(h.x), int(h.y)));
		const double outer =
			std::min(1., std::max(0., (std::abs(across - valley(along)) - 40.) / 26.));
		const double away =
			std::min(1., std::max(0., (std::sqrt(double(homeDistance2)) - 38.) / 20.));
		const double freedom = outer * away;
		const double dry = uplands.at(along, 2 * across + 8 * std::sin(along * 2 * kPi / length));
		const bool swale = std::abs(swales.at(i % t.w, i / t.w) - .5) < .065;
		if (freedom > 0 && L.terrain[i] == GRASS)
		{
			if (basins[i] < 65536 * .36 * freedom)
			{
				L.terrain[i] = WATER;
				++lakeCorners;
			}
			else if (dry > 1 - .48 * freedom)
			{
				if (swale)
					++swaleCorners;
				else
				{
					L.terrain[i] = SAND;
					++desertCorners;
				}
			}
		}
		L.woodland[i] = !swale && homeDistance2 >= 27 * 27 &&
						std::abs(across - valley(along)) > 55 && forest[i] > threshold;
	}
	c.telemetry.measure("orchard.country.lake-corners", lakeCorners);
	c.telemetry.measure("orchard.country.desert-corners", desertCorners);
	c.telemetry.measure("orchard.country.swale-corners", swaleCorners);
	layBeaches(L.terrain, t);
	const auto grass = pureTiles(L.terrain, t, GRASS);
	for (Plot &p : L.plots)
		p.tiles.erase(
			std::remove_if(p.tiles.begin(), p.tiles.end(), [&](int i) { return !grass[i]; }),
			p.tiles.end());
	for (Grove &g : L.groves)
		g.tiles.erase(
			std::remove_if(g.tiles.begin(), g.tiles.end(), [&](int i) { return !grass[i]; }),
			g.tiles.end());
	for (size_t k = 0; k < L.groves.size(); ++k)
	{
		int nearest = std::numeric_limits<int>::max();
		for (size_t other = 0; other < L.groves.size(); ++other)
			if (other != k)
				nearest = std::min(
					nearest, t.dist2(int(L.groves[k].centre.x), int(L.groves[k].centre.y),
									 int(L.groves[other].centre.x), int(L.groves[other].centre.y)));
		if (nearest < 12 * 12)
		{
			L.failure = "Orchard groves overlap their gathering margins.";
			return L;
		}
		c.telemetry.measure("orchard.grove.neighbor-tiles", std::sqrt(double(nearest)), int(k));
	}
	if (c.telemetry.enabled())
	{
		for (size_t k = 0; k < L.groves.size(); ++k)
		{
			c.telemetry.measure("orchard.grove.x", int(L.groves[k].centre.x), int(k));
			c.telemetry.measure("orchard.grove.y", int(L.groves[k].centre.y), int(k));
			c.telemetry.measure("orchard.grove.kind", L.groves[k].kind, int(k));
		}
		for (size_t k = 0; k < L.courts.size(); ++k)
		{
			c.telemetry.measure("orchard.court.x", int(L.courts[k].x), int(k));
			c.telemetry.measure("orchard.court.y", int(L.courts[k].y), int(k));
		}
	}
	c.telemetry.measure("orchard.groves", L.groves.size());
	c.telemetry.measure("orchard.spacing", gap);
	c.telemetry.measure("orchard.valley.span", span);
	c.telemetry.measure("orchard.valley.bend", amplitude);
	c.telemetry.measure("orchard.valley.vertical", vertical);
	return L;
}
bool generate(Game &game, GenerationContext &c)
{
	c.stage = "orchard layout";
	const Layout L = design(c.request, c);
	if (!L.failure.empty())
	{
		c.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	const OrchardCommonsOptions o(c.request);
	writeUndermap(game.map, L.terrain);
	for (int k = 0; k < c.request.nbTeams; ++k)
		game.addTeam();
	c.stage = "orchard resources";
	const auto fertility = cropGrowthField(L.terrain, t);
	for (size_t k = 0; k < L.plots.size(); ++k)
	{
		const Plot &p = L.plots[k];
		const int amount = p.type == WHEAT ? o.wheat : p.type == WOOD ? o.wood : o.stone;
		const int wanted = p.minimum + (p.type == STONE ? 0 : int(scaledCount(p.extra, amount)));
		const int placed = plantContainedPlot(game.map, t, p.tiles, fertility, p.type, wanted,
											  p.minimum > 0 && p.type != STONE);
		c.telemetry.measure("orchard.plot.planted", placed, int(k));
		if (placed < wanted)
			c.telemetry.fallback("orchard.plot.saturated", "Plot capacity reached", int(k));
		if (placed < p.minimum)
		{
			c.detail = "An orchard farm lacks its renewable supply floor.";
			return false;
		}
	}
	// Scale the map's small stone budget before rounding, then deal tiles evenly.
	// Per-quarry rounding made 75%, 175% and 275% identical to the preceding step.
	int extraStone = int(scaledCount(3 * c.request.nbTeams, o.stone)), stonePlaced = 0;
	while (extraStone > 0)
	{
		bool progress = false;
		for (const Plot &p : L.plots)
			if (p.type == STONE && extraStone > 0)
			{
				if (plantContainedPlot(game.map, t, p.tiles, fertility, STONE, 1, false))
				{
					--extraStone;
					++stonePlaced;
					progress = true;
				}
			}
		if (!progress)
			break;
	}
	c.telemetry.measure("orchard.stone.extra-tiles", stonePlaced);
	if (extraStone)
		c.telemetry.fallback("orchard.stone.saturated", "Quarry grass exhausted");
	for (size_t k = 0; k < L.groves.size(); ++k)
	{
		const Grove &g = L.groves[k];
		const int wanted = 6 + int(scaledCount(18, o.fruit));
		std::vector<int> tiles = g.tiles;
		std::stable_sort(tiles.begin(), tiles.end(),
						 [&](int a, int b)
						 {
							 return t.dist2(a % t.w, a / t.w, int(g.centre.x), int(g.centre.y)) <
									t.dist2(b % t.w, b / t.w, int(g.centre.x), int(g.centre.y));
						 });
		int placed = 0;
		for (int i : tiles)
			if (placed < wanted && clearGround(game.map, i % t.w, i / t.w))
			{
				game.map.setResource(i % t.w, i / t.w, g.kind, 1);
				++placed;
			}
		c.telemetry.measure("orchard.grove.fruit-tiles", placed, int(k));
		if (placed < 6)
		{
			c.detail = "An orchard grove lost its minimum fruit deposit.";
			return false;
		}
	}
	int trees = 0;
	const auto treeNoise = periodicNoise(t.w, t.h, 9, c.stream("orchard-tree-density"));
	std::vector<std::pair<int, int>> treeSites;
	for (int i = 0; i < t.size(); ++i)
		if (L.woodland[i] && fertility.at(i % t.w, i / t.w) == 0 &&
			game.map.isGrass(i % t.w, i / t.w) && clearGround(game.map, i % t.w, i / t.w))
			treeSites.push_back({treeNoise[i], i});
	std::sort(treeSites.begin(), treeSites.end());
	const int wantedTrees =
		std::min(int(treeSites.size()), int(scaledCount(int(treeSites.size()) / 3, o.wood)));
	for (int n = 0; n < wantedTrees; ++n)
	{
		int i = treeSites[n].second;
		game.map.setResource(i % t.w, i / t.w, WOOD, 1);
		++trees;
	}
	c.telemetry.measure("orchard.woodland.tiles", trees);
	seedAlgae(game.map, c, t, "orchard-algae", o.algae, AlgaeBand::anyWater(25));
	// Pick the actual swarm anchor after deposits, beaches and fords exist. Geometric
	// distance alone missed river detours and asymmetric gathering faces at high amounts.
	c.stage = "orchard colonies";
	std::vector<unsigned char> open(t.size(), 0);
	std::array<std::vector<unsigned char>, 3> fruitFaces;
	for (auto &mask : fruitFaces)
		mask.assign(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		open[i] = stepCost(game.map, i % t.w, i / t.w, StepCosts::walking()) >= 0;
	for (int i = 0; i < t.size(); ++i)
	{
		const int type = game.map.getResource(i % t.w, i / t.w).type;
		if (type < CHERRY || type > PRUNE)
			continue;
		for (const auto &step : kCardinalSteps)
		{
			int n = t.at(i % t.w + step[0], i / t.w + step[1]);
			if (open[n])
				fruitFaces[type - CHERRY][n] = 1;
		}
	}
	std::array<std::vector<int>, 3> fruitWalk;
	for (int f = 0; f < 3; ++f)
		fruitWalk[f] = stepsFrom(t, fruitFaces[f], open);
	const auto roomAnchors = buildAnchors(t, potentialBuildingTiles(game.map), 8);
	std::vector<MapGeneratorPoint> anchors;
	for (const ShapePoint h : L.homes)
	{
		int best = std::numeric_limits<int>::max();
		MapGeneratorPoint chosen{int(h.x) - 2, int(h.y) - 2};
		for (int dy = -16; dy <= 16; ++dy)
			for (int dx = -16; dx <= 16; ++dx)
			{
				const int x = t.x(int(h.x) - 2 + dx), y = t.y(int(h.y) - 2 + dy);
				if (!game.map.isFreeForBuilding(t.x(x - 2), t.y(y - 2), 8, 8))
					continue;
				int room = 0;
				for (int by = -16; by <= 12; by += 2)
					for (int bx = -16; bx <= 12; bx += 2)
					{
						if (bx < 4 && bx + 8 > 0 && by < 4 && by + 8 > 0)
							continue;
						room += roomAnchors[t.at(x + bx, y + by)];
					}
				if (room < 3)
					continue;
				int error = 0, worst = 0;
				for (int f = 0; f < 3; ++f)
				{
					const int d = fruitWalk[f][t.at(x + 1, y + 1)];
					error += std::abs(d - 42);
					worst = std::max(worst, std::abs(d - 42));
				}
				const int score = worst * 100000 + error * 1000 + dx * dx + dy * dy;
				if (score < best)
				{
					best = score;
					chosen = {x, y};
				}
			}
		if (best == std::numeric_limits<int>::max())
		{
			c.detail = "No roomy orchard colony site fits its bank.";
			return false;
		}
		anchors.push_back(chosen);
	}
	if (!settleColonies(
			game, c, "orchard-settlement",
			[&](int k)
			{
				std::vector<unsigned char> mask(t.size(), 0);
				for (int dy = -2; dy < 6; ++dy)
					for (int dx = -2; dx < 6; ++dx)
					{
						int i = t.at(anchors[k].x + dx, anchors[k].y + dy);
						mask[i] = 1;
					}
				return mask;
			},
			[&](int k) { return anchors[k]; }))
		return false;
	for (int k = 0; k < c.request.nbTeams; ++k)
		for (int f = 0; f < 3; ++f)
			c.telemetry.measure("orchard.start.fruit-walk",
								fruitWalk[f][t.at(anchors[k].x + 1, anchors[k].y + 1)], k * 3 + f);
	return true;
}
std::string validateWorld(const Game &game, const GenerationContext &c)
{
	GenerationContext replay(c.request);
	const Layout L = design(c.request, replay);
	if (const auto e = designMismatch(L, game.map, "Orchard Commons"); !e.empty())
		return e;
	const Map &map = game.map;
	const Torus &t = L.t;
	const auto walk =
		walkFromFirstColony(map, c.request.nbTeams, "the orchard valley", "without swimming");
	if (!walk.error.empty())
		return walk.error;
	const auto buildable = potentialBuildingTiles(map);
	for (const ShapePoint court : L.courts)
	{
		for (int dy = -4; dy < 4; ++dy)
			for (int dx = -4; dx < 4; ++dx)
			{
				const int i = t.at(int(court.x) + dx, int(court.y) + dy);
				if (!buildable[i] || walk.steps[i] < 0)
					return "An orchard inn clearing lost its 8 by 8 building and upgrade room.";
			}
	}
	const auto fertility = cropGrowthField(L.terrain, t);
	std::vector<unsigned char> future(t.size(), 0), walking(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		walking[i] = stepCost(map, i % t.w, i / t.w, StepCosts::walking()) >= 0;
		future[i] = walking[i] && L.plotOf[i] < 0;
		const int type = map.getResource(i % t.w, i / t.w).type;
		if ((type == WHEAT || type == WOOD) && L.plotOf[i] < 0 &&
			fertility.at(i % t.w, i / t.w) > 0)
			return "An uncontained renewable crop threatens orchard routes.";
	}
	const auto broad = erode(t, future, 1);
	const auto regions = connectedRegions(broad, t.w, t.h, true);
	const int common = regions[t.at(int(L.courts.front().x), int(L.courts.front().y))];
	if (common < 0)
		return "An orchard court lacks a broad approach.";
	for (const ShapePoint court : L.courts)
		if (regions[t.at(int(court.x), int(court.y))] != common)
			return "Orchard courts cannot share the three-tile-wide future route network.";
	for (const Grove &g : L.groves)
		for (int side : {-1, 1})
			for (int d = -7; d <= 7; ++d)
			{
				for (int i : {t.at(int(g.centre.x) + side * 7, int(g.centre.y) + d),
							  t.at(int(g.centre.x) + d, int(g.centre.y) + side * 7)})
					if (!future[i])
						return "A fruit grove lost its two-sided gathering and flanking loop.";
			}
	std::array<std::vector<int>, 3> targets;
	std::vector<int> stocks(L.groves.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const int type = map.getResource(i % t.w, i / t.w).type;
		if (type < CHERRY || type > PRUNE)
			continue;
		const int g = L.groveOf[i];
		if (g < 0 || L.groves[g].kind != type)
			return "Fruit escaped its designated single-variety grove.";
		++stocks[g];
		for (const auto &step : kCardinalSteps)
		{
			int n = t.at(i % t.w + step[0], i / t.w + step[1]);
			if (stepCost(map, n % t.w, n / t.w, StepCosts::walking()) >= 0)
				targets[type - CHERRY].push_back(n);
		}
	}
	for (int stock : stocks)
		if (stock < 6)
			return "A fruit grove lost its minimum supply.";
	std::array<std::vector<int>, 3> access;
	std::vector<int> first;
	std::vector<std::vector<int>> courtAccess(L.courts.size());
	for (int k = 0; k < c.request.nbTeams; ++k)
	{
		// Access belongs to a colony's usable swarm exits, not the random subset occupied
		// by its requested starting workers. A one-worker roll must not redefine fairness.
		std::vector<int> exits;
		const int sx = game.teams[k]->startPosX, sy = game.teams[k]->startPosY;
		for (int dy = -1; dy <= 4; ++dy)
			for (int dx = -1; dx <= 4; ++dx)
			{
				if (dx >= 0 && dx < 4 && dy >= 0 && dy < 4)
					continue;
				const int i = t.at(sx + dx, sy + dy);
				if (stepCost(map, i % t.w, i / t.w, StepCosts::walking()) >= 0)
					exits.push_back(i);
			}
		// Every traversable step has cost one: the shared BFS is equivalent to Dijkstra.
		const auto d = stepsFrom(t, tileMask(t, exits), walking);
		int nearest = t.size();
		for (int f = 0; f < 3; ++f)
		{
			int distance = t.size();
			for (int i : targets[f])
				if (d[i] >= 0)
					distance = std::min(distance, d[i]);
			if (distance == t.size())
				return "A colony cannot gather every fruit variety.";
			access[f].push_back(distance);
			nearest = std::min(nearest, distance);
		}
		first.push_back(nearest);
		int wheat = 0, wood = 0;
		for (const Plot &p : L.plots)
			if (p.type == WHEAT || p.type == WOOD)
				for (int i : p.tiles)
					if (fertility.at(i % t.w, i / t.w) > 0 &&
						map.getResource(i % t.w, i / t.w).type == p.type)
					{
						int reach = t.size();
						for (const auto &step : kCardinalSteps)
						{
							const int n = t.at(i % t.w + step[0], i / t.w + step[1]);
							if (d[n] >= 0)
								reach = std::min(reach, d[n]);
						}
						if (reach <= (p.type == WHEAT ? 24 : 32))
							(p.type == WHEAT ? wheat : wood)++;
					}
		if (wheat < 12 || wood < 8)
			return "A colony lacks accessible renewable wheat or wood frontage.";
		for (size_t court = 0; court < L.courts.size(); ++court)
		{
			int i = t.at(int(L.courts[court].x), int(L.courts[court].y));
			courtAccess[court].push_back(d[i] < 0 ? t.size() : d[i]);
		}
		int competingCourts = 0;
		for (const ShapePoint court : L.courts)
		{
			int i = t.at(int(court.x), int(court.y));
			if (d[i] >= 0 && d[i] <= 64)
				++competingCourts;
		}
		if (competingCourts < 2)
			return "A colony cannot establish competing orchard outposts.";
		if (nearest < 24 || nearest > 48)
			return "First orchard access falls outside 24 to 48 walking steps.";
		std::vector<unsigned char> local(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			local[i] = d[i] >= 0 && d[i] <= 24;
		if (buildSites(t, buildable, local, 8) < 16)
			return "An orchard colony lacks building and upgrade room.";
	}
	for (auto &distances : courtAccess)
	{
		std::sort(distances.begin(), distances.end());
		if (distances[1] > 80 || distances[1] - distances[0] > 48)
			return "An orchard inn site lacks competitive access from two colonies.";
	}
	const auto uneven = [](const std::vector<int> &a)
	{
		return 4 * (*std::max_element(a.begin(), a.end())) >
			   5 * (*std::min_element(a.begin(), a.end()));
	};
	if (uneven(first))
		return "First-grove walking access differs by more than 25 percent.";
	for (const auto &a : access)
		if (uneven(a))
		{
			std::string e = "Walking access to a fruit variety differs by more than 25 percent:";
			for (int d : a)
				e += " " + std::to_string(d);
			return e;
		}
	return "";
}
} // namespace
OrchardCommonsOptions::OrchardCommonsOptions(const GenerationRequest &r)
	: spacing(r.option("orchard-spacing")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition orchardCommonsDefinition()
{
	return {"orchard-commons",
			58,
			"Orchard Commons",
			1,
			false,
			{GeneratorControl::choice("orchard-spacing", "Orchard spacing",
									  {"Compact", "Balanced", "Spread"}, 1, ControlGroup::Layout),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validateRequest,
			validateWorld,
			{"terrain:natural", "feature:river", "feature:forest", "feature:lakes",
			 "feature:desert", "style:contested-center", "style:wide-open"}};
}
