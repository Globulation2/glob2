// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaravanseraiGenerator.h"
#include "BuildingType.h"
#include "Contact.h"
#include "Drawing.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Growth.h"
#include "LatticeNoise.h"
#include "Orbits.h"
#include "Patterns.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Room.h"
#include "Routes.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Walls.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;

// Caravanserai: oasis towns on a desert trade route.
//
// WHAT IT LOOKS LIKE. A desert seen from the air: bare sand dunes in long parallel bands with
// sparse scrub in the hollows between them, flat-topped rock mesas standing out of the sand, and
// scattered oases, each an irregular pool ringed with green, date palms and a patch of grain. Every
// colony's home is a large oasis town round its own lake, its shore a ring of fields and a palm
// grove behind a sand path, the town beyond. Half way between neighbouring towns, on the caravan
// route, stands a caravanserai: a square walled courtyard with a gate at either end and a well in
// the middle, in an oasis of its own with orchards and a quarry. The routes between towns are
// clear lines of sand through the scrub. (The first version was flat sand with perfect circles in
// straight rows, 28 water tiles and 80 of wood on the whole map: nothing read as a desert and no
// colony could build its way along the chain.)
//
// HOW IT PLAYS. Sand holds no building and no crop, so the desert can be crossed but never
// settled: every forward inn, tower or barracks stands on an oasis, and the oases are what a colony
// expands along. Home is rich - a lake, wheat and palms on its shore, a town's worth of building
// room - and everything else is out in the sand: the stone and fruit are at the caravanserais,
// which every pair of neighbours shares at equal distance, and in the rock of the mesas; the oases
// between add forward ground, a little grain and palms. Mesas are walls of stone the routes must go
// round, so the way to a caravanserai is not always the straight one.
//
// FAIRNESS. The towns stand on a lattice and are one stencil stamped by quarter turns, so every
// home oasis, lake, field ring and palm grove is identical. Each caravanserai is the midpoint of two
// neighbours, and the oases on the way to it are placed along each colony's straight route at the
// same spacing; the scattered oases, mesas and dunes are noise, and the lobby's best-of-five start
// scoring covers what they leave uneven. The validator proves every colony's walk to its nearest
// caravanserai is within a tolerance of every other's.
namespace
{
// The home oasis, in its own frame: an outline of `oasis-size` tiles radius, a lake at the back of
// it a third as wide, a ring of fields round the lake's shore closed off from the town by a sand
// path, and a palm grove in the back of the ring between two sand spokes.
constexpr double kHomeRoughness = 0.2, kLakeShare = 0.3, kLakeBack = 0.42, kLakeRoughness = 0.3;
constexpr double kFieldWidth = 6, kGroveHalfAngle = 0.8;
// The home's crops: wheat on this share of the ring's ground, palms on this share of the grove's,
// scaled by the amounts but never below floors that feed an opening.
constexpr int kWheatPercent = 55, kPalmPercent = 60, kWheatFloor = 60, kPalmFloor = 30;
// A home must keep this much town (4x4 build sites) and this much field ring.
constexpr int kLeastTownSites = 60, kLeastFieldTiles = 120, kDefaultHome = 24;
// A caravanserai: a courtyard wall at Chebyshev distance 6 from its middle, gates three tiles
// wide at both ends of the route, a well of 2x2 corners in the middle, standing in an oasis of
// radius 14 with a pond to one side of the route.
constexpr int kSeraiWall = 6, kSeraiGateHalf = 1, kWellCorners = 2;
constexpr double kSeraiOasis = 14, kSeraiRoughness = 0.1, kSeraiPondOut = 11, kSeraiPond = 2.8;
// A caravanserai's prizes: the three fruits in groves beside the courtyard, a quarry beside them,
// grain and palms by the pond. Groves and quarry scale with their amounts.
constexpr int kGroveTiles = 5, kQuarryRadius = 2, kSeraiWheat = 18, kSeraiPalms = 10;
// Oases: route oases every `kWaySpacing` tiles along each colony's straight way to a caravanserai,
// and scattered oases one per so many desert tiles by the Oases control; radii 4 to 8, a pond at
// a third of the radius, off centre; palms and grain in proportion.
constexpr int kWaySpacing = 32, kLeastOasis = 4, kMostOasis = 10, kOasisGap = 8;
constexpr int kDesertTilesPerOasis[3] = {3200, 1800, 1100};
// Mesas: the noise field's top share of the open desert by the Desert control, kept this far from
// every oasis, town and route.
constexpr int kMesaPercent[3] = {0, 5, 11};
constexpr int kMesaClearance = 6;
// Dunes: scrub on this share of the corners in the hollows between dune bands, never two side by
// side (a single grass corner is scrub; four make a buildable tile), and not on the routes.
constexpr int kHollowPercent = 38, kScrubPercent = 16;
constexpr double kDuneSpacing = 12;
constexpr int kHaloWidth = 3, kHaloPercent = 30;
constexpr int kLeastHomeDistance = 16;
// Lattices that are not exact (six colonies on a rectangle) leave neighbours at unequal distances,
// which the caravanserai walk inherits; 32 steps is a quarter of a 256 map's colony spacing.
constexpr int kWalkTolerance = 32;

enum HomeKind : signed char
{
	kNotHome,
	kTown,
	kFields,
	kGrove,
	kLake
};

struct HomeStencil
{
	int extent = 0;
	double radius = 0;
	std::vector<unsigned char> vertex;
	std::vector<signed char> kind;
	std::array<int, 3> pure{}; // town, fields, grove pure grass tiles, beaches laid
	int townSites = 0;
	ShapePoint swarm{}, lake{};
	int side() const { return 2 * extent + 1; }
	int index(int dx, int dy) const { return (dy + extent) * side() + dx + extent; }
};

HomeStencil buildHome(double radius, GenerationContext &context)
{
	HomeStencil s;
	s.radius = radius;
	const RadialShape outline(radius, kHomeRoughness, context, "caravanserai-home");
	const RadialShape lake(kLakeShare * radius, kLakeRoughness, context, "caravanserai-lake");
	s.lake = {-kLakeBack * radius, 0};
	s.extent = int(std::ceil(outline.maximumRadius())) + 3;
	const int side = s.side();
	s.vertex.assign(size_t(side) * side, SAND);
	s.kind.assign(size_t(side) * side, kNotHome);
	const auto classify = [&](double px, double py, bool vertex)
	{
		const double r = std::hypot(px, py);
		if (r > outline.radiusAt(std::atan2(py, px)))
			return std::pair<signed char, unsigned char>{kNotHome, SAND};
		const double lx = px - s.lake.x, ly = py - s.lake.y;
		const double angle = std::atan2(ly, lx), fromLake = std::hypot(lx, ly);
		const double shore = fromLake - lake.radiusAt(angle);
		if (shore <= 0)
			return std::pair<signed char, unsigned char>{kLake, WATER};
		if (shore <= kFieldWidth + 1)
		{
			const double off = std::abs(std::remainder(angle - kPi, 2 * kPi));
			const double spokeGap = std::abs(off - kGroveHalfAngle) * fromLake;
			if (vertex && (shore > kFieldWidth || spokeGap <= 0.6))
				return std::pair<signed char, unsigned char>{kFields, SAND};
			if (shore <= kFieldWidth)
				return std::pair<signed char, unsigned char>{off < kGroveHalfAngle ? kGrove : kFields,
															 GRASS};
			return std::pair<signed char, unsigned char>{kTown, GRASS};
		}
		return std::pair<signed char, unsigned char>{kTown, GRASS};
	};
	for (int dy = -s.extent; dy <= s.extent; ++dy)
		for (int dx = -s.extent; dx <= s.extent; ++dx)
		{
			const int at = s.index(dx, dy);
			s.vertex[at] = classify(dx, dy, true).second;
			s.kind[at] = classify(dx + 0.5, dy + 0.5, false).first;
		}
	// Pure grass once the lake's beach is laid, by kind; and the town's 4x4 build sites.
	const auto after = [&](int vx, int vy)
	{
		const unsigned char v = s.vertex[s.index(vx, vy)];
		if (v != GRASS)
			return v;
		for (int ny = vy - 1; ny <= vy + 1; ++ny)
			for (int nx = vx - 1; nx <= vx + 1; ++nx)
				if (std::abs(nx) <= s.extent && std::abs(ny) <= s.extent &&
					s.vertex[s.index(nx, ny)] == WATER)
					return static_cast<unsigned char>(SAND);
		return v;
	};
	std::vector<unsigned char> townPure(size_t(side) * side, 0);
	for (int dy = -s.extent; dy < s.extent; ++dy)
		for (int dx = -s.extent; dx < s.extent; ++dx)
		{
			const bool pure = after(dx, dy) == GRASS && after(dx + 1, dy) == GRASS &&
							  after(dx, dy + 1) == GRASS && after(dx + 1, dy + 1) == GRASS;
			const signed char kind = s.kind[s.index(dx, dy)];
			if (!pure)
				continue;
			if (kind == kTown)
			{
				++s.pure[0];
				townPure[s.index(dx, dy)] = 1;
			}
			else if (kind == kFields)
				++s.pure[1];
			else if (kind == kGrove)
				++s.pure[2];
		}
	// The swarm goes where the town is widest: the tile farthest from any tile that is not town
	// (ties forward). Numbi searches for a first building only when its swarm's surroundings are
	// open, and a swarm set a fixed distance forward stood against the field ring's path, where no
	// Numbi colony ever built (a 45,000-tick Numbi tournament, 2026-09-16).
	{
		int best = -1, bestTile = -1;
		for (int dy = -s.extent + 1; dy < s.extent - 1; ++dy)
			for (int dx = -s.extent + 1; dx < s.extent - 1; ++dx)
			{
				if (s.kind[s.index(dx, dy)] != kTown)
					continue;
				int clear = 0;
				bool open = true;
				while (open && clear < s.extent)
				{
					++clear;
					for (int oy = -clear; oy <= clear && open; ++oy)
						for (int ox = -clear; ox <= clear && open; ++ox)
						{
							if (std::max(std::abs(ox), std::abs(oy)) != clear)
								continue;
							const int x = dx + ox, y = dy + oy;
							open = std::abs(x) <= s.extent && std::abs(y) <= s.extent &&
								   s.kind[s.index(x, y)] == kTown;
						}
				}
				const int score = clear * 1000 + dx * 10 - std::abs(dy);
				if (score > best)
				{
					best = score;
					bestTile = s.index(dx, dy);
				}
			}
		const int side = s.side();
		s.swarm = {double(bestTile % side - s.extent) + 0.5, double(bestTile / side - s.extent) + 0.5};
	}
	for (int dy = -s.extent; dy + 3 < s.extent; ++dy)
		for (int dx = -s.extent; dx + 3 < s.extent; ++dx)
		{
			bool fits = true;
			for (int y = 0; y < 4 && fits; ++y)
				for (int x = 0; x < 4 && fits; ++x)
					fits = townPure[s.index(dx + x, dy + y)];
			s.townSites += fits;
		}
	return s;
}

struct Serai
{
	ShapePoint centre;
	int facing; // quarter turn of the route through it
	int pondSide;
	int a, b;
};

struct Oasis
{
	ShapePoint centre, pond;
	double radius;
};

struct Layout
{
	Torus t{1, 1};
	HomeStencil home;
	std::vector<ShapePoint> homes;
	std::vector<int> facings;
	std::vector<Serai> serais;
	std::vector<Oasis> oases;
	TerrainSketch sketch;
	std::vector<signed char> homeKind;
	std::vector<int> homeOf;       // colony of every home tile, else -1
	std::vector<int> seraiOf;      // caravanserai of every tile in its oasis, else -1
	std::vector<int> courtyardOf;  // caravanserai of every courtyard tile, else -1
	std::vector<int> oasisOf;      // scattered or route oasis of every tile, else -1
	std::vector<unsigned char> wall, gate, mesa, route;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const CaravanseraiOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	const int offsetX = int(context.bounded("caravanserai-layout", std::uint32_t(t.w)));
	const int offsetY = int(context.bounded("caravanserai-layout", std::uint32_t(t.h)));
	L.homes = latticeSites(t.w, t.h, teams, offsetX, offsetY).sites;
	for (ShapePoint &h : L.homes)
		h = {double(int(std::lround(h.x)) % t.w), double(int(std::lround(h.y)) % t.h)};
	dealStarts(context, L.homes);
	// One facing for every colony, drawn once per map. A home stencil turned by different quarter
	// turns covers identical tiles, but the AIs scan along the map's axes: with a facing per colony,
	// Numbi colonies on The Glacis grew to 60 in one facing and 20 to 30 in the others (rotation
	// tournaments, 2026-09-16). The same facing makes every home an exact translation of the others.
	L.facings.assign(teams, int(context.bounded("caravanserai-facing", 4)));

	// Negotiate the home into the lattice: a caravanserai needs its whole oasis between two homes.
	const double nearest = nearestSiteDistance(t, L.homes);
	int radius = o.oasisSize;
	bool serais = teams > 1;
	const auto homeReach = [&] { return radius * (1 + 3 * kHomeRoughness) + 3; };
	while (teams > 1 && nearest < 2 * (homeReach() + kSeraiOasis + 4) && radius > kLeastHomeDistance)
	{
		radius -= 2;
		context.telemetry.fallback("caravanserai.home.shrunk", "Homes shrank for their caravanserais");
	}
	if (teams > 1 && nearest < 2 * (homeReach() + kSeraiOasis + 4))
	{
		serais = false;
		context.telemetry.fallback("caravanserai.serais.omitted", "No room for caravanserais");
	}
	if (nearest < 2 * homeReach() + 8 || std::min(t.w, t.h) < 2 * homeReach() + 8)
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	context.telemetry.measure("caravanserai.home.radius", radius);
	L.home = buildHome(radius, context);
	const HomeStencil &s = L.home;
	context.telemetry.measure("caravanserai.home.town-sites", s.townSites);
	context.telemetry.measure("caravanserai.home.field-tiles", s.pure[1]);
	context.telemetry.measure("caravanserai.home.grove-tiles", s.pure[2]);
	// The floors are for the default home; a home shrunk to fit a crowded map keeps them in
	// proportion, never below a third.
	const double scale = std::max(0.33, double(radius * radius) / (kDefaultHome * kDefaultHome));
	if (s.townSites < kLeastTownSites * scale || s.pure[1] < kLeastFieldTiles * scale)
	{
		L.failure = "The home oasis is too small; raise Home oasis size.";
		return L;
	}

	L.sketch.assign(n, SAND);
	L.homeKind.assign(n, kNotHome);
	L.homeOf.assign(n, -1);
	L.seraiOf.assign(n, -1);
	L.courtyardOf.assign(n, -1);
	L.oasisOf.assign(n, -1);
	L.wall.assign(n, 0);
	L.gate.assign(n, 0);
	L.mesa.assign(n, 0);
	L.route.assign(n, 0);
	std::vector<unsigned char> feature(n, 0); // corners any oasis, home or courtyard owns
	for (int k = 0; k < teams; ++k)
	{
		const int cx = int(L.homes[k].x), cy = int(L.homes[k].y);
		for (int dy = -s.extent; dy <= s.extent; ++dy)
			for (int dx = -s.extent; dx <= s.extent; ++dx)
			{
				const int at = s.index(dx, dy);
				const auto [tx, ty] = turnStencilTile(L.facings[k], dx, dy);
				const int i = t.at(cx + tx, cy + ty);
				if (s.kind[at] != kNotHome)
				{
					L.homeKind[i] = s.kind[at];
					L.homeOf[i] = k;
				}
				const auto [vx, vy] = turnStencilVertex(L.facings[k], dx, dy);
				const int v = t.at(cx + vx, cy + vy);
				if (s.vertex[at] != SAND || s.kind[at] != kNotHome)
				{
					L.sketch[v] = TerrainType(s.vertex[at]);
					feature[v] = 1;
				}
			}
	}

	// Caravanserais between neighbours.
	if (serais)
	{
		const RadialShape seraiOasis(kSeraiOasis, kSeraiRoughness, context, "caravanserai-serai");
		for (const auto &[a, b] : nearestPairs(t, L.homes, o.caravanserais))
		{
			ShapePoint mid = midpointAcross(t, L.homes[a], L.homes[b]);
			mid = {std::floor(mid.x), std::floor(mid.y)};
			bool clear = true;
			for (const Serai &other : L.serais)
				clear = clear && siteDistance(t, mid, other.centre) >= 2 * kSeraiOasis + 4;
			for (int k = 0; k < teams; ++k)
				clear = clear && siteDistance(t, mid, L.homes[k]) >= homeReach() + kSeraiOasis + 2;
			if (!clear)
			{
				context.telemetry.fallback("caravanserai.serai.crowded",
										   "A caravanserai had no room between its homes", a * teams + b);
				continue;
			}
			const Serai serai{mid, quarterTurn(headingAcross(t, L.homes[a], L.homes[b])),
							  int(context.bounded("caravanserai-serai", 2)) * 2 - 1, a, b};
			const int index = int(L.serais.size());
			L.serais.push_back(serai);
			const int cx = int(mid.x), cy = int(mid.y);
			forEachTileInShape(t, mid.x, mid.y, seraiOasis, 0,
							   [&](int i, double, double)
							   {
								   L.sketch[i] = GRASS;
								   feature[i] = 1;
								   L.seraiOf[i] = index;
							   });
			const bool alongX = serai.facing % 2 == 0;
			for (int dy = -kSeraiWall; dy <= kSeraiWall; ++dy)
				for (int dx = -kSeraiWall; dx <= kSeraiWall; ++dx)
				{
					const int i = t.at(cx + dx, cy + dy);
					L.sketch[i] = GRASS;
					L.sketch[t.at(cx + dx + 1, cy + dy + 1)] = GRASS;
					const int ring = std::max(std::abs(dx), std::abs(dy));
					if (ring < kSeraiWall)
						L.courtyardOf[i] = index;
					else
					{
						const int along = alongX ? dx : dy, across = alongX ? dy : dx;
						const bool door = std::abs(along) == kSeraiWall && std::abs(across) <= kSeraiGateHalf;
						(door ? L.gate : L.wall)[i] = 1;
					}
				}
			// The well, in the middle of the courtyard.
			for (int dy = 0; dy < kWellCorners; ++dy)
				for (int dx = 0; dx < kWellCorners; ++dx)
					L.sketch[t.at(cx + dx, cy + dy)] = WATER;
			// The pond, to one side of the route outside the wall.
			const double px = alongX ? cx : cx + serai.pondSide * kSeraiPondOut;
			const double py = alongX ? cy + serai.pondSide * kSeraiPondOut : cy;
			for (int dy = -4; dy <= 4; ++dy)
				for (int dx = -4; dx <= 4; ++dx)
					if (std::hypot(dx, dy) <= kSeraiPond)
						L.sketch[t.at(int(px) + dx, int(py) + dy)] = WATER;
		}
	}
	context.telemetry.measure("caravanserai.serais", int(L.serais.size()));

	// Oases: first along every colony's way to each caravanserai it shares, then scattered.
	std::vector<unsigned char> taken = feature;
	const auto clearOf = [&](ShapePoint p, double r)
	{
		for (int k = 0; k < teams; ++k)
			if (siteDistance(t, p, L.homes[k]) < homeReach() + r + kOasisGap)
				return false;
		for (const Serai &serai : L.serais)
			if (siteDistance(t, p, serai.centre) < kSeraiOasis + r + kOasisGap)
				return false;
		for (const Oasis &other : L.oases)
			if (siteDistance(t, p, other.centre) < other.radius + r + kOasisGap)
				return false;
		return true;
	};
	const auto stampOasis = [&](ShapePoint centre, double r)
	{
		// Oases vary: rougher outlines, and most drawn out along a hollow between the dunes.
		const RadialShape outline(r, 0.3, context, "caravanserai-oases");
		const double stretch = 1.0 + context.bounded("caravanserai-oases", 80) / 100.0;
		const double heading = context.bounded("caravanserai-oases", 628) / 100.0;
		const double pondAngle = context.bounded("caravanserai-oases", 628) / 100.0;
		const RadialShape pond(std::max(1.6, 0.32 * r), 0.3, context, "caravanserai-oases");
		const ShapePoint pondAt = polarPoint(centre.x, centre.y, 0.3 * r, pondAngle);
		const int index = int(L.oases.size());
		L.oases.push_back({centre, pondAt, r});
		const int reach = int(std::ceil(outline.maximumRadius() * stretch)) + 1;
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
			{
				const double along = (dx * std::cos(heading) + dy * std::sin(heading)) / stretch;
				const double across = (-dx * std::sin(heading) + dy * std::cos(heading)) * std::sqrt(stretch);
				if (std::hypot(along, across) > outline.radiusAt(std::atan2(across, along)))
					continue;
				const int i = t.at(int(centre.x) + dx, int(centre.y) + dy);
				L.sketch[i] = GRASS;
				feature[i] = 1;
				L.oasisOf[i] = index;
			}
		forEachTileInShape(t, pondAt.x, pondAt.y, pond, 0,
						   [&](int i, double, double) { L.sketch[i] = WATER; });
	};
	int routeOases = 0;
	for (const Serai &serai : L.serais)
		for (const int k : {serai.a, serai.b})
			for (const ShapePoint &p :
				 waypointsAlong(t, L.homes[k], serai.centre, kWaySpacing, homeReach() + kWaySpacing / 2.0,
								kSeraiOasis + kWaySpacing / 2.0))
			{
				const double r = kLeastOasis + context.bounded("caravanserai-oases", kMostOasis - kLeastOasis + 1);
				if (clearOf(p, r))
				{
					stampOasis(p, r);
					++routeOases;
				}
			}
	int desert = 0;
	for (int i = 0; i < n; ++i)
		desert += !feature[i];
	const int wanted = desert / kDesertTilesPerOasis[std::clamp(o.oases, 0, 2)];
	int scattered = 0;
	for (int attempt = 0; attempt < 40 * wanted && scattered < wanted; ++attempt)
	{
		const int i = int(context.bounded("caravanserai-scatter", std::uint32_t(n)));
		const double r = kLeastOasis + context.bounded("caravanserai-scatter", kMostOasis - kLeastOasis + 1);
		const ShapePoint p{double(i % t.w), double(i / t.w)};
		if (!clearOf(p, r))
			continue;
		stampOasis(p, r);
		++scattered;
	}
	context.telemetry.measure("caravanserai.oases.route", routeOases);
	context.telemetry.measure("caravanserai.oases.scattered", scattered);

	// The caravan routes: from every home to each caravanserai it shares, round the features.
	const std::vector<int> fromFeature = stepsFrom(t, feature);
	const std::vector<int> relief = fractalNoise(t.w, t.h, 48, 3, context.stream("caravanserai-mesas"));
	for (const Serai &serai : L.serais)
		for (const int k : {serai.a, serai.b})
		{
			const std::vector<int> path = cheapestWalk(
				t, GridNeighbors::Eight, {t.at(int(L.homes[k].x), int(L.homes[k].y))},
				tileMask(t, {t.at(int(serai.centre.x), int(serai.centre.y))}),
				[&](int, int to, int dx, int dy)
				{ return (dx && dy ? 14 : 10) + (L.oasisOf[to] >= 0 ? 0 : 4) + relief[to] / 8192; });
			for (int i : path)
				if (!feature[i])
					L.route[i] = 1;
		}
	std::vector<unsigned char> keepClear = feature;
	for (int i = 0; i < n; ++i)
		keepClear[i] = keepClear[i] || L.route[i];
	const std::vector<int> fromKept = stepsFrom(t, keepClear);

	// Mesas on the high ground of the relief, clear of everything a colony needs.
	const int desertChoice = std::clamp(o.desert, 0, 2);
	if (kMesaPercent[desertChoice] > 0)
	{
		std::vector<int> samples;
		for (int i = 0; i < n; ++i)
			if (fromKept[i] >= kMesaClearance)
				samples.push_back(relief[i]);
		if (!samples.empty())
		{
			// The share of the whole map, taken from the eligible desert.
			const int share = std::min(100, int(std::int64_t(kMesaPercent[desertChoice]) * n / samples.size()));
			const int cut = percentile(samples, 100 - share);
			for (int i = 0; i < n; ++i)
				if (fromKept[i] >= kMesaClearance && relief[i] >= cut)
					L.sketch[i] = GRASS;
		}
	}
	// A mesa's stone stands on the tiles whose four corners it raised.
	int mesaTiles = 0;
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		bool rock = fromKept[i] >= kMesaClearance;
		for (const int j : {i, t.at(x + 1, y), t.at(x, y + 1), t.at(x + 1, y + 1)})
			rock = rock && L.sketch[j] == GRASS && !feature[j] && fromKept[j] >= kMesaClearance;
		L.mesa[i] = rock;
		mesaTiles += rock;
	}
	context.telemetry.measure("caravanserai.mesa.tiles", mesaTiles);

	// Dunes: scrub in the hollows between the bands, a single corner at a time.
	StripeStyle dunes;
	dunes.acrossX = 1 + int(context.bounded("caravanserai-dunes", 4));
	dunes.acrossY = int(context.bounded("caravanserai-dunes", 3)) * (context.bounded("caravanserai-dunes", 2) ? 1 : -1);
	dunes.warpPercent = 35;
	std::mt19937 &duneStream = context.stream("caravanserai-dunes");
	const std::vector<int> phase = stripePhase(t, dunes, duneStream);
	// Many dune bands to one stripe of the field: a whole number, so the bands still wrap.
	const int bands = std::max(1, int(std::lround(stripeSpacing(t, dunes) / kDuneSpacing)));
	int scrub = 0;
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		const bool hollow = (phase[i] * bands) % 65536 < 65536 * kHollowPercent / 100;
		if (L.sketch[i] != SAND || fromKept[i] < 2)
			continue;
		// Round every oasis the scrub thickens into a halo, wherever the dunes lie.
		const bool halo = fromFeature[i] >= 0 && fromFeature[i] <= kHaloWidth;
		if (!hollow && !halo)
			continue;
		if (int(context.bounded("caravanserai-scrub", 100)) >= (halo ? kHaloPercent : kScrubPercent))
			continue;
		bool alone = true;
		for (int dy = -1; dy <= 1 && alone; ++dy)
			for (int dx = -1; dx <= 1 && alone; ++dx)
				alone = (!dx && !dy) || L.sketch[t.at(x + dx, y + dy)] != GRASS;
		if (alone)
		{
			L.sketch[i] = GRASS;
			++scrub;
		}
	}
	context.telemetry.measure("caravanserai.dunes.scrub", scrub);

	// The design's invariants on the sketch as the game will see it: courtyard walls on pure grass.
	TerrainSketch beached = L.sketch;
	layBeaches(beached, t);
	const std::vector<unsigned char> pure = pureTiles(beached, t, GRASS);
	for (int i = 0; i < n; ++i)
		if (L.wall[i] && !pure[i])
		{
			L.failure = "A wall would stand on a beach; try another seed.";
			return L;
		}
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "caravanserai layout";
	const CaravanseraiOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("caravanserai.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "caravanserai terrain";
	TerrainSketch terrain = L.sketch;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	const DesignedStone walls = designedStone(map, t, L.wall);
	if (walls.gaps)
	{
		context.detail = "a caravanserai wall has a gap";
		return false;
	}
	std::vector<unsigned char> structural = walls.stone;
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (walls.stone[i])
			map.setResource(x, y, STONE, 1);
		else if (L.mesa[i] && map.isGrass(x, y))
		{
			map.setResource(x, y, STONE, 1);
			structural[i] = 1;
		}
	}

	context.stage = "caravanserai colonies";
	std::vector<int> townOf(n, -1);
	for (int i = 0; i < n; ++i)
		if (L.homeKind[i] == kTown)
			townOf[i] = L.homeOf[i];
	if (!settleColonies(
			game, context, "caravanserai-starts",
			[&](int k) { return homeGrassMask(map, t, townOf, k); },
			[&](int k)
			{
				const ShapePoint p = turnStencilPoint(L.facings[k], L.home.swarm);
				return MapGeneratorPoint(int(std::floor(L.homes[k].x + p.x)) - 2,
										 int(std::floor(L.homes[k].y + p.y)) - 2);
			}))
		return false;
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);

	context.stage = "caravanserai resources";
	std::vector<unsigned char> waterTiles(n, 0);
	for (int i = 0; i < n; ++i)
		waterTiles[i] = map.isWater(i % t.w, i / t.w);
	const std::vector<int> fromWater = stepsFrom(t, waterTiles);
	const auto open = [&](int i)
	{ return map.isGrass(i % t.w, i / t.w) && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
	// Plant `count` of `type` on the tiles `where` allows, nearest the water first.
	const auto plantNearWater = [&](int type, int count, auto where)
	{
		std::vector<std::pair<int, int>> tiles;
		for (int i = 0; i < n; ++i)
			if (where(i) && open(i) && fromWater[i] >= 0)
				tiles.push_back({fromWater[i], i});
		std::stable_sort(tiles.begin(), tiles.end());
		int placed = 0;
		for (const auto &[d, i] : tiles)
		{
			if (placed >= count)
				break;
			map.setResource(i % t.w, i / t.w, type, 1);
			++placed;
		}
		return placed;
	};
	const HomeStencil &s = L.home;
	const int wheat = std::max(kWheatFloor, int(scaledCount(s.pure[1] * kWheatPercent / 100, o.wheat)));
	const int palms = std::max(kPalmFloor, int(scaledCount(s.pure[2] * kPalmPercent / 100, o.wood)));
	std::vector<unsigned char> topup(n, 0);
	for (int k = 0; k < teams; ++k)
	{
		// Wheat in a solid arc of the ring nearest the town's swarm (the whole ring is within the
		// growth probe of the lake): a thin band along the water left Numbi's estimate of its food
		// too small to breed on.
		const ShapePoint swarm = turnStencilPoint(L.facings[k], s.swarm);
		const int sx = int(std::floor(L.homes[k].x + swarm.x)), sy = int(std::floor(L.homes[k].y + swarm.y));
		std::vector<std::pair<int, int>> ring;
		for (int i = 0; i < n; ++i)
			if (L.homeOf[i] == k && L.homeKind[i] == kFields && open(i))
				ring.push_back({t.dist2(i % t.w, i / t.w, sx, sy), i});
		std::stable_sort(ring.begin(), ring.end());
		int w = 0;
		for (const auto &[d, i] : ring)
			if (w < wheat)
			{
				map.setResource(i % t.w, i / t.w, WHEAT, 1);
				++w;
			}
		const int p = plantNearWater(WOOD, palms, [&](int i) { return L.homeOf[i] == k && L.homeKind[i] == kGrove; });
		context.telemetry.measure("caravanserai.home.wheat-planted", w, k);
		context.telemetry.measure("caravanserai.home.palms-planted", p, k);
	}
	for (int i = 0; i < n; ++i)
		topup[i] = L.homeKind[i] == kFields || L.homeKind[i] == kGrove;
	// The caravanserais' prizes.
	for (size_t v = 0; v < L.serais.size(); ++v)
	{
		const Serai &serai = L.serais[v];
		const int cx = int(serai.centre.x), cy = int(serai.centre.y);
		const bool alongX = serai.facing % 2 == 0;
		const auto inOasis = [&](int i)
		{ return L.seraiOf[i] == int(v) && L.courtyardOf[i] < 0 && !L.wall[i] && !L.gate[i]; };
		// Groves and quarry on the far side from the pond, off the route.
		const int side = -serai.pondSide;
		for (int fruit = 0; fruit < 4; ++fruit)
		{
			const int along = (fruit - 1) * 5 - 2;
			const int ax = alongX ? cx + along : cx + side * 9, ay = alongX ? cy + side * 9 : cy + along;
			const int seed = seedNear(t, ax, ay, 3, [&](int i) { return inOasis(i) && open(i); });
			if (seed < 0)
				continue;
			if (fruit < 3)
				growPatch(map, t, seed, CHERRY + fruit, int(scaledCount(kGroveTiles, o.fruit)),
						  [&](int i) { return inOasis(i) && open(i); });
			else if (scaledCount(1, o.stone) > 0)
				placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w), STONE,
								   kQuarryRadius);
		}
		plantNearWater(WHEAT, int(scaledCount(kSeraiWheat, o.wheat)), [&](int i) { return inOasis(i) && t.chebyshev(i % t.w, i / t.w, cx, cy) > kSeraiWall + 1; });
		plantNearWater(WOOD, int(scaledCount(kSeraiPalms, o.wood)), [&](int i) { return inOasis(i) && t.chebyshev(i % t.w, i / t.w, cx, cy) > kSeraiWall + 1; });
	}
	// The oases: palms by the pond, a little grain.
	for (size_t q = 0; q < L.oases.size(); ++q)
	{
		const double r = L.oases[q].radius;
		const auto inOasis = [&](int i) { return L.oasisOf[i] == int(q); };
		plantNearWater(WOOD, int(scaledCount(int(r * 1.5), o.wood)), inOasis);
		plantNearWater(WHEAT, int(scaledCount(int(r * 1.2), o.wheat)), inOasis);
	}
	seedAlgae(map, context, t, "caravanserai-algae", o.algae, AlgaeBand::anyWater(30));

	secureStartingCrops(game, context, t, 24, 32, 0, &structural, &topup);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&structural);
	context.stage = "caravanserai routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1}, 0, &structural);
	return true;
}

std::vector<int> grassReach(const Map &map, const Torus &t, const std::vector<unsigned char> &from)
{
	const int n = t.size();
	std::vector<unsigned char> open(n, 0), source(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		open[i] = map.isGrass(x, y) && !(map.isResource(x, y) && map.getResource(x, y).type == STONE);
		source[i] = from[i] && open[i];
	}
	return stepsFrom(t, source, open);
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	if (const std::string mismatch = designMismatch(L, map, "caravanserai"); !mismatch.empty())
		return mismatch;
	if (const std::string broken = wallStanding(map, t, L.wall, L.gate, "caravanserai wall");
		!broken.empty())
		return broken;
	if (!L.serais.empty())
	{
		std::vector<int> pieces(n, -1);
		for (int i = 0; i < n; ++i)
			pieces[i] = L.courtyardOf[i] >= 0 ? L.courtyardOf[i]
						: (!L.wall[i] && !L.gate[i]) ? int(L.serais.size())
													 : -1;
		if (pieceLeak(map, t, pieces, L.gate) >= 0)
			return "A caravanserai can be entered other than by its gates.";
	}
	// The field ring and the grove keep their crops off the town and off each other.
	for (const signed char kind : {kFields, kGrove})
	{
		std::vector<unsigned char> part(n, 0);
		for (int i = 0; i < n; ++i)
			part[i] = L.homeKind[i] == kind;
		const std::vector<int> reach = grassReach(map, t, part);
		for (int i = 0; i < n; ++i)
			if (reach[i] >= 0 && !part[i])
				return kind == kFields ? "A home's fields are open to its town."
									   : "A home's palm grove is open to its fields or town.";
	}
	if (const ColonyWalk walk = walkFromFirstColony(map, teams, "the desert", "across the sand");
		!walk.error.empty())
		return walk.error;
	if (!L.serais.empty() && teams > 1)
	{
		std::vector<unsigned char> gates(n, 0);
		for (int i = 0; i < n; ++i)
			gates[i] = L.gate[i];
		if (const std::string uneven = unevenCosts(costsToTarget(map, teams, gates, StepCosts::walking()),
												   kWalkTolerance, "a caravanserai");
			!uneven.empty())
			return uneven;
	}
	return startingAccessFailure(map, teams, {{WHEAT, 24, "wheat"}, {WOOD, 32, "wood"}}, 16, 24);
}
} // namespace

CaravanseraiOptions::CaravanseraiOptions(const GenerationRequest &r)
	: oasisSize(r.option("oasis-size")), oases(r.option("oases")),
	  caravanserais(r.option("caravanserais")), desert(r.option("desert")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition caravanseraiDefinition()
{
	return {
			"caravanserai",
			41,
			"Caravanserai",
			2,
			false,
			// Home oasis size is the town's radius: 24 holds a lake, its field ring and a town of
			// 60-odd build sites. Two caravanserais per colony: one to each of its two nearest
			// neighbours on a square lattice's row and column.
			{{"oasis-size", "Home oasis size", 18, 30, 2, 24, ControlGroup::Layout},
			 GeneratorControl::choice("oases", "Oases", {"Sparse", "Normal", "Many"}, 1),
			 {"caravanserais", "Caravanserais per colony", 1, 3, 1, 2, ControlGroup::Layout},
			 GeneratorControl::choice("desert", "Desert", {"Open erg", "Dunes and mesas", "Canyon country"}, 1),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			{"terrain:natural", "feature:desert", "feature:oases", "style:sprawling"}};
}
