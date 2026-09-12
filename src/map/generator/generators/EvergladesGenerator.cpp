// SPDX-License-Identifier: GPL-3.0-or-later
#include "EvergladesGenerator.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "HeightMap.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Unit.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Everglades: a dense wetland, the same everywhere and across the wrap. Pools and long sloughs lie
// on a jittered lattice that tiles the torus, bigger where the land is wet and smaller where it is
// dry, and the grass between them starts thick with wood and wheat. Every tile is a few steps from
// water, so wood and wheat grow back at the engine's top rate; resources block movement, so the
// swamp grows shut unless workers keep cutting it. Nothing grows on sand, and every pool's beach is
// sand, so the banks between pools are the lanes that stay open, winding and narrow. Every colony
// starts in a clearing with a pond and a kit, ringed by a sand levee with gaps the swamp creeps
// through; what lies beyond is held only while it is kept cut.
//
// Nothing here is symmetric on purpose: the landscape is one random field over the whole map and
// the clearings are nudged off their ring, so it reads as country, not as an arena. Fairness is
// statistical, and the lobby keeps the best-scoring of several seeds. The design is a pure
// function of the request, so validateWorld rebuilds it and checks the finished world.
namespace
{

// The clearings sit near a ring at this share of the half side, evenly spaced from a random start,
// each nudged by up to these shares of a wedge and of the ring's radius.
constexpr double kHomeRing = 0.58;
// On a crowded map the ring widens up to this share of the half side before the request is refused.
constexpr double kHomeRingMax = 0.76;
constexpr double kHomeJitterTurn = 0.12;
constexpr double kHomeJitterRadius = 0.08;
// The clearings' outlines wobble by up to this share of their radius. A clearing that would not fit
// its ring shrinks down to this radius before the request is refused.
constexpr double kClearingRoughness = 0.3;
constexpr int kMinimumClearing = 6;
// A clearing smaller than this has no pond: the swamp's pools are a few steps away anyway.
constexpr int kPondClearing = 9;
// A pond up to this big sits in every clearing (smaller in a shrunken clearing); the levee is this
// wide, and no pool water lies within this distance beyond it, so the gaps open onto grass.
constexpr double kPondRadius = 3.0;
constexpr double kLeveeWidth = 2.0;
constexpr double kLeveeClearance = 2.0;
constexpr int kLeveeSectors = 36;
// Every home starts identical: wheat and wood beside its pond, a quarry, all unscaled.
constexpr int kHomeWheat = 40;
constexpr int kHomeWood = 30;
// The kit keeps this far from the swarm beyond the clearance, so there is room to build beside it.
constexpr int kBuildRoom = 4;
// The swamp's standing wood and wheat, as shares of the grass off the clearings, at 100.
constexpr int kWoodShare = 55;
constexpr int kWheatShare = 15;
// A slough is a pool this much bigger, stretched this much along a random line.
constexpr double kSloughGrowth = 1.4;
constexpr double kSloughStretch = 2.2;
// Pools scale from this at the driest ground to this at the wettest.
constexpr double kDryScale = 0.6;
constexpr double kWetScale = 1.5;
// The most of the map the pools may be expected to cover, before the beaches; pools that would
// cover more are scaled down to this, since beyond it the colonies can hardly reach each other.
constexpr double kMaximumCoverage = 0.6;
// Opening a route for a boxed-in colony: the cost of a step onto open ground, onto a deposit that
// will be cleared, onto stone or fruit, and onto water that becomes a ford.
constexpr int kStepOpen = 1, kStepDeposit = 3, kStepEternal = 8, kStepFord = 25;

struct Geometry
{
	int teams, half, clearing, buildRoom;
	double spacing, poolRadius, sloughs, homeRing, levee, pond, clearance;
	double reach, jitterTurn, jitterRadius; // a clearing's fullest extent; how far homes are nudged
	double coverage;                        // the share of the map the pools are expected to cover
};

// The share of the map the pools cover, expected: the mean pool area (radius scaled by wetness,
// the random factor and the rough outline, stretched; sloughs grown and stretched further) over
// the cell area, with overlaps discounted.
double expectedCoverage(const Geometry &g)
{
	const double meanScale = (kDryScale + kWetScale) / 2;
	const double radius2 = g.poolRadius * g.poolRadius * meanScale * meanScale * 1.1;
	const double pool = kPi * radius2 * 1.2;
	const double slough = pool * kSloughGrowth * kSloughGrowth * kSloughStretch / 1.2;
	const double nominal = ((1 - g.sloughs) * pool + g.sloughs * slough) / (g.spacing * g.spacing);
	return 1 - std::exp(-nominal);
}

// Whether every clearing, at its fullest reach, fits between its neighbours and inside the wrap
// with the homes nudged this far.
bool clearingsFit(const Geometry &g)
{
	const double nearest =
		2 * g.homeRing * (1 - g.jitterRadius) * std::sin(kPi * (1 - 2 * g.jitterTurn) / g.teams);
	const bool fitsRing = g.teams < 2 || nearest >= 2 * g.reach;
	const bool fitsWrap = g.homeRing * (1 + g.jitterRadius) + g.reach <= g.half - 1;
	return fitsRing && fitsWrap;
}

Geometry geometryFor(const GenerationRequest &r)
{
	const EvergladesOptions o(r);
	Geometry g;
	g.teams = std::max(1, r.nbTeams);
	g.half = std::min(1 << r.wDec, 1 << r.hDec) / 2;
	g.clearing = o.clearingSize;
	g.spacing = o.poolSpacing;
	g.poolRadius = o.poolSize;
	g.sloughs = o.sloughs / 100.0;
	g.homeRing = kHomeRing * g.half;
	g.levee = o.levee / 100.0;
	// Nudge the homes as far as the ring allows, down to not at all on a crowded ring; when even
	// that does not fit, the clearings shrink until it does, then the grass beyond the levee and
	// the margin go, then the ring widens. Only then is the request refused.
	g.clearance = kLeveeClearance;
	double margin = 2;
	for (;;)
	{
		g.reach = g.clearing * (1 + kClearingRoughness) + kLeveeWidth + g.clearance + margin;
		for (int step = 4; step >= 0; --step)
		{
			g.jitterTurn = kHomeJitterTurn * step / 4;
			g.jitterRadius = kHomeJitterRadius * step / 4;
			if (clearingsFit(g))
				break;
		}
		if (clearingsFit(g))
			break;
		if (g.clearing > kMinimumClearing)
			--g.clearing;
		else if (g.clearance > 0 || margin > 0)
			g.clearance = margin = 0;
		else if (g.homeRing + 0.02 * g.half <= kHomeRingMax * g.half)
			g.homeRing += 0.02 * g.half;
		else
			break;
	}
	g.pond = g.clearing >= kPondClearing ? std::clamp(g.clearing * 0.23, 2.0, kPondRadius) : 0.0;
	g.buildRoom = std::clamp(g.clearing - 5, 0, kBuildRoom);
	// Pools that would drown the map are scaled down to the wettest playable coverage.
	g.coverage = expectedCoverage(g);
	if (g.coverage > kMaximumCoverage)
	{
		const double wanted = -std::log(1 - kMaximumCoverage), now = -std::log(1 - g.coverage);
		g.poolRadius *= std::sqrt(wanted / now);
		g.coverage = expectedCoverage(g);
	}
	return g;
}

// A pool: a rough disc stretched along a line through its centre.
struct Pool
{
	double x, y, aspect, spin, reach;
	RadialShape shape;
	bool holds(double dx, double dy) const
	{
		if (std::abs(dx) > reach || std::abs(dy) > reach)
			return false;
		const double u = (dx * std::cos(spin) + dy * std::sin(spin)) / aspect;
		const double v = -dx * std::sin(spin) + dy * std::cos(spin);
		return std::hypot(u, v) < shape.radiusAt(std::atan2(v, u));
	}
};

struct Home
{
	int x, y;
	RadialShape outline, pond;
	std::array<unsigned char, kLeveeSectors> standing;
};

struct Layout
{
	Torus t{1, 1};
	Geometry g{};
	int cx = 0, cy = 0;
	std::vector<Pool> pools;
	std::vector<Home> homes;
	std::vector<unsigned char> water, sand; // per tile
	std::vector<int> clearingOf;            // clearing tiles: the colony, else -1
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	L.g = geometryFor(request);
	const Torus &t = L.t;
	const Geometry &g = L.g;
	const int n = t.w * t.h, teams = g.teams;
	L.cx = t.w / 2;
	L.cy = t.h / 2;
	const auto unit = [&](const char *stream)
	{ return context.bounded(stream, 2001) / 1000.0 - 1; };

	// The homes: near a ring, evenly spaced from a random start, each nudged a little.
	const double phase = context.bounded("glades-layout", 3600) / 3600.0 * 2 * kPi;
	const double wedge = 2 * kPi / teams;
	for (int k = 0; k < teams; ++k)
	{
		const double a = phase + wedge * (k + 0.5 + g.jitterTurn * unit("glades-layout"));
		const double r = g.homeRing * (1 + g.jitterRadius * unit("glades-layout"));
		Home home{t.x(L.cx + int(std::lround(r * std::cos(a)))),
				  t.y(L.cy + int(std::lround(r * std::sin(a)))),
				  RadialShape(g.clearing, kClearingRoughness, context, "glades-clearings"),
				  RadialShape(std::max(g.pond, 1.0), 0.3, context, "glades-ponds"),
				  {}};
		// The levee round the clearing: a share of its sectors stand, the rest are gaps.
		home.standing.fill(1);
		std::array<int, kLeveeSectors> sectors{};
		for (int s = 0; s < kLeveeSectors; ++s)
			sectors[s] = s;
		context.shuffle(sectors.begin(), sectors.end(), "glades-levee");
		const int open = int(std::lround((1 - g.levee) * kLeveeSectors));
		for (int i = 0; i < open; ++i)
			home.standing[sectors[i]] = 0;
		L.homes.push_back(home);
	}

	// The pools: one per cell of a lattice that tiles the torus exactly, jittered, sized by a smooth
	// wetness field so the country has wet reaches and dry ones, some grown and stretched into sloughs.
	HeightMap wet(t.w, t.h, context.stream("glades-wet"));
	wet.makePlain(12);
	const int nx = std::max(1, int(std::lround(t.w / g.spacing)));
	const int ny = std::max(1, int(std::lround(t.h / g.spacing)));
	const double cellW = double(t.w) / nx, cellH = double(t.h) / ny;
	for (int j = 0; j < ny; ++j)
		for (int i = 0; i < nx; ++i)
		{
			const double px = (i + 0.5) * cellW + unit("glades-pools") * cellW / 3;
			const double py = (j + 0.5) * cellH + unit("glades-pools") * cellH / 3;
			const float wetness =
				std::clamp(wet(unsigned(t.x(int(px))), unsigned(t.y(int(py)))), 0.0f, 1.0f);
			const bool slough = context.bounded("glades-pools", 1000) < g.sloughs * 1000;
			const double radius =
				g.poolRadius * (0.7 + context.bounded("glades-pools", 601) / 1000.0) *
				(kDryScale + (kWetScale - kDryScale) * wetness) * (slough ? kSloughGrowth : 1);
			RadialShape shape(radius, 0.45, context, "glades-pools");
			const double aspect =
				slough ? kSloughStretch : 1 + context.bounded("glades-pools", 401) / 1000.0;
			const double spin = context.bounded("glades-pools", 3600) / 3600.0 * kPi;
			L.pools.push_back({px, py, aspect, spin, shape.maximumRadius() * aspect + 1, shape});
		}

	L.water.assign(n, 0);
	L.sand.assign(n, 0);
	L.clearingOf.assign(n, -1);
	for (const Pool &p : L.pools)
	{
		const int r = int(std::ceil(p.reach));
		const int x0 = int(std::floor(p.x)), y0 = int(std::floor(p.y));
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx)
				if (p.holds(x0 + dx - p.x, y0 + dy - p.y))
					L.water[t.at(x0 + dx, y0 + dy)] = 1;
	}
	// The clearings, cut out of the swamp: grass with a pond, the levee, and grass just beyond it.
	for (int k = 0; k < teams; ++k)
	{
		const Home &home = L.homes[k];
		const int r = int(std::ceil(home.outline.maximumRadius() + kLeveeWidth + g.clearance)) + 1;
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx)
			{
				const int i = t.at(home.x + dx, home.y + dy);
				const double d = std::hypot(double(dx), double(dy)),
							 a = std::atan2(double(dy), double(dx));
				const double edge = home.outline.radiusAt(a);
				if (d < edge)
				{
					L.clearingOf[i] = k;
					L.water[i] = g.pond > 0 && d < home.pond.radiusAt(a);
					L.sand[i] = 0;
				}
				else if (d < edge + kLeveeWidth)
				{
					const int sector =
						int(std::fmod(a + 2 * kPi, 2 * kPi) / (2 * kPi) * kLeveeSectors) %
						kLeveeSectors;
					L.water[i] = 0;
					L.sand[i] = home.standing[sector];
				}
				else if (d < edge + kLeveeWidth + g.clearance)
				{
					L.water[i] = 0;
				}
			}
	}
	return L;
}

// Every home's kit, identical and unscaled: wheat and wood beside the pond on the sides, a quarry
// on the near side towards the map's centre. The rest of the clearing starts clear.
void furnishHomes(Map &map, const Layout &L, GenerationContext &context)
{
	const Torus &t = L.t;
	const std::vector<unsigned char> reserved =
		swarmSurroundings(t, context, kSwarmClearance + L.g.buildRoom);
	for (int team = 0; team < L.g.teams; ++team)
	{
		const Home &home = L.homes[team];
		const auto eligible = [&](int i)
		{ return L.clearingOf[i] == team && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
		const double a =
			std::atan2(double(t.offsetY(L.cy, home.y)), double(t.offsetX(L.cx, home.x)));
		const auto at = [&](double along, double across)
		{
			return std::make_pair(
				home.x + int(std::lround(along * std::cos(a) - across * std::sin(a))),
				home.y + int(std::lround(along * std::sin(a) + across * std::cos(a))));
		};
		const double beside = L.g.pond * 1.3 + 3;
		if (const auto [x, y] = at(0, -beside); true)
			if (const int seed = seedNear(t, x, y, 10, eligible); seed >= 0)
				growPatch(map, t, seed, CORN, kHomeWheat, eligible);
		if (const auto [x, y] = at(0, beside); true)
			if (const int seed = seedNear(t, x, y, 10, eligible); seed >= 0)
				growPatch(map, t, seed, WOOD, kHomeWood, eligible);
		if (const auto [x, y] = at(-(L.g.clearing - 4), 0); true)
			if (const int seed = seedNear(t, x, y, 8, eligible); seed >= 0)
				placeResourceClump(map, context, {seed % t.w, seed / t.w}, STONE, 2);
	}
}

// The swamp: standing wood and wheat in patches over the grass off the clearings, stone outcrops
// that never grow, and a few fruit groves. Everything will grow from here.
void stockSwamp(Map &map, const Layout &L, GenerationContext &context, const EvergladesOptions &o)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	HeightMap patch(t.w, t.h, context.stream("glades-patch"));
	patch.makePlain(10);
	HeightMap split(t.w, t.h, context.stream("glades-split"));
	split.makePlain(5);
	const auto eligible = [&](int i)
	{ return L.clearingOf[i] < 0 && clearGround(map, i % t.w, i / t.w); };
	std::vector<int> ground;
	std::vector<std::pair<float, int>> byPatch;
	for (int i = 0; i < n; ++i)
		if (eligible(i))
		{
			ground.push_back(i);
			byPatch.push_back({-patch(i % t.w, i / t.w), i});
		}
	if (ground.empty())
		return;
	std::stable_sort(byPatch.begin(), byPatch.end());
	const int area = int(ground.size());
	const int wood = int(scaledCount(std::int64_t(area) * kWoodShare / 100, o.wood));
	const int wheat = int(scaledCount(std::int64_t(area) * kWheatShare / 100, o.wheat));
	const int total = std::min(area, wood + wheat);
	std::vector<int> chosen;
	for (int k = 0; k < total; ++k)
		chosen.push_back(byPatch[k].second);
	std::stable_sort(
		chosen.begin(), chosen.end(), [&](int p, int q)
		{ return split.uiLevel(p % t.w, p / t.w, 2048) < split.uiLevel(q % t.w, q / t.w, 2048); });
	const int wheatShare = int(std::int64_t(total) * wheat / std::max(1, wood + wheat));
	for (int k = 0; k < total; ++k)
		map.setResource(chosen[k] % t.w, chosen[k] / t.w, k < wheatShare ? CORN : WOOD, 1);
	const int outcrops = int(scaledCount(std::max(2, area / 1000), o.stone));
	for (int k = 0; k < outcrops; ++k)
		for (int attempt = 0; attempt < 100; ++attempt)
		{
			const int at = ground[context.bounded("glades-stone", ground.size())];
			if (eligible(at))
			{
				placeResourceClump(map, context, {at % t.w, at / t.w}, STONE,
								   1 + int(context.bounded("glades-stone", 2)));
				break;
			}
		}
	const int groves = int(scaledCount(std::max(3, area / 1500), o.fruit));
	for (int k = 0; k < groves; ++k)
		for (int attempt = 0; attempt < 100; ++attempt)
		{
			const int at = ground[context.bounded("glades-fruit", ground.size())];
			if (eligible(at))
			{
				placeResourceClump(map, context, {at % t.w, at / t.w},
								   CHERRY + int(context.bounded("glades-fruit", 3)), 2);
				break;
			}
		}
}

// Every colony must be able to walk to colony 0 at the start. Where the swamp or the sloughs box
// one in, the cheapest way through is opened: deposits on it are cleared and water on it becomes a
// sand ford. The map may look odd there; it does not fail.
void openRoutes(Game &game, const Layout &L, GenerationContext &context)
{
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.w * t.h, teams = context.request.nbTeams;
	if (teams < 2)
		return;
	const auto openAt = [&](int i)
	{
		const int x = i % t.w, y = i / t.w;
		return !map.isWater(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID;
	};
	const auto costAt = [&](int i)
	{
		const int x = i % t.w, y = i / t.w;
		if (map.getBuilding(x, y) != NOGBID)
			return -1;
		if (map.isWater(x, y))
			return kStepFord;
		if (map.isResource(x, y))
		{
			const int type = map.getResource(x, y).type;
			return type == STONE || (type >= CHERRY && type <= CHERRY + 2) ? kStepEternal
																		   : kStepDeposit;
		}
		return kStepOpen;
	};
	// The open tiles round a colony's swarm.
	const auto doorstep = [&](int team)
	{
		std::vector<int> tiles;
		for (int dy = -1; dy <= 4; ++dy)
			for (int dx = -1; dx <= 4; ++dx)
				if (dy == -1 || dy == 4 || dx == -1 || dx == 4)
				{
					const int i = t.at(context.bootX[team] + dx, context.bootY[team] + dy);
					if (openAt(i))
						tiles.push_back(i);
				}
		return tiles;
	};
	const auto &steps = kCardinalSteps;
	bool changed = false;
	for (int team = 1; team < teams; ++team)
	{
		std::vector<unsigned char> open(n), source(n, 0);
		for (int i = 0; i < n; ++i)
			open[i] = openAt(i);
		for (int i : doorstep(0))
			source[i] = 1;
		const std::vector<int> walk = stepsFrom(t, source, open);
		std::vector<unsigned char> target(n, 0);
		bool arrived = false;
		for (int i : doorstep(team))
		{
			target[i] = 1;
			arrived = arrived || walk[i] >= 0;
		}
		if (arrived)
			continue;
		// The cheapest way from anything colony 0 can reach to this colony's doorstep.
		std::vector<int> cost(n, INT_MAX), from(n, -1);
		std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<>>
			heap;
		for (int i = 0; i < n; ++i)
			if (walk[i] >= 0)
			{
				cost[i] = 0;
				heap.push({0, i});
			}
		int reached = -1;
		while (!heap.empty() && reached < 0)
		{
			const auto [c, i] = heap.top();
			heap.pop();
			if (c > cost[i])
				continue;
			if (target[i])
			{
				reached = i;
				break;
			}
			const int x = i % t.w, y = i / t.w;
			for (const auto &step : steps)
			{
				const int m = t.at(x + step[0], y + step[1]);
				const int stepCost = costAt(m);
				if (stepCost < 0 || c + stepCost >= cost[m])
					continue;
				cost[m] = c + stepCost;
				from[m] = i;
				heap.push({cost[m], m});
			}
		}
		for (int i = reached; i >= 0 && cost[i] > 0; i = from[i])
		{
			const int x = i % t.w, y = i / t.w;
			if (map.isWater(x, y))
			{
				// One sand corner turns this tile and its three other tiles into walkable shore.
				map.setUMTerrain(x, y, SAND);
				for (int dy = -1; dy <= 0; ++dy)
					for (int dx = -1; dx <= 0; ++dx)
						if (map.isResource(t.x(x + dx), t.y(y + dy)))
							map.setNoResource(t.x(x + dx), t.y(y + dy), 1);
				changed = true;
			}
			else if (map.isResource(x, y))
			{
				map.setNoResource(x, y, 1);
			}
		}
		if (changed)
			map.rebuildTerrain();
	}
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "glades layout";
	const EvergladesOptions o(context.request);
	Map &map = game.map;
	const int teams = context.request.nbTeams;
	map.makeHomogenMap(GRASS);
	for (int i = 0; i < teams; ++i)
		game.addTeam();
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	const int n = t.w * t.h;

	context.stage = "glades terrain";
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
	{
		if (L.water[i])
			terrain[i] = WATER;
		else if (L.sand[i])
			terrain[i] = SAND;
	}
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "glades colonies";
	const auto clearing = [&](int team)
	{
		std::vector<unsigned char> ground(n, 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.clearingOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	// The swarm stands just past the pond's beach on the far side from the map's centre, leaving
	// the rest of that side to build on; in a clearing too small for a pond it takes the middle.
	const auto anchor = [&](int team)
	{
		const Home &home = L.homes[team];
		const double a =
			std::atan2(double(t.offsetY(L.cy, home.y)), double(t.offsetX(L.cx, home.x)));
		const double back = L.g.pond > 0 ? L.g.pond * 1.3 + 4 : 0;
		return MapGeneratorPoint(home.x + int(std::lround(back * std::cos(a))) - 2,
								 home.y + int(std::lround(back * std::sin(a))) - 2);
	};
	if (!settleColonies(game, context, "glades-starts", clearing, anchor))
		return false;

	context.stage = "glades resources";
	furnishHomes(map, L, context);
	stockSwamp(map, L, context, o);
	seedAlgae(map, context, t, "glades-algae", o.algae, AlgaeBand::anyWater());
	clearAroundSwarms(map, context, t);
	guaranteeStartingResources(game, context, 24, 32, 0);
	clearAroundSwarms(map, context, t);
	context.stage = "glades routes";
	openRoutes(game, L, context);
	return true;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 1)
		return "Everglades need at least one colony.";
	if (!clearingsFit(geometryFor(r)))
		return "Too many colonies for this map; use a bigger map or fewer colonies.";
	return "";
}

// Checked on the finished world: every clearing keeps its pond, no levee tile carries a deposit
// (nothing grows on sand, so nothing may start there), and colony 0 can walk to every other colony,
// with water, buildings and every resource blocking (openRoutes has already opened any way needed).
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "glades"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int n = t.w * t.h, teams = context.request.nbTeams;
	for (int k = 0; k < teams && L.g.pond > 0; ++k)
	{
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
			for (int dx = -2; dx <= 2 && !pond; ++dx)
				pond = map.isWater(t.x(L.homes[k].x + dx), t.y(L.homes[k].y + dy));
		if (!pond)
			return "Colony " + std::to_string(k) + "'s clearing has lost its pond.";
	}
	for (int i = 0; i < n; ++i)
		if (L.sand[i] && map.isResource(i % t.w, i / t.w))
			return "A deposit stands on the levee at (" + std::to_string(i % t.w) + ", " +
				   std::to_string(i / t.w) + ").";
	return walkFromFirstColony(map, teams, "the glades", "through the glades").error;
}
} // namespace

EvergladesOptions::EvergladesOptions(const GenerationRequest &r)
	: poolSpacing(r.option("pool-spacing")), poolSize(r.option("pool-size")),
	  sloughs(r.option("sloughs")), clearingSize(r.option("clearing-size")),
	  levee(r.option("levee")), wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition evergladesDefinition()
{
	return {
		"everglades",
		19,
		"Everglades",
		2,
		false,
		// Pool spacing and pool size in tiles; sloughs is the share of pools grown and stretched
		// into sloughs; the clearings' radius in tiles; the levee is the share of each clearing's
		// ring that is sand.
		{{"pool-spacing", "Pool spacing", 8, 20, 1, 15, ControlGroup::Terrain},
		 {"pool-size", "Pool size", 2, 7, 1, 4, ControlGroup::Terrain},
		 {"sloughs", "Sloughs", 0, 50, 5, 15, ControlGroup::Terrain},
		 {"clearing-size", "Clearing size", 12, 18, 1, 13, ControlGroup::Layout},
		 {"levee", "Levee", 0, 100, 10, 70, ControlGroup::Layout},
		 // The swamp's standing wood and wheat, its outcrops and groves, and the pools' algae;
		 // every home's kit is unscaled.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		validateRequest,
		validateWorld};
}
