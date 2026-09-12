// SPDX-License-Identifier: GPL-3.0-or-later
#include "TidalFlatsGenerator.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "Geometry.h"
#include "HeightMap.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Unit.h"
#include "Wedge.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Tidal flats: grass islands standing on a wide expanse of walkable sand, with tide pools and
// lagoons of water over the flats and the odd grassy sandbar. Sand carries units freely but holds
// no building and no deposit, and nothing regrows beside it, so there are open-field battles from
// the first minute and no forward bases: an army on the flats fights far from any inn or tower,
// while a defender holds a rim of towers on the island's edge. Expansion means taking another
// island whole. Every colony's island has a pond inland, fields beside it and a quarry; smaller
// islands round the flats carry a prize each, and an island at the centre of the map carries the
// orchard of all three fruits.
//
// Every island, pool and sandbar is designed in the frame of a colony's wedge - arc across it and
// radius out from the centre - and stamped into every wedge alike, so whatever the roll, every
// colony gets the same neighbourhood turned round the centre, and the layout is fair for any
// colony count. The whole design is a pure function of the request, so validateWorld rebuilds
// it and checks the finished world.
namespace
{

// The home islands' centres lie this far out, as a share of the half side, and keep this much sand
// from each other, from the map's wrap and from every other island.
constexpr double kHomeRing = 0.58;
constexpr int kIslandGap = 8;
// Every home starts identical: wheat and wood beside its pond, a quarry, all unscaled.
constexpr int kHomeWheat = 40;
constexpr int kHomeWood = 30;
// Neutral islands' radii as shares of the home islands' (never below a size that holds a pond and
// a field), and the central island's as a share of the half side.
constexpr double kNeutralLow = 0.45, kNeutralHigh = 0.6;
constexpr double kNeutralMinimum = 7.0;
// Every neutral island is an oasis: a pond, and every other tile of it under wheat, so taking one
// means clearing it first.
constexpr double kCentralShare = 0.13;
// Sandbars are just big enough to hold a tower or an inn.
constexpr double kSandbarLow = 3.0, kSandbarHigh = 4.5;

struct Geometry
{
	int teams, half;
	double homeRing, homeRadius, centralRadius, amplitude;
};

Geometry geometryFor(const GenerationRequest &r)
{
	const TidalFlatsOptions o(r);
	Geometry g;
	g.teams = std::max(1, r.nbTeams);
	g.half = std::min(1 << r.wDec, 1 << r.hDec) / 2;
	g.homeRing = kHomeRing * g.half;
	g.centralRadius = o.centralIsland ? std::max(8.0, kCentralShare * g.half) : 0.0;
	g.amplitude = o.coastRoughness / 100.0 * 0.35;
	// The control is the islands' radius as a share of the half side; with many colonies on the
	// ring, or a small map, the islands shrink to what the ring, the wrap and the central island
	// leave room for, and validateRequest refuses a map where that is too small for a base.
	double radius = o.homeIslandSize / 100.0 * g.half;
	const double reach = 1 + g.amplitude;
	if (g.teams >= 2)
		radius =
			std::min(radius, (2 * g.homeRing * std::sin(kPi / g.teams) - kIslandGap) / (2 * reach));
	radius = std::min(radius, (g.half - kIslandGap / 2.0 - g.homeRing) / reach);
	if (g.centralRadius > 0)
		radius = std::min(radius, (g.homeRing - g.centralRadius * 1.3 - kIslandGap) / reach);
	g.homeRadius = std::floor(radius);
	return g;
}

// A feature in a wedge's frame (Blob) and what it is. Shapes are evaluated in the wedge's frame
// too, so every wedge gets the same.
enum Kind
{
	HomeIsland = 0,
	Neutral,
	Central,
	Sandbar,
	Pool,
	Lagoon
};
struct Feature : Blob
{
	Kind kind;
	double reach; // the blob's fullest extent, stretched
	int prize;    // neutral islands: 0 fruit, 1 stone, 2 wheat
};

struct Layout
{
	Torus t{1, 1};
	Geometry g{};
	int cx = 0, cy = 0;
	double phase = 0;
	std::vector<Feature> blobs;                      // in the wedge's frame
	std::vector<int> homeX, homeY;                   // every home island's centre
	std::vector<int> pondX, pondY;                   // and its pond's
	std::vector<unsigned char> grass, water, island; // per tile; island: which home, else -1 below
	std::vector<int> islandOf; // -1 flats, -2 central, -3 neutral or sandbar, k a home
	std::vector<int> prizeOf;  // neutral islands: the prize, else -1
	std::vector<double> radius, angle;
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
	const TidalFlatsOptions o(request);
	L.cx = t.w / 2;
	L.cy = t.h / 2;
	L.phase = context.bounded("flats-layout", 3600) / 3600.0 * 2 * kPi;
	const double wedge = 2 * kPi / teams;
	const double arcHalfAt = [&](double d) { return teams < 2 ? kPi * d : kPi * d / teams; }(1.0);

	// Everything in one wedge, then stamped into every wedge: the home island in the middle of the
	// ring, then neutral islands, sandbars, lagoons and pools that keep clear of each other, of the
	// central island and of the map's wrap.
	std::vector<Feature> &blobs = L.blobs;
	blobs.push_back({{0.0, g.homeRing, 1.0, 0.0,
					  RadialShape(g.homeRadius, g.amplitude, context, "flats-coast")},
					 HomeIsland,
					 0,
					 -1});
	blobs.back().reach = blobs.back().shape.maximumRadius();
	const auto fits = [&](double s0, double r0, double reach, double gap)
	{
		if (r0 - reach < 6 + g.centralRadius * 1.3 + (g.centralRadius > 0 ? gap : 0))
			return false;
		if (r0 + reach > g.half - 4)
			return false;
		if (teams >= 2 && std::abs(s0) + reach + gap / 2 > arcHalfAt * r0)
			return false;
		for (const Feature &b : blobs)
		{
			const double need =
				reach + b.reach + std::max(gap, b.kind == HomeIsland ? double(kIslandGap) : gap);
			if (std::hypot(s0 - b.s, r0 - b.r) < need)
				return false;
		}
		return true;
	};
	const auto scatter = [&](Kind kind, int wanted, double low, double high, double roughness,
							 double gap, const char *stream, bool stretched)
	{
		for (int attempt = 0, placed = 0; attempt < wanted * 80 && placed < wanted; ++attempt)
		{
			const double r0 = 8 + context.bounded(stream, 1000) / 1000.0 * (g.half - 16);
			const double s0 = (context.bounded(stream, 2001) / 1000.0 - 1) * arcHalfAt * r0;
			const double radius = low + context.bounded(stream, 1000) / 1000.0 * (high - low);
			const double stretch = stretched ? 1.2 + context.bounded(stream, 81) / 100.0 : 1.0;
			const double turn = stretched ? context.bounded(stream, 3600) / 3600.0 * kPi : 0.0;
			RadialShape shape(radius, roughness, context, stream);
			const double reach = shape.maximumRadius() * stretch;
			if (!fits(s0, r0, reach, gap))
				continue;
			blobs.push_back({{s0, r0, stretch, turn, shape}, kind, reach, placed % 2});
			++placed;
		}
	};
	scatter(Neutral, o.extraIslands, std::max(kNeutralMinimum, kNeutralLow * g.homeRadius),
			std::max(kNeutralMinimum + 2, kNeutralHigh * g.homeRadius), 0.35, kIslandGap,
			"flats-islands", false);
	scatter(Sandbar, o.sandbars, kSandbarLow, kSandbarHigh, 0.3, 6, "flats-sandbars", false);
	scatter(Lagoon, o.lagoons, 8.0, 14.0, 0.35, 6, "flats-lagoons", true);
	const double wedgeArea = wedge * g.half * g.half / 2;
	scatter(Pool, int(std::lround(o.tidePools * wedgeArea / 16384.0)), 1.5, 4.0, 0.4, 4,
			"flats-pools", false);
	const RadialShape central(std::max(1.0, g.centralRadius), g.amplitude * 0.6, context,
							  "flats-central");

	L.grass.assign(n, 0);
	L.water.assign(n, 0);
	L.islandOf.assign(n, -1);
	L.prizeOf.assign(n, -1);
	L.radius.assign(n, 0.0);
	L.angle.assign(n, 0.0);
	L.homeX.assign(teams, 0);
	L.homeY.assign(teams, 0);
	L.pondX.assign(teams, 0);
	L.pondY.assign(teams, 0);
	for (int k = 0; k < teams; ++k)
	{
		const double a = L.phase + wedge * (k + 0.5);
		L.homeX[k] = t.x(L.cx + int(std::lround(g.homeRing * std::cos(a))));
		L.homeY[k] = t.y(L.cy + int(std::lround(g.homeRing * std::sin(a))));
		L.pondX[k] = L.homeX[k];
		L.pondY[k] = L.homeY[k];
	}
	const double pondRadius = std::clamp(0.2 * g.homeRadius, 3.0, 6.0);
	const RadialShape pond(pondRadius, 0.3, context, "flats-ponds");
	const RadialShape oasis(2.5, 0.3, context, "flats-ponds");
	const WedgeFrame frame(t, L.phase, teams);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			const WedgeFrame::Cell cell = frame.cell(x, y);
			const double d = cell.d, theta = cell.theta;
			L.radius[i] = d;
			L.angle[i] = theta;
			if (g.centralRadius > 0 && d < central.radiusAt(theta))
			{
				L.grass[i] = 1;
				L.islandOf[i] = -2;
				if (d < std::max(1.5, 0.22 * g.centralRadius))
					L.water[i] = 1;
				continue;
			}
			const int k = cell.k;
			const double s = cell.s;
			for (const Feature &b : blobs)
			{
				if (!b.holds(s - b.s, d - b.r))
					continue;
				if (b.kind == Pool || b.kind == Lagoon)
				{
					L.water[i] = 1;
					L.grass[i] = 0;
					L.islandOf[i] = -1;
				}
				else
				{
					L.grass[i] = 1;
					L.islandOf[i] = b.kind == HomeIsland ? k : -3;
					if (b.kind == Neutral)
						L.prizeOf[i] = b.prize;
					if (b.kind == HomeIsland &&
						std::hypot(s - b.s, d - b.r) < pond.radiusAt(std::atan2(d - b.r, s - b.s)))
						L.water[i] = 1;
					if (b.kind == Neutral &&
						std::hypot(s - b.s, d - b.r) < oasis.radiusAt(std::atan2(d - b.r, s - b.s)))
						L.water[i] = 1;
				}
			}
		}
	return L;
}

// Every home's kit, identical and unscaled: wheat and wood beside the pond on the side away from
// the swarm, a quarry on the island's rim towards the centre of the map; then its own scaled
// ambient farmland and outcrops.
void furnishHomes(Map &map, const Layout &L, GenerationContext &context, const TidalFlatsOptions &o)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const Fertility::Field fertility = Fertility::forMap(map, false);
	HeightMap split(t.w, t.h, context.stream("flats-home-split"));
	split.makePlain(6);
	const double pondRadius = std::clamp(0.2 * L.g.homeRadius, 3.0, 6.0) * 1.3 + 3;
	for (int team = 0; team < L.g.teams; ++team)
	{
		const auto eligible = [&](int i)
		{ return L.islandOf[i] == team && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
		const double a = std::atan2(double(t.offsetY(L.cy, L.pondY[team])),
									double(t.offsetX(L.cx, L.pondX[team])));
		const auto at = [&](double along, double across)
		{
			return std::make_pair(
				L.pondX[team] + int(std::lround(along * std::cos(a) - across * std::sin(a))),
				L.pondY[team] + int(std::lround(along * std::sin(a) + across * std::cos(a))));
		};
		// Wheat and wood on the two sides of the pond, the quarry towards the map's centre.
		if (const auto [x, y] = at(0, -pondRadius); true)
			if (const int seed = seedNear(t, x, y, 12, eligible); seed >= 0)
				growPatch(map, t, seed, CORN, kHomeWheat, eligible);
		if (const auto [x, y] = at(0, pondRadius); true)
			if (const int seed = seedNear(t, x, y, 12, eligible); seed >= 0)
				growPatch(map, t, seed, WOOD, kHomeWood, eligible);
		if (const auto [x, y] = at(-(pondRadius + 4), 0); true)
			if (const int seed = seedNear(t, x, y, 10, eligible); seed >= 0)
				placeResourceClump(map, context, {seed % t.w, seed / t.w}, STONE, 2);
		// Ambient farmland on the island's fertile ground, split two to one.
		std::vector<int> ground;
		std::vector<std::pair<double, int>> farm;
		for (int i = 0; i < n; ++i)
			if (eligible(i))
			{
				ground.push_back(i);
				if (const std::uint32_t f = fertility.at(i % t.w, i / t.w); f > 0)
					farm.push_back({-double(f), i});
			}
		std::stable_sort(farm.begin(), farm.end());
		const int area = int(ground.size());
		const int wheat = int(scaledCount(area * 4 / 100, o.wheat)),
				  wood = int(scaledCount(area * 2 / 100, o.wood));
		const int total = std::min(int(farm.size()), wheat + wood);
		std::vector<int> chosen;
		for (int k = 0; k < total; ++k)
			chosen.push_back(farm[k].second);
		std::stable_sort(chosen.begin(), chosen.end(),
						 [&](int p, int q)
						 {
							 return split.uiLevel(p % t.w, p / t.w, 2048) <
									split.uiLevel(q % t.w, q / t.w, 2048);
						 });
		const int wheatShare = int(std::int64_t(total) * wheat / std::max(1, wheat + wood));
		for (int k = 0; k < total; ++k)
			map.setResource(chosen[k] % t.w, chosen[k] / t.w, k < wheatShare ? CORN : WOOD, 1);
		const int outcrops = int(scaledCount(1, o.stone));
		for (int k = 0; k < outcrops && !ground.empty(); ++k)
			for (int attempt = 0; attempt < 100; ++attempt)
			{
				const int at2 = ground[context.bounded("flats-home-stone", ground.size())];
				if (eligible(at2))
				{
					placeResourceClump(map, context, {at2 % t.w, at2 / t.w}, STONE, 1);
					break;
				}
			}
	}
}

// Every neutral island is an oasis, its pond ringed by wheat to its shores, with one prize inside,
// a fruit grove or a stone deposit in turn; the central island carries the orchard of all three
// fruits round its pond, with a quarry.
void stockIslands(Map &map, const Layout &L, GenerationContext &context, const TidalFlatsOptions &o)
{
	const Torus &t = L.t;
	const int n = t.w * t.h;
	// Every neutral island is its own component of grass; each gets its prize at its middle.
	std::vector<unsigned char> seen(n, 0);
	for (int start = 0; start < n; ++start)
	{
		if (seen[start] || L.islandOf[start] != -3 || L.prizeOf[start] < 0)
			continue;
		std::vector<int> tiles{start};
		seen[start] = 1;
		for (size_t head = 0; head < tiles.size(); ++head)
		{
			const int i = tiles[head];
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int m = t.at(i % t.w + dx, i / t.w + dy);
					if (!seen[m] && L.islandOf[m] == -3 && L.prizeOf[m] >= 0)
					{
						seen[m] = 1;
						tiles.push_back(m);
					}
				}
		}
		double sx = 0, sy = 0;
		for (int i : tiles)
		{
			sx += t.offsetX(start, i % t.w);
			sy += t.offsetY(start / t.w, i / t.w);
		}
		const int mx = t.x(start % t.w + int(std::lround(sx / tiles.size())));
		const int my = t.y(start / t.w + int(std::lround(sy / tiles.size())));
		const auto eligible = [&](int i)
		{ return L.islandOf[i] == -3 && clearGround(map, i % t.w, i / t.w); };
		// The prize, on the far side of the pond from the map's centre.
		const double a = std::atan2(double(t.offsetY(L.cy, my)), double(t.offsetX(L.cx, mx)));
		if (const int seed = seedNear(t, mx + int(std::lround(6 * std::cos(a))),
									  my + int(std::lround(6 * std::sin(a))), 8, eligible);
			seed >= 0)
		{
			const MapGeneratorPoint centre(seed % t.w, seed / t.w);
			if (L.prizeOf[start] == 0)
			{
				if (scaledCount(1, o.fruit) > 0)
					placeResourceClump(map, context, centre,
									   CHERRY + int(context.bounded("flats-prizes", 3)), 2);
			}
			else if (scaledCount(1, o.stone) > 0)
			{
				placeResourceClump(map, context, centre, STONE, 2);
			}
		}
		// The oasis: every other tile of the island under wheat, unscaled, so it has to be cleared.
		for (int i : tiles)
			if (eligible(i) && map.isResourceAllowed(i % t.w, i / t.w, CORN))
				map.setResource(i % t.w, i / t.w, CORN, 1);
	}
	if (L.g.centralRadius > 0)
	{
		const auto eligible = [&](int i)
		{ return L.islandOf[i] == -2 && clearGround(map, i % t.w, i / t.w); };
		const double rho = std::max(1.5, 0.22 * L.g.centralRadius) + 4;
		const double spin = context.bounded("flats-prizes", 3600) / 3600.0 * 2 * kPi;
		if (scaledCount(1, o.fruit) > 0)
			for (int f = 0; f < 3; ++f)
			{
				const double a = spin + 2 * kPi * f / 3;
				const int seed = seedNear(t, L.cx + int(std::lround(rho * std::cos(a))),
										  L.cy + int(std::lround(rho * std::sin(a))), 8, eligible);
				if (seed >= 0)
					placeResourceClump(map, context, {seed % t.w, seed / t.w}, CHERRY + f, 2);
			}
		if (scaledCount(1, o.stone) > 0)
			if (const int seed = seedNear(
					t, L.cx + int(std::lround((L.g.centralRadius - 4) * std::cos(spin + kPi / 3))),
					L.cy + int(std::lround((L.g.centralRadius - 4) * std::sin(spin + kPi / 3))), 8,
					eligible);
				seed >= 0)
				placeResourceClump(map, context, {seed % t.w, seed / t.w}, STONE, 2);
	}
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "flats layout";
	const TidalFlatsOptions o(context.request);
	Map &map = game.map;
	const int teams = context.request.nbTeams;
	map.makeHomogenMap(SAND);
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

	context.stage = "flats terrain";
	TerrainSketch terrain(n, SAND);
	for (int i = 0; i < n; ++i)
	{
		if (L.grass[i])
			terrain[i] = GRASS;
		if (L.water[i])
			terrain[i] = WATER;
	}
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "flats colonies";
	const auto island = [&](int team)
	{
		std::vector<unsigned char> home(n, 0);
		for (int i = 0; i < n; ++i)
			home[i] = L.islandOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return home;
	};
	// The swarm stands on the far side of the pond from the map's centre, a short walk from the
	// fields; placeSettlement measures from the footprint's top-left tile.
	const auto anchor = [&](int team)
	{
		const double a = std::atan2(double(t.offsetY(L.cy, L.homeY[team])),
									double(t.offsetX(L.cx, L.homeX[team])));
		const double back = std::clamp(0.2 * L.g.homeRadius, 3.0, 6.0) * 1.3 + 6;
		return MapGeneratorPoint(L.pondX[team] + int(std::lround(back * std::cos(a))) - 2,
								 L.pondY[team] + int(std::lround(back * std::sin(a))) - 2);
	};
	if (!settleColonies(game, context, "flats-starts", island, anchor))
		return false;

	context.stage = "flats resources";
	furnishHomes(map, L, context, o);
	stockIslands(map, L, context, o);
	seedAlgae(map, context, t, "flats-algae", o.algae, AlgaeBand::anyWater());
	clearAroundSwarms(map, context, t);
	guaranteeStartingResources(game, context, 24, 32, 0);
	clearAroundSwarms(map, context, t);
	return true;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.nbTeams < 1)
		return "Tidal flats need at least one colony.";
	const Geometry g = geometryFor(r);
	if (g.homeRadius < 9)
		return "The home islands do not fit on this map; use a bigger map, fewer colonies or "
			   "smaller islands.";
	return "";
}

// Checked on the finished world: every home island still carries its pond, and colony 0 can walk
// to every other colony and onto the central island, with water, buildings and every resource
// blocking; the flats themselves hold nothing, so that walk is always the open one.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "flats"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int n = t.w * t.h, teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
	{
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
			for (int dx = -2; dx <= 2 && !pond; ++dx)
				pond = map.isWater(t.x(L.pondX[k] + dx), t.y(L.pondY[k] + dy));
		if (!pond)
			return "Colony " + std::to_string(k) + "'s island has lost its pond.";
	}
	const ColonyWalk walk = walkFromFirstColony(map, teams, "the flats", "over the flats");
	if (!walk.error.empty())
		return walk.error;
	if (L.g.centralRadius > 0)
	{
		bool arrived = false;
		for (int i = 0; i < n && !arrived; ++i)
			arrived = L.islandOf[i] == -2 && walk.steps[i] >= 0;
		if (!arrived)
			return "The central island cannot be reached over the flats.";
	}
	return "";
}
} // namespace

TidalFlatsOptions::TidalFlatsOptions(const GenerationRequest &r)
	: homeIslandSize(r.option("home-island-size")), extraIslands(r.option("extra-islands")),
	  sandbars(r.option("sandbars")), tidePools(r.option("tide-pools")),
	  lagoons(r.option("lagoons")), coastRoughness(r.option("coast-roughness")),
	  centralIsland(r.option("central-island") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition tidalFlatsDefinition()
{
	return {
		"tidal-flats",
		18,
		"Tidal flats",
		3,
		false,
		// The home islands' radius as a share of the half side; extra islands, sandbars and
		// lagoons per colony; tide pools per 128x128 of flats.
		{{"home-island-size", "Home island size", 14, 26, 1, 20, ControlGroup::Terrain},
		 {"extra-islands", "Extra islands", 0, 4, 1, 3, ControlGroup::Terrain},
		 {"sandbars", "Sandbars", 0, 4, 1, 3, ControlGroup::Terrain},
		 {"tide-pools", "Tide pools", 0, 12, 1, 8, ControlGroup::Terrain},
		 {"lagoons", "Lagoons", 0, 4, 1, 3, ControlGroup::Terrain},
		 {"coast-roughness", "Coast roughness", 0, 100, 5, 50, ControlGroup::Terrain},
		 // Off, the middle of the map is flats like the rest, and there is no orchard.
		 GeneratorControl::toggle("central-island", "Central island", true, ControlGroup::Layout),
		 // Every island's ambient fields and outcrops and every prize; each home's kit is unscaled.
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
