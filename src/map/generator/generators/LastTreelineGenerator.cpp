// SPDX-License-Identifier: GPL-3.0-or-later
#include "LastTreelineGenerator.h"
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
#include "Room.h"
#include "Sketch.h"
#include "Topology.h"
#include <algorithm>
#include <limits>
using namespace MapGeneration;

// A retreating lake leaves wooded shore fragments in a sandy basin. Homes can feed
// themselves, but their dry timber is finite. Continued construction draws workers
// to two neighbouring groves. Sand contains the faster-spreading trees and preserves
// both the inner lakebed route and the outer approach. No special simulation rules.
namespace
{
constexpr int kHomeWood = 48;
constexpr int kHomeWheat = 64;
struct Patch
{
	ShapePoint centre;
	std::vector<int> tiles;
	int type;
};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<ShapePoint> homes, courts;
	std::vector<Patch> patches;
	std::vector<int> plotOf;
	std::string failure;
};
std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 2 || r.nbTeams > 8)
		return "The Last Treeline supports 2 to 8 colonies.";
	if (r.wDec < 8 || r.hDec < 8 || r.wDec > 9 || r.hDec > 9)
		return "The Last Treeline needs 256 or 512 tile sides.";
	return "";
}
Layout design(const GenerationRequest &r, GenerationContext &c)
{
	Layout L;
	L.failure = validateRequest(r);
	if (!L.failure.empty())
		return L;
	L.t = {1 << r.wDec, 1 << r.hDec};
	const Torus &t = L.t;
	const LastTreelineOptions o(r);
	const double bankDepth = 14. + (o.depth - 12) / 2.;
	L.terrain.assign(t.size(), GRASS);
	L.plotOf.assign(t.size(), -1);
	const double cx = t.w / 2., cy = t.h / 2.;
	const double homeRadius = 36 + 6 * r.nbTeams, shoreRadius = homeRadius - 24;
	const double heading = c.bounded("treeline-heading", 6283) / 1000.;
	const int form = c.bounded("treeline-form", 3);
	const double phase = c.bounded("treeline-basin-phase", 6283) / 1000.;
	const auto shoreBend = [&](double a)
	{ return 3 * std::sin(2 * a + phase) + 2 * std::sin(3 * a - phase); };
	const PeriodicNoise country(t.w, t.h, 42, c.stream("treeline-country"));
	const PeriodicNoise bankEdge(t.w, t.h, 9, c.stream("treeline-bank-edge"));
	const PeriodicNoise waterEdge(t.w, t.h, 12, c.stream("treeline-water-edge"));
	// The former lake's scalloped shore opens into dry tongues. Its low centre is
	// traversable sand, not a swimming gate; no one tower can guard the whole basin.
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const double dx = x - cx, dy = y - cy;
			const double d = std::hypot(dx, dy);
			bool basin = false;
			// The shoreline terms total at most 24 tiles. Outside that bound the
			// angle cannot affect terrain; avoid five trig calls per distant tile.
			if (d < shoreRadius + 24)
			{
				const double a = std::atan2(dy, dx);
				const double edge = shoreRadius + shoreBend(a) + 15 + 2 * std::sin(3 * a + phase) +
									2 * std::sin(5 * a - phase);
				basin = d < edge;
			}
			if (basin || (d > homeRadius + 36 && country.at(x, y) > .63))
				L.terrain[t.at(x, y)] = SAND;
		}
	// One grove between each pair of neighbouring home guides. Organic lobes, lake
	// bites and rotations vary; positional budgets keep every grove in shared reach.
	for (int k = 0; k < r.nbTeams; ++k)
	{
		const double guide = heading + (k + .5) * 2 * kPi / r.nbTeams;
		const double a =
			guide + .04 * std::sin(2 * guide + phase) + .02 * std::sin(3 * guide - phase);
		const AxisFrame frame{cx, cy, a};
		const double radius = shoreRadius + shoreBend(a);
		const ShapePoint p = frame.at(radius, 0);
		const int groveForm = c.bounded("treeline-grove-form", 3);
		const double tail = .3 + .15 * groveForm;
		const double length[] = {2.2, 2.6, 3.0}, width[] = {2.0, 1.8, 1.55};
		const Teardrop outline{length[groveForm] * bankDepth, width[groveForm] * bankDepth + 2,
							   tail};
		c.telemetry.measure("treeline.grove.form", groveForm, k);
		std::vector<int> corners;
		forEachTileInTeardrop(
			t, p.x, p.y, a + kPi / 2, Teardrop{outline.length, outline.width * 1.25, tail},
			[&](int i, double along, double across)
			{
				if (std::abs(across) <
					outline.halfWidthAt(along) + 4 * (bankEdge.at(i % t.w, i / t.w) - .5))
					corners.push_back(i);
			});
		Patch grove{p, stampContainedPlot(L.terrain, t, corners), WOOD};
		// Sloughs protrude into the lakebed: a wooded bank, not a pond ring.
		const ShapePoint pool = frame.at(radius - 8. * bankDepth / 14, (form - 1) * 2);
		const double waterLength[] = {2.1, 2.45, 2.8}, waterWidth[] = {1.15, 1., .9};
		const Teardrop water{waterLength[groveForm] * bankDepth, waterWidth[groveForm] * bankDepth,
							 tail};
		forEachTileInTeardrop(
			t, pool.x, pool.y, a + kPi / 2, Teardrop{water.length, water.width * 1.3, tail},
			[&](int i, double along, double across)
			{
				if (std::abs(across) <
					water.halfWidthAt(along) + 4 * (waterEdge.at(i % t.w, i / t.w) - .5))
					L.terrain[i] = WATER;
			});
		L.patches.push_back(std::move(grove));
		for (int side : {-1, 1})
		{
			const ShapePoint court = frame.at(radius + o.depth + 15, side * 11);
			L.courts.push_back(court);
			// A soft grass shoulder outside the sand margin, never inside a forest.
			RadialShape shoulder(9, .1, c, "treeline-shoulders");
			fillShape(L.terrain, t, court.x, court.y, shoulder, 0, GRASS);
		}
		const double hg = heading + k * 2 * kPi / r.nbTeams;
		const double ha = hg + .04 * std::sin(2 * hg + phase) + .02 * std::sin(3 * hg - phase);
		L.homes.push_back(polarPoint(cx, cy, homeRadius + shoreBend(ha), ha));
	}
	for (const ShapePoint h : L.homes)
		for (int dy = -8; dy <= 8; ++dy)
			for (int dx = -8; dx <= 8; ++dx)
				if (dx * dx + dy * dy <= 64)
					L.terrain[t.at(int(h.x) + dx, int(h.y) + dy)] = GRASS;
	dealStarts(c, L.homes);
	// Food is a contained, broad crescent around a pond, with the swarm outside
	// its sand margin. Farm forms vary once per map; no prebuilt outposts are granted.
	for (const ShapePoint h : L.homes)
	{
		const double a = std::atan2(h.y - cy, h.x - cx);
		const AxisFrame frame{h.x, h.y, a};
		const ShapePoint farm = frame.at(17, 0);
		const Teardrop field{36. + 2 * form, 26., .35 + .1 * form};
		std::vector<int> corners;
		forEachTileInTeardrop(t, farm.x, farm.y, a + kPi / 2,
							  Teardrop{field.length, field.width * 1.2, field.headShare},
							  [&](int i, double along, double across)
							  {
								  if (std::abs(across) <
									  field.halfWidthAt(along) +
										  4 * (bankEdge.at(i % t.w, i / t.w) - .5))
									  corners.push_back(i);
							  });
		L.patches.push_back({farm, stampContainedPlot(L.terrain, t, corners), WHEAT});
		const ShapePoint pool = frame.at(25, form - 1);
		const Teardrop pond{32. + 2 * form, 18., .4 + .05 * form};
		forEachTileInTeardrop(t, pool.x, pool.y, a + kPi / 2,
							  Teardrop{pond.length, pond.width * 1.25, pond.headShare},
							  [&](int i, double along, double across)
							  {
								  if (std::abs(across) <
									  pond.halfWidthAt(along) +
										  4 * (waterEdge.at(i % t.w, i / t.w) - .5))
									  L.terrain[i] = WATER;
							  });
	}
	layBeaches(L.terrain, t);
	const auto grass = pureTiles(L.terrain, t, GRASS);
	for (size_t k = 0; k < L.patches.size(); ++k)
	{
		auto &tiles = L.patches[k].tiles;
		tiles.erase(std::remove_if(tiles.begin(), tiles.end(), [&](int i) { return !grass[i]; }),
					tiles.end());
		for (int i : tiles)
			L.plotOf[i] = int(k);
	}
	// Fit outposts to the actual bank clearings after neighbouring farms have been
	// drawn. Their nominal guide can meet a farm on a crowded, bent shoreline.
	auto outpostGround = grass;
	for (int i = 0; i < t.size(); ++i)
		outpostGround[i] = grass[i] && L.plotOf[i] < 0;
	for (const ShapePoint h : L.homes)
		for (int dy = -8; dy <= 8; ++dy)
			for (int dx = -8; dx <= 8; ++dx)
				outpostGround[t.at(int(h.x) + dx, int(h.y) + dy)] = 0;
	auto outpostAnchors = buildAnchors(t, outpostGround, 8);
	for (ShapePoint &q : L.courts)
	{
		ShapePoint chosen = q;
		int best = std::numeric_limits<int>::max();
		for (int dy = -10; dy <= 10; ++dy)
			for (int dx = -10; dx <= 10; ++dx)
			{
				const int x = int(q.x) + dx, y = int(q.y) + dy;
				if (!outpostAnchors[t.at(x - 4, y - 4)])
					continue;
				const int score = dx * dx + dy * dy;
				if (score < best)
				{
					best = score;
					chosen = {double(t.x(x)), double(t.y(y))};
				}
			}
		if (best == std::numeric_limits<int>::max())
		{
			L.failure = "No outpost clearing fits this shoreline.";
			return L;
		}
		q = chosen;
		// Keep a second outpost independent, including its upgrade margins.
		for (int dy = -7; dy <= 7; ++dy)
			for (int dx = -7; dx <= 7; ++dx)
				outpostAnchors[t.at(int(q.x) - 4 + dx, int(q.y) - 4 + dy)] = 0;
	}
	c.telemetry.measure("treeline.basin.home-radius", homeRadius);
	c.telemetry.measure("treeline.basin.shore-radius", shoreRadius);
	c.telemetry.measure("treeline.woodland.depth", o.depth);
	c.telemetry.measure("treeline.form", form);
	return L;
}
bool generate(Game &game, GenerationContext &c)
{
	c.stage = "treeline landscape";
	const Layout L = design(c.request, c);
	if (!L.failure.empty())
	{
		c.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	const LastTreelineOptions o(c.request);
	writeUndermap(game.map, L.terrain);
	for (int k = 0; k < c.request.nbTeams; ++k)
		game.addTeam();
	c.stage = "treeline colonies";
	if (!settleColonies(
			game, c, "treeline-settlements",
			[&](int k)
			{
				std::vector<unsigned char> mask(t.size(), 0);
				for (int dy = -5; dy <= 5; ++dy)
					for (int dx = -5; dx <= 5; ++dx)
					{
						int i = t.at(int(L.homes[k].x) + dx, int(L.homes[k].y) + dy);
						mask[i] = game.map.isGrass(i % t.w, i / t.w) && L.plotOf[i] < 0;
					}
				return mask;
			},
			[&](int k) { return MapGeneratorPoint{int(L.homes[k].x) - 2, int(L.homes[k].y) - 2}; }))
		return false;
	c.stage = "treeline supplies";
	const auto fertility = cropGrowthField(L.terrain, t);
	for (size_t k = 0; k < L.patches.size(); ++k)
	{
		const Patch &p = L.patches[k];
		const int floor = p.type == WOOD ? 40 : kHomeWheat;
		const int extra = p.type == WOOD ? int(p.tiles.size()) / 5 : 32;
		const int wanted = floor + int(scaledCount(extra, p.type == WOOD ? o.wood : o.wheat));
		int nearby = 0;
		if (p.type == WHEAT)
		{
			const Team *home = game.teams[k - c.request.nbTeams];
			const auto eligible = [&](int i)
			{
				return L.plotOf[i] == int(k) && fertility.at(i % t.w, i / t.w) > 0 &&
					   clearGround(game.map, i % t.w, i / t.w);
			};
			const int seed = seedNear(t, home->startPosX + 2, home->startPosY + 2, 18, eligible);
			if (seed >= 0)
				nearby = growPatch(game.map, t, seed, WHEAT, 24, eligible);
		}
		if (p.type == WOOD)
		{
			// Seed both ends of each bank before filling its fertile core. Initial
			// access should not depend on which end happens to have deeper water.
			std::vector<std::pair<int, int>> neighbours;
			for (int home = 0; home < c.request.nbTeams; ++home)
				neighbours.push_back(
					{t.dist2(game.teams[home]->startPosX + 2, game.teams[home]->startPosY + 2,
							 int(p.centre.x), int(p.centre.y)),
					 home});
			std::sort(neighbours.begin(), neighbours.end());
			for (int side = 0; side < 2; ++side)
			{
				const Team *home = game.teams[neighbours[side].second];
				const auto eligible = [&](int i)
				{
					return L.plotOf[i] == int(k) && fertility.at(i % t.w, i / t.w) > 0 &&
						   clearGround(game.map, i % t.w, i / t.w);
				};
				const int seed =
					seedNear(t, home->startPosX + 2, home->startPosY + 2, 72, eligible);
				if (seed >= 0)
					nearby += growPatch(game.map, t, seed, WOOD, 8, eligible);
			}
		}
		const int planted = nearby + plantContainedPlot(game.map, t, p.tiles, fertility, p.type,
														wanted - nearby, true);
		c.telemetry.measure("treeline.patch.tiles", p.tiles.size(), int(k));
		c.telemetry.measure("treeline.patch.planted", planted, int(k));
		if (planted < floor)
		{
			c.detail = "A shoreline grove or home farm lacks renewable seed stock.";
			return false;
		}
		if (planted < wanted)
			c.telemetry.fallback("treeline.patch.saturated", "Productive ground filled", int(k));
		if (c.telemetry.enabled())
		{
			double yield = 0;
			for (int i : p.tiles)
				yield += fertility.at(i % t.w, i / t.w) / double(Fertility::kScale);
			c.telemetry.measure("treeline.patch.growth-potential", yield, int(k));
		}
	}
	// Finite wood is budgeted in harvested tiles, not visual tree size: wood is
	// nongranular and each collection clears its tile. No ambient timber elsewhere.
	for (int k = 0; k < c.request.nbTeams; ++k)
	{
		const int sx = game.teams[k]->startPosX + 2, sy = game.teams[k]->startPosY + 2;
		std::vector<std::pair<int, int>> dry, quarry;
		for (int dy = -30; dy <= 30; ++dy)
			for (int dx = -30; dx <= 30; ++dx)
			{
				const int i = t.at(sx + dx, sy + dy), d = dx * dx + dy * dy;
				bool court = false;
				for (const ShapePoint q : L.courts)
					court |= t.chebyshev(i % t.w, i / t.w, int(q.x), int(q.y)) < 7;
				if (court || d < 9 * 9 || d > 30 * 30 || L.plotOf[i] >= 0 ||
					!clearGround(game.map, i % t.w, i / t.w))
					continue;
				if (fertility.at(i % t.w, i / t.w) == 0)
					dry.push_back({d, i});
				if (d >= 17 * 17)
					quarry.push_back({d, i});
			}
		std::sort(dry.begin(), dry.end());
		if (int(dry.size()) < kHomeWood)
		{
			c.detail = "A home lacks dry ground for finite opening timber.";
			return false;
		}
		// Group the reserve into a copse on the nearest dry flank, leaving town clear.
		const int root = dry.front().second;
		std::stable_sort(dry.begin(), dry.end(),
						 [&](const auto &a, const auto &b)
						 {
							 return t.dist2(a.second % t.w, a.second / t.w, root % t.w,
											root / t.w) <
									t.dist2(b.second % t.w, b.second / t.w, root % t.w, root / t.w);
						 });
		for (int n = 0; n < kHomeWood; ++n)
			game.map.setResource(dry[n].second % t.w, dry[n].second / t.w, WOOD, 1);
		std::sort(quarry.begin(), quarry.end());
		const auto firstFree =
			std::find_if(quarry.begin(), quarry.end(), [&](const auto &entry)
						 { return clearGround(game.map, entry.second % t.w, entry.second / t.w); });
		if (firstFree != quarry.end())
		{
			const int centre = firstFree->second;
			std::stable_sort(
				quarry.begin(), quarry.end(),
				[&](const auto &a, const auto &b)
				{
					return t.dist2(a.second % t.w, a.second / t.w, centre % t.w, centre / t.w) <
						   t.dist2(b.second % t.w, b.second / t.w, centre % t.w, centre / t.w);
				});
		}
		int stones = 0, fruits = 0;
		const int wantedStone = 4 + int(scaledCount(8, o.stone));
		const int wantedFruit = int(scaledCount(6, o.fruit));
		for (const auto &entry : quarry)
		{
			int i = entry.second;
			if (!clearGround(game.map, i % t.w, i / t.w))
				continue;
			if (stones < wantedStone)
			{
				game.map.setResource(i % t.w, i / t.w, STONE, 1);
				++stones;
			}
			else if (fruits < wantedFruit)
			{
				game.map.setResource(i % t.w, i / t.w, CHERRY + fruits % 3, 1);
				++fruits;
			}
			else
				break;
		}
		c.telemetry.measure("treeline.home.finite-wood", kHomeWood, k);
		c.telemetry.measure("treeline.home.stone", stones, k);
		c.telemetry.measure("treeline.home.fruit", fruits, k);
	}
	seedAlgae(game.map, c, t, "treeline-algae", o.algae, AlgaeBand::anyWater(25));
	return true;
}
std::string validateWorld(const Game &game, const GenerationContext &c)
{
	GenerationContext replay(c.request);
	const Layout L = design(c.request, replay);
	if (auto e = designMismatch(L, game.map, "The Last Treeline"); !e.empty())
		return e;
	const Map &map = game.map;
	const Torus &t = L.t;
	// Read the FINISHED terrain, so a late edit cannot silently irrigate home wood.
	TerrainSketch actual(t.size());
	for (int i = 0; i < t.size(); ++i)
		actual[i] = TerrainType(map.getUMTerrain(i % t.w, i / t.w));
	const auto fertility = cropGrowthField(actual, t);
	if (auto e = containedPlotsMismatch(map, t, L.plotOf, &fertility); !e.empty())
		return e;
	const auto connected =
		walkFromFirstColony(map, c.request.nbTeams, "the treeline basin", "without swimming");
	if (!connected.error.empty())
		return connected.error;
	std::vector<unsigned char> open(t.size(), 0), future(t.size(), 0);
	std::vector<std::vector<int>> faces(c.request.nbTeams);
	for (int i = 0; i < t.size(); ++i)
	{
		open[i] = stepCost(map, i % t.w, i / t.w, StepCosts::walking()) >= 0;
		future[i] = open[i] && L.plotOf[i] < 0;
		int type = map.getResource(i % t.w, i / t.w).type;
		if ((type == WOOD || type == WHEAT) && L.plotOf[i] >= 0 &&
			L.patches[L.plotOf[i]].type != type)
			return "Wood and wheat must remain in separate growing areas.";
	}
	for (int i = 0; i < t.size(); ++i)
		if (map.getResource(i % t.w, i / t.w).type == WOOD && L.plotOf[i] >= 0)
			for (const auto &step : kCardinalSteps)
			{
				const int n = t.at(i % t.w + step[0], i / t.w + step[1]);
				if (open[n])
					faces[L.plotOf[i]].push_back(n);
			}
	const auto buildable = potentialBuildingTiles(map);
	const auto anchors = buildAnchors(t, buildable, 8);
	// The entire crop footprint may fill. Check the broad route network and both
	// gathering flanks against that future, not only against today's scattered trees.
	const auto broad = erode(t, future, 1);
	const auto regions = connectedRegions(broad, t.w, t.h, true);
	int common = -1;
	for (const ShapePoint q : L.courts)
	{
		const int centre = t.at(int(q.x), int(q.y));
		if (common < 0)
			common = regions[centre];
		if (common < 0 || regions[centre] != common)
			return "Woodland outposts lack a connected three-tile-wide future approach.";
		if (!anchors[t.at(int(q.x) - 4, int(q.y) - 4)])
			return "A woodland outpost lost its eight by eight construction clearing.";
	}
	std::vector<unsigned char> frontierSources(t.size(), 0);
	for (const ShapePoint q : L.courts)
		frontierSources[t.at(int(q.x), int(q.y))] = 1;
	const auto futureReach = stepsFrom(t, frontierSources, future);
	for (int g = 0; g < c.request.nbTeams; ++g)
	{
		const Patch &p = L.patches[g];
		int frontage[2] = {0, 0};
		double yield = 0;
		const double a = std::atan2(p.centre.y - t.h / 2., p.centre.x - t.w / 2.);
		for (int i : p.tiles)
		{
			yield += fertility.at(i % t.w, i / t.w) / double(Fertility::kScale);
			bool edge = false;
			for (const auto &step : kCardinalSteps)
			{
				const int n = t.at(i % t.w + step[0], i / t.w + step[1]);
				edge |= future[n] && futureReach[n] >= 0;
			}
			if (edge)
			{
				const double tangent =
					-(i % t.w - p.centre.x) * std::sin(a) + (i / t.w - p.centre.y) * std::cos(a);
				++frontage[tangent > 0];
			}
		}
		if (yield < 12 || std::min(frontage[0], frontage[1]) < 8)
			return "A woodland lacks productive growing room or two gathering edges (" +
				   std::to_string(g) + ": " + std::to_string(yield) + "," +
				   std::to_string(frontage[0]) + "," + std::to_string(frontage[1]) + ").";
	}
	for (size_t k = c.request.nbTeams; k < L.patches.size(); ++k)
	{
		double yield = 0;
		for (int i : L.patches[k].tiles)
			yield += fertility.at(i % t.w, i / t.w) / double(Fertility::kScale);
		if (yield < 25)
			return "A home farm lacks sustainable wheat growing room.";
	}
	std::vector<std::vector<int>> reaches(c.request.nbTeams);
	std::vector<int> first;
	for (int k = 0; k < c.request.nbTeams; ++k)
	{
		const int sx = game.teams[k]->startPosX, sy = game.teams[k]->startPosY;
		std::vector<unsigned char> sources(t.size(), 0);
		for (int dy = -1; dy <= 4; ++dy)
			for (int dx = -1; dx <= 4; ++dx)
			{
				const int i = t.at(sx + dx, sy + dy);
				if (open[i])
					sources[i] = 1;
			}
		bool futureExit = false;
		for (int dy = -1; dy <= 4; ++dy)
			for (int dx = -1; dx <= 4; ++dx)
				futureExit |= futureReach[t.at(sx + dx, sy + dy)] >= 0;
		if (!futureExit)
			return "A colony cannot reach the woodland network after crops fill.";
		const auto d = stepsFrom(t, sources, open);
		int room = 0, food = 0, earlyFood = 0, timber = 0;
		for (int i = 0; i < t.size(); ++i)
		{
			if (d[i] >= 0 && d[i] <= 24 && anchors[i])
				++room;
			const int type = map.getResource(i % t.w, i / t.w).type;
			if (type != WHEAT && type != WOOD)
				continue;
			int distance = t.size();
			for (const auto &step : kCardinalSteps)
			{
				int n = t.at(i % t.w + step[0], i / t.w + step[1]);
				if (d[n] >= 0)
					distance = std::min(distance, d[n]);
			}
			if (type == WHEAT && distance <= 24 && fertility.at(i % t.w, i / t.w) > 0)
				++food;
			if (type == WHEAT && distance <= 12 && fertility.at(i % t.w, i / t.w) > 0)
				++earlyFood;
			if (type == WOOD && L.plotOf[i] < 0 && distance <= 32)
				++timber;
		}
		if (room < 16 || food < 12 || earlyFood < 8 || timber < 8)
			return "A treeline home lacks building room or accessible opening supplies (" +
				   std::to_string(k) + ": " + std::to_string(room) + "," + std::to_string(food) +
				   "," + std::to_string(earlyFood) + "," + std::to_string(timber) + ").";
		std::vector<int> choices;
		for (int g = 0; g < c.request.nbTeams; ++g)
		{
			int best = t.size();
			for (int i : faces[g])
				if (d[i] >= 0)
					best = std::min(best, d[i]);
			reaches[g].push_back(best);
			choices.push_back(best);
		}
		std::sort(choices.begin(), choices.end());
		if (choices[1] > 72)
			return "A colony cannot reach two woodland fronts within 72 steps.";
		first.push_back(choices.front());
	}
	for (auto &reach : reaches)
	{
		std::sort(reach.begin(), reach.end());
		if (reach[1] > 72 || reach[1] - reach[0] > 28)
			return "A shoreline grove lacks competing access from two colonies.";
	}
	if (*std::max_element(first.begin(), first.end()) >
		*std::min_element(first.begin(), first.end()) + 18)
		return "First woodland access differs by more than 18 steps.";
	return "";
}
} // namespace
LastTreelineOptions::LastTreelineOptions(const GenerationRequest &r)
	: depth(r.option("woodland-depth")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition lastTreelineDefinition()
{
	return {"last-treeline",
			70,
			"The Last Treeline",
			1,
			false,
			{{"woodland-depth", "Woodland depth", 12, 16, 2, 14, ControlGroup::Layout},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validateRequest,
			validateWorld,
			{"terrain:natural", "feature:forest", "feature:lakes", "feature:desert",
			 "style:contested-center", "style:wide-open"}};
}
