// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaravanseraiGenerator.h"
#include "ResourceSemantics.h"
#include "BuildingType.h"
#include "Contact.h"
#include "Drawing.h"
#include "GenerationWorld.h"
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
#include "Rock.h"
#include "Room.h"
#include "Routes.h"
#include "Settlements.h"
#include "Sketch.h"
#include "TerrainPropertiesLayout.h"
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
// WHAT IT LOOKS LIKE. A desert seen from the air: dunes in long parallel bands, wind-rippled on
// their crests and pale in the hollows between them, sparse dry scrub in the hollows, flat-topped
// rock mesas standing out of the sand on skirts of scree, and scattered oases, each an irregular
// pool ringed with spring meadow, lush grass and date palms drying out towards the sand. Every
// colony's home is a large oasis town round its own lake, its shore a ring of fields and a palm
// grove behind a sand path, the town beyond. Half way between neighbouring towns stands a
// caravanserai: a square walled courtyard, paved, with a gate at either end and a well in the
// middle, in an oasis of its own with orchards and a quarry. The caravan routes are roads: packed
// trails or sandstone flags from every town's edge to the caravanserais' gates. (The first version
// was flat sand with perfect circles in straight rows, 28 water tiles and 80 of wood on the whole
// map: nothing read as a desert and no colony could build its way along the chain.)
//
// HOW IT PLAYS. Sand holds no building and no crop, so the desert can be crossed but never
// settled: every forward inn, tower or barracks stands on an oasis or beside a road, and the oases
// are what a colony expands along. Home is rich - a lake, wheat and palms on its shore, a town's
// worth of building room - and everything else is out in the sand: the stone and fruit are at the
// caravanserais, which every pair of neighbours shares at equal distance, and in the quarry faces
// cut into the mesas; the oases between add forward ground, grain and palms. The roads are the
// map's logistics (revision 5): path ground, twice walking speed, buildable and never overgrown,
// so a colony's carriers and armies move along the trade route far faster than across the dunes,
// and an inn or tower built on the road holds it. Mesas are walls of rock (towers shoot over them)
// the routes go round; their stone is quarried only from faces cut into their flanks.
//
// THE DESERT CONTROL is the country: an open erg of dunes with no rock and earth trails; dunes and
// mesas, sandstone hoodoos on scree with flagstone roads; or canyon country, twice the rock in
// grey ridges rimmed with hoodoos, standing on slow hardpan floors that only the roads cross
// quickly. Every look is drawn inside its mechanic's rule group (composeLooks), so dune sand plays
// as sand, and the homes, the kit areas and every tile a shared helper reads stay their canonical
// types: plain grass, sand and water.
//
// FAIRNESS. The towns stand on a lattice and are one stencil stamped by one quarter turn drawn per
// map, so every home oasis, lake, field ring and palm grove is identical, and the validator proves
// it by comparing every home tile's rules with colony 0's. Each caravanserai is the midpoint of two
// neighbours, and the oases on the way to it are placed along each colony's straight route at the
// same spacing; every colony's road to a caravanserai it shares is its own cheapest walk to its own
// gate. The scattered oases, mesas, quarries and dunes are noise, and the lobby's best-of-five start
// scoring covers what they leave uneven. The validator proves every colony's walk to its nearest
// caravanserai is within a tolerance of every other's.
//
// WHAT THE PALETTE MUST NOT DO (Bajada 2.0's lessons). Barren ground inhibits growth like sand and
// would be buildable desert, so there is none. Rough ground (scree, hardpan) holds nothing and does
// not inhibit, so it replaces sand only where nothing should grow anyway. Fertile meadow is only on
// oasis shores. Quarry faces and the ring before them stay plain grass, as do the homes and the
// caravanserai walls' corners, so every stone stands where it was put.
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
// Caravan roads bend round heavy going: noise kRoadSwayCell tiles across adds up to kRoadSway to a
// desert step's cost (see the routes in design).
constexpr int kRoadSwayCell = 40, kRoadSway = 24;
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
// Quarry faces on the mesas' flanks: one per kEdgePerQuarry tiles of mesa edge at the default
// stone-amount, at least kQuarryGap apart (closer above 100%, never under kLeastQuarryGap), each
// the edge tiles within kQuarryHalf of its centre, where the mesa is at least three tiles thick,
// nearest the routes and oases first.
constexpr int kEdgePerQuarry = 32, kQuarryGap = 12, kQuarryHalf = 2, kLeastQuarryGap = 6;
// Talus: scree one corner out from the rock, two where the fray noise passes, never nearer than
// kRoughClearance to anything a colony needs.
constexpr int kTalusInner = 1, kTalusOuter = 2, kTalusCut = 36000, kTalusCell = 6;
constexpr int kRoughClearance = 3;
// Canyon floors: hardpan on the next share of the relief below the mesas, by the Desert control.
constexpr int kFloorPercent[3] = {0, 0, 12};
// The floors fade out beyond kFloorReach corners from a mesa and within kFloorHomeClearance of a home,
// the relief falling kFloorFalloff a corner (twice that towards a home).
constexpr int kFloorReach = 3, kFloorHomeClearance = 18, kFloorFalloff = 1200;
// Oasis shores: spring meadow on the corners two and three steps from an oasis pond (one step is
// its beach), lush grass out to kLushReach.
constexpr int kMeadowNear = 2, kMeadowFar = 3, kLushReach = 4;
// Dune looks: the middle of each band's crest share is wind-rippled.
constexpr int kCrestLow = 55, kCrestHigh = 85;
// The outer corners of every home's town look dry this far in.
constexpr int kTownRim = 2;

// What each Desert country looks like: only looks, each inside its mechanic's rule group. Mesa rock
// (cores and rims), its talus, the roads, the caravanserais' courtyards.
struct Country
{
	const char *name;
	TerrainType rock, rim, talus, road, court;
};
constexpr std::array<Country, 3> kCountries{{
	{"Open erg", OUTCROP, OUTCROP, SCREE, TRAIL, SANDSTONE},
	{"Dunes and mesas", HOODOOS, HOODOOS, SCREE, SANDSTONE, BRICK_PATH},
	{"Canyon country", RIDGE_ROCK, HOODOOS, SCREE, TRAIL, SANDSTONE},
}};

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
	// The mechanic sketch's extra masks: home vertices (kept plain), the road and courtyard
	// vertices, quarry face tiles, the rock tiles, corners that must stay plain grass (walls,
	// quarries), and every vertex owned by a home, oasis or caravanserai.
	std::vector<unsigned char> homeVertex, road, court, quarry, rockTile, plain, feature;
	std::vector<int> dunePhase; // 0 to 65535 across one dune band, crests in the upper part
	int desert = 1;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const CaravanseraiOptions o(request);
	Layout L;
	L.desert = std::clamp(o.desert, 0, 2);
	context.telemetry.choice("caravanserai.desert", kCountries[L.desert].name);
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	const int offsetX = int(context.bounded("caravanserai-layout", std::uint32_t(t.w)));
	const int offsetY = int(context.bounded("caravanserai-layout", std::uint32_t(t.h)));
	L.homes = latticeSites(t.w, t.h, teams, offsetX, offsetY).sites;
	for (ShapePoint &h : L.homes)
		h = {double(t.remainderX(int(std::lround(h.x)))), double(t.remainderY(int(std::lround(h.y))))};
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
	L.homeVertex.assign(n, 0);
	L.road.assign(n, 0);
	L.court.assign(n, 0);
	L.quarry.assign(n, 0);
	L.rockTile.assign(n, 0);
	L.plain.assign(n, 0);
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
					L.homeVertex[v] = 1;
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
			// The courtyard is paved on every corner no wall tile touches, the well in the middle.
			for (int dy = 1 - kSeraiWall + 1; dy < kSeraiWall; ++dy)
				for (int dx = 1 - kSeraiWall + 1; dx < kSeraiWall; ++dx)
				{
					L.sketch[t.at(cx + dx, cy + dy)] = TRAIL;
					L.court[t.at(cx + dx, cy + dy)] = 1;
				}
			for (int dy = 0; dy < kWellCorners; ++dy)
				for (int dx = 0; dx < kWellCorners; ++dx)
				{
					L.sketch[t.at(cx + dx, cy + dy)] = WATER;
					L.court[t.at(cx + dx, cy + dy)] = 0;
				}
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
		const ShapePoint p{double(t.remainderX(i)), double(i / t.w)};
		if (!clearOf(p, r))
			continue;
		stampOasis(p, r);
		++scattered;
	}
	context.telemetry.measure("caravanserai.oases.route", routeOases);
	context.telemetry.measure("caravanserai.oases.scattered", scattered);

	// The caravan routes: from every home to the gate facing it of each caravanserai it shares,
	// round the features and never through a courtyard or its walls.
	std::vector<unsigned char> featureTile(n, 0), wallTile(n, 0);
	for (int i = 0; i < n; ++i)
	{
		featureTile[i] = L.homeOf[i] >= 0 || L.seraiOf[i] >= 0 || L.oasisOf[i] >= 0 || L.courtyardOf[i] >= 0 ||
						 L.wall[i] || L.gate[i];
		wallTile[i] = L.courtyardOf[i] >= 0 || L.wall[i] || L.gate[i];
	}
	// Every corner of a feature's tiles is the feature's: nothing drawn round it may touch them.
	{
		const std::vector<unsigned char> corners = tileCorners(t, featureTile);
		for (int i = 0; i < n; ++i)
			feature[i] = feature[i] || corners[i];
	}
	const std::vector<unsigned char> wallCorner = tileCorners(t, wallTile);
	const std::vector<int> fromFeature = stepsFrom(t, feature);
	const std::vector<int> relief = fractalNoise(t.w, t.h, 48, 3, context.stream("caravanserai-mesas"));
	// The going: broad swells of noise kRoadSwayCell tiles across, up to kRoadSway on a step that
	// costs 14 in the open, so a caravan route bends round the heavy going as a track across real
	// sand does (the relief alone, at most 7, drew roads ruler-straight). Its own stream, so the mesas
	// and dunes draw exactly as before.
	const std::vector<int> going = fractalNoise(t.w, t.h, kRoadSwayCell, 2, context.stream("caravanserai-going"));
	for (const Serai &serai : L.serais)
		for (const int k : {serai.a, serai.b})
		{
			const int cx = int(serai.centre.x), cy = int(serai.centre.y);
			const bool alongX = serai.facing % 2 == 0;
			const int offset = alongX ? t.offsetX(cx, int(L.homes[k].x)) : t.offsetY(cy, int(L.homes[k].y));
			const int side = offset > 0 || (offset == 0 && k == serai.b) ? 1 : -1;
			const int gate = alongX ? t.at(cx + side * (kSeraiWall + 1), cy) : t.at(cx, cy + side * (kSeraiWall + 1));
			const std::vector<int> path = cheapestWalk(
				t, GridNeighbors::Eight, {t.at(int(L.homes[k].x), int(L.homes[k].y))}, tileMask(t, {gate}),
				[&](int, int to, int dx, int dy)
				{
					return (dx && dy ? 14 : 10) + (L.oasisOf[to] >= 0 ? 0 : 4) + relief[to] / 8192 +
						   (L.oasisOf[to] >= 0 ? 0 : going[to] * kRoadSway / 65536) + (wallTile[to] ? 100000 : 0);
				});
			// The road: the desert part of the walk, and its way across the caravanserai's oasis.
			for (int i : path)
				if (!featureTile[i] || (L.seraiOf[i] >= 0 && !wallTile[i]))
					L.route[i] = 1;
		}
	{
		// A road tile takes path on all four corners, except corners a home, an oasis or a wall
		// owns, and the caravanserai's oasis, which the road crosses to its gate.
		const std::vector<unsigned char> corners = tileCorners(t, L.route);
		for (int i = 0; i < n; ++i)
			if (corners[i] && L.sketch[i] != WATER && !wallCorner[i] && (!feature[i] || L.seraiOf[i] >= 0) &&
				!L.homeVertex[i])
			{
				L.road[i] = 1;
				L.sketch[i] = TRAIL;
			}
		// Through every gate into the courtyard: the two corners down the middle of a gate touch no
		// wall tile, so the road runs on into the paving.
		for (const Serai &serai : L.serais)
		{
			const int cx = int(serai.centre.x), cy = int(serai.centre.y);
			const bool alongX = serai.facing % 2 == 0;
			for (int along = -(kSeraiWall + 1); along <= kSeraiWall + 2; ++along)
				for (int across = 0; across <= 1; ++across)
				{
					const int i = alongX ? t.at(cx + along, cy + across) : t.at(cx + across, cy + along);
					if (L.sketch[i] != WATER && !L.court[i])
					{
						L.road[i] = 1;
						L.sketch[i] = TRAIL;
					}
				}
		}
	}
	std::vector<unsigned char> keepClear = feature;
	for (int i = 0; i < n; ++i)
		keepClear[i] = keepClear[i] || L.route[i] || L.road[i];
	const std::vector<int> fromKept = stepsFrom(t, keepClear);

	// Mesas on the high ground of the relief, clear of everything a colony needs.
	std::vector<unsigned char> raised(n, 0);
	std::vector<int> samples;
	for (int i = 0; i < n; ++i)
		if (fromKept[i] >= kMesaClearance)
			samples.push_back(relief[i]);
	const auto cutFor = [&](int percent)
	{
		// The share of the whole map, taken from the eligible desert.
		const int share = std::min(100, int(std::int64_t(percent) * n / std::int64_t(std::max<size_t>(1, samples.size()))));
		return percentile(samples, 100 - share);
	};
	if (kMesaPercent[L.desert] > 0 && !samples.empty())
	{
		const int cut = cutFor(kMesaPercent[L.desert]);
		for (int i = 0; i < n; ++i)
			raised[i] = fromKept[i] >= kMesaClearance && relief[i] >= cut;
	}
	// A mesa is the tiles whose four corners it raised.
	int mesaTiles = 0;
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		bool rock = true;
		for (const int j : {i, t.at(x + 1, y), t.at(x, y + 1), t.at(x + 1, y + 1)})
			rock = rock && raised[j];
		L.mesa[i] = rock;
		mesaTiles += rock;
	}
	context.telemetry.measure("caravanserai.mesa.tiles", mesaTiles);

	// Quarry faces cut into the mesas' flanks, the stone of the desert: edge tiles where the mesa is
	// thick enough to stay a wall behind them, the faces nearest a home first (half weight on the
	// nearest road or oasis), spaced apart, so every colony's nearest mesa is quarried on its side.
	std::vector<unsigned char> offMesa(n, 0);
	for (int i = 0; i < n; ++i)
		offMesa[i] = !L.mesa[i];
	const std::vector<int> fromOff = stepsFrom(t, offMesa), fromHomes = stepsFrom(t, L.homeVertex);
	std::vector<std::pair<int, int>> faces;
	int edge = 0;
	for (int i = 0; i < n; ++i)
		if (L.mesa[i] && fromOff[i] == 1)
		{
			++edge;
			faces.push_back(
				{(2 * fromHomes[i] + fromKept[i]) * 16 + int(context.bounded("caravanserai-quarries", 16)), i});
		}
	std::stable_sort(faces.begin(), faces.end());
	const int wantedQuarries = int(scaledCount(edge / kEdgePerQuarry, o.stone));
	const int quarryGap = std::max(kLeastQuarryGap, kQuarryGap * 100 / std::max(100, o.stone));
	std::vector<int> quarrySites;
	int quarryTiles = 0;
	for (const auto &[rank, i] : faces)
	{
		if (int(quarrySites.size()) >= wantedQuarries)
			break;
		const int x = t.remainderX(i), y = i / t.w;
		bool spaced = true;
		for (const int site : quarrySites)
			spaced = spaced && t.chebyshev(x, y, t.remainderX(site), site / t.w) >= quarryGap;
		bool thick = false;
		for (int dy = -3; dy <= 3 && !thick; ++dy)
			for (int dx = -3; dx <= 3 && !thick; ++dx)
				thick = fromOff[t.at(x + dx, y + dy)] >= 3;
		if (!spaced || !thick)
			continue;
		quarrySites.push_back(i);
		// quarryNear(t, L.mesa, offMesa, i, kQuarryHalf, 1), with the steps already taken.
		for (int dy = -kQuarryHalf; dy <= kQuarryHalf; ++dy)
			for (int dx = -kQuarryHalf; dx <= kQuarryHalf; ++dx)
			{
				const int q = t.at(x + dx, y + dy);
				if (L.mesa[q] && fromOff[q] == 1 && !L.quarry[q])
				{
					L.quarry[q] = 1;
					++quarryTiles;
				}
			}
	}
	context.telemetry.measure("caravanserai.quarries", int(quarrySites.size()));
	context.telemetry.measure("caravanserai.quarry.tiles", quarryTiles);
	// The rock: mesa tiles that are not quarry faces. Raised corners that are not rock (a mesa's
	// convex corners, a sliver too thin to stand) are scree, and talus skirts the rock.
	for (int i = 0; i < n; ++i)
		L.rockTile[i] = L.mesa[i] && !L.quarry[i];
	const std::vector<unsigned char> rock = rockVertices(t, L.rockTile);
	std::vector<unsigned char> eligible(n, 0);
	for (int i = 0; i < n; ++i)
		eligible[i] = !raised[i] && fromKept[i] >= kRoughClearance && L.sketch[i] == SAND;
	const std::vector<unsigned char> talus =
		talusApron(t, rock, eligible, periodicNoise(t.w, t.h, kTalusCell, context.stream("caravanserai-talus")),
				   kTalusInner, kTalusOuter, kTalusCut);
	int rockCorners = 0, scree = 0;
	for (int i = 0; i < n; ++i)
	{
		if (rock[i])
		{
			L.sketch[i] = RIDGE_ROCK;
			++rockCorners;
		}
		else if (raised[i] || talus[i])
		{
			L.sketch[i] = SCREE;
			++scree;
		}
	}
	// Every quarry tile keeps four plain grass corners, and the ring before it is grass too (its
	// corners beyond the quarry's may look dry).
	{
		const std::vector<unsigned char> approach = quarryApproach(t, L.quarry), corners = tileCorners(t, L.quarry);
		for (int i = 0; i < n; ++i)
			if ((L.quarry[i] || approach[i]) && !rock[i])
			{
				L.sketch[i] = GRASS;
				L.plain[i] = corners[i];
			}
	}
	for (int i = 0; i < n; ++i)
		if (wallCorner[i] && L.sketch[i] == GRASS)
			L.plain[i] = 1;
	context.telemetry.measure("caravanserai.mesa.rock-corners", rockCorners);
	context.telemetry.measure("caravanserai.talus.corners", scree);
	// Canyon floors: slow hardpan on the next band of the relief below the mesas.
	int hardpan = 0;
	if (kFloorPercent[L.desert] > 0 && !samples.empty())
	{
		// The floors lie between and round the mesas, never in a home's dooryard, where they would
		// slow one colony's opening and not another's.
		const int cut = cutFor(kMesaPercent[L.desert] + kFloorPercent[L.desert]);
		const std::vector<int> fromRaised = stepsFrom(t, raised), fromHome = stepsFrom(t, L.homeVertex);
		// The relief sinks away from the mesas and towards the homes, so the floors' edges follow
		// its contours rather than a distance's squares.
		const auto floorRelief = [&](int i)
		{
			const int away = fromRaised[i] < 0 ? 1000 : std::max(0, fromRaised[i] - kFloorReach);
			const int nearby = fromHome[i] < 0 ? 0 : std::max(0, kFloorHomeClearance - fromHome[i]);
			return relief[i] - away * kFloorFalloff - nearby * kFloorFalloff * 2;
		};
		for (int i = 0; i < n; ++i)
			if (L.sketch[i] == SAND && fromKept[i] >= kRoughClearance && floorRelief(i) >= cut)
			{
				L.sketch[i] = HARDPAN;
				++hardpan;
			}
	}
	context.telemetry.measure("caravanserai.floor.corners", hardpan);

	// Dunes: scrub in the hollows between the bands, a single corner at a time.
	StripeStyle dunes;
	dunes.acrossX = 1 + int(context.bounded("caravanserai-dunes", 4));
	dunes.acrossY = int(context.bounded("caravanserai-dunes", 3)) * (context.bounded("caravanserai-dunes", 2) ? 1 : -1);
	dunes.warpPercent = 35;
	std::mt19937 &duneStream = context.stream("caravanserai-dunes");
	const std::vector<int> phase = stripePhase(t, dunes, duneStream);
	// Many dune bands to one stripe of the field: a whole number, so the bands still wrap.
	const int bands = std::max(1, int(std::lround(stripeSpacing(t, dunes) / kDuneSpacing)));
	L.dunePhase.assign(n, 0);
	int scrub = 0;
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		L.dunePhase[i] = int((std::int64_t(phase[i]) * bands) % 65536);
		const bool hollow = L.dunePhase[i] < 65536 * kHollowPercent / 100;
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

	// Oasis shores: spring meadow two and three corners from a pond, in the scattered and route
	// oases and the caravanserais' (never in a courtyard, by a wall or in a home).
	int meadow = 0;
	{
		std::vector<unsigned char> water(n, 0);
		for (int i = 0; i < n; ++i)
			water[i] = L.sketch[i] == WATER;
		const std::vector<int> fromWater = stepsFrom(t, water);
		for (int i = 0; i < n; ++i)
			if (L.sketch[i] == GRASS && !L.homeVertex[i] && !wallCorner[i] && L.courtyardOf[i] < 0 &&
				(L.oasisOf[i] >= 0 || L.seraiOf[i] >= 0) && fromWater[i] >= kMeadowNear &&
				fromWater[i] <= kMeadowFar)
			{
				L.sketch[i] = SPRING_MEADOW;
				++meadow;
			}
	}
	int roadCorners = 0;
	for (int i = 0; i < n; ++i)
		roadCorners += L.road[i];
	context.telemetry.measure("caravanserai.meadow.corners", meadow);
	context.telemetry.measure("caravanserai.road.corners", roadCorners);
	L.feature = feature;
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

bool generate(GenerationWorld &game, GenerationContext &context)
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
	GenerationMap &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "caravanserai terrain";
	TerrainSketch terrain = L.sketch;
	layBeaches(terrain, t);
	{
		// The looks, each inside its vertex's rule group so nothing plays differently: oasis grass
		// lush by the water and dry at the sand's edge, scrub dry, dunes rippled on their crests and
		// pale in the hollows, the country's rock with its rims, talus and roads, the courtyards
		// paved. The homes, the quarries and the walls' corners stay plain, as do the beaches.
		const Country &country = kCountries[L.desert];
		std::vector<unsigned char> wet(n, 0), open(n, 0), desert(n, 0);
		for (int i = 0; i < n; ++i)
		{
			wet[i] = terrain[i] == WATER;
			open[i] = terrain[i] != RIDGE_ROCK;
			desert[i] = !L.feature[i];
		}
		const std::vector<int> nearWater = stepsFrom(t, wet), inRock = stepsFrom(t, open),
							   inOasis = stepsFrom(t, desert);
		TerrainSketch looks(n, 0);
		for (int i = 0; i < n; ++i)
		{
			// A home is plain grass but for the dry outer rim of its town, the same on every home.
			if (L.homeVertex[i] && terrain[i] == GRASS && L.homeKind[i] == kTown && inOasis[i] <= kTownRim)
				looks[i] = DRY_GRASS;
			if (L.homeVertex[i] || L.plain[i])
				continue;
			switch (terrain[i])
			{
			case GRASS:
				if (!L.feature[i])
					looks[i] = DRY_GRASS;
				else if (nearWater[i] >= 0 && nearWater[i] <= kLushReach)
					looks[i] = LUSH_GRASS;
				else if (inOasis[i] <= 1)
					looks[i] = DRY_GRASS;
				break;
			case SAND:
				if (L.feature[i] || (nearWater[i] >= 0 && nearWater[i] <= 1))
					break;
				looks[i] = L.dunePhase[i] < 65536 * kHollowPercent / 100 ? PALE_SAND
						   : L.dunePhase[i] >= 65536 * kCrestLow / 100 && L.dunePhase[i] < 65536 * kCrestHigh / 100
							   ? WINDY_SAND_DUNE
							   : DUNE_SAND;
				break;
			case RIDGE_ROCK:
				looks[i] = inRock[i] <= 1 ? country.rim : country.rock;
				break;
			case SCREE:
				looks[i] = country.talus;
				break;
			case TRAIL:
				looks[i] = L.court[i] ? country.court : country.road;
				break;
			default:
				break;
			}
		}
		writeVertices(map, composeLooks(terrain, looks));
	}
	const DesignedStone walls = designedStone(map, t, L.wall);
	if (walls.gaps)
	{
		context.detail = "a caravanserai wall has a gap";
		return false;
	}
	// The walls are the caravanserais' stone; the mesas' is on their quarry faces.
	std::vector<unsigned char> structural = walls.stone;
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if (walls.stone[i])
			map.setResourceByIndex(x, y, STONE, 1);
		else if (L.quarry[i] && map.isResourceAllowed(x, y, STONE))
		{
			map.setResourceByIndex(x, y, STONE, 1);
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
		waterTiles[i] = terrainProvidesFertility(map.terrainPropertiesAt(t.remainderX(i), i / t.w));
	const std::vector<int> fromWater = stepsFrom(t, waterTiles);
	const auto open = [&](int i, int type)
	{ return map.terrainSupportsResourceAtByIndex(t.remainderX(i), i / t.w, type) && !reserved[i] && clearGround(map, t.remainderX(i), i / t.w); };
	// Plant `count` of `type` on the tiles `where` allows, nearest the water first.
	const auto plantNearWater = [&](int type, int count, auto where)
	{
		std::vector<std::pair<int, int>> tiles;
		for (int i = 0; i < n; ++i)
			if (where(i) && open(i, type) && fromWater[i] >= 0)
				tiles.push_back({fromWater[i], i});
		std::stable_sort(tiles.begin(), tiles.end());
		int placed = 0;
		for (const auto &[d, i] : tiles)
		{
			if (placed >= count)
				break;
			map.setResourceByIndex(t.remainderX(i), i / t.w, type, 1);
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
			if (L.homeOf[i] == k && L.homeKind[i] == kFields && open(i, WHEAT))
				ring.push_back({t.dist2(t.remainderX(i), i / t.w, sx, sy), i});
		std::stable_sort(ring.begin(), ring.end());
		int w = 0;
		for (const auto &[d, i] : ring)
			if (w < wheat)
			{
				map.setResourceByIndex(t.remainderX(i), i / t.w, WHEAT, 1);
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
			const int seed = seedNear(t, ax, ay, 3, [&](int i) { return inOasis(i) &&
				open(i, fruit < 3 ? CHERRY + fruit : STONE); });
			if (seed < 0)
				continue;
			if (fruit < 3)
				growPatch(map, t, seed, CHERRY + fruit, int(scaledCount(kGroveTiles, o.fruit)),
						  [&](int i) { return inOasis(i) && open(i, fruit < 3 ? CHERRY + fruit : STONE); });
			else if (scaledCount(1, o.stone) > 0)
				placeResourceClump(map, context, MapGeneratorPoint(t.remainderX(seed), seed / t.w), STONE,
								   kQuarryRadius);
		}
		plantNearWater(WHEAT, int(scaledCount(kSeraiWheat, o.wheat)), [&](int i) { return inOasis(i) && t.chebyshev(t.remainderX(i), i / t.w, cx, cy) > kSeraiWall + 1; });
		plantNearWater(WOOD, int(scaledCount(kSeraiPalms, o.wood)), [&](int i) { return inOasis(i) && t.chebyshev(t.remainderX(i), i / t.w, cx, cy) > kSeraiWall + 1; });
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

std::vector<int> grassReach(const GenerationMap &map, const Torus &t,
							const std::vector<unsigned char> &from)
{
	const int n = t.size();
	std::vector<unsigned char> open(n, 0), source(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		open[i] = (map.canResourcesGrow(x, y) && (map.terrainSupportsResourceAtByIndex(x, y, WHEAT) ||
			map.terrainSupportsResourceAtByIndex(x, y, WOOD))) && !permanentResourceBarrier(map, i);
		source[i] = from[i] && open[i];
	}
	return stepsFrom(t, source, open);
}

std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const GenerationMap &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	if (const std::string mismatch = designMismatch(L, map, "caravanserai"); !mismatch.empty())
		return mismatch;
	if (const std::string broken = wallStanding(map, t, L.wall, L.gate, "caravanserai wall");
		!broken.empty())
		return broken;
	// Every quarry face cut into a mesa still holds its stone.
	if (const std::string fault = quarryFault(map, t, L.quarry, std::vector<int>(n, -1), teams, 0); !fault.empty())
		return fault;
	// quarryFault passes a tile that cannot hold stone; here every face tile must, since the design keeps
	// its corners and the ring before it grass (a spoiled one would be a hole in the mesa, not a quarry).
	for (int i = 0; i < n; ++i)
		if (L.quarry[i] && !(map.isResource(t.remainderX(i), i / t.w) &&
							 map.getResource(t.remainderX(i), i / t.w).type == STONE))
			return "A quarry face at (" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) +
				   ") holds no stone.";
	const auto at = [&](int i) { return std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w); };
	// Every home is colony 0's, tile for tile, by the rules each tile plays by (the looks round it
	// may differ; its rules may not). One facing makes every home a translation of the first.
	for (int k = 1; k < teams; ++k)
	{
		const int dx = int(L.homes[k].x) - int(L.homes[0].x), dy = int(L.homes[k].y) - int(L.homes[0].y);
		for (int i = 0; i < n; ++i)
		{
			if (L.homeOf[i] != 0)
				continue;
			const int j = t.at(t.remainderX(i) + dx, i / t.w + dy);
			if (L.homeOf[j] != k || L.homeKind[j] != L.homeKind[i] ||
				!sameTerrainProperties(map.terrainPropertiesAt(t.remainderX(i), i / t.w),
									   map.terrainPropertiesAt(t.remainderX(j), j / t.w)))
				return "Colony " + std::to_string(k) + "'s home oasis differs from colony 0's at (" + at(j) + ").";
		}
	}
	// The caravan roads are path: every tile all of whose corners the design paved plays as path,
	// and nothing stands on the mesas' rock.
	TerrainSketch beached = L.sketch;
	layBeaches(beached, t);
	const std::vector<unsigned char> roadTiles = pureTiles(beached, t, TRAIL);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		const TerrainProperties &rules = map.terrainPropertiesAt(x, y);
		if (roadTiles[i] && L.court[i] == 0 && L.road[i] &&
			!(rules.walkable && rules.buildable && rules.groundSpeedQ8 == 512 && !rules.resourcesGrow))
			return "A caravan road is not a road at (" + at(i) + ").";
		if (L.rockTile[i] && map.isResource(x, y))
			return "Something stands on a mesa's rock at (" + at(i) + ").";
	}
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
	return startingAccessFailure(map, teams, {{MaterialId::Food, 24, "food"}, {MaterialId::Wood, 32, "wood"}}, 16, 24);
}
} // namespace

CaravanseraiOptions::CaravanseraiOptions(const GenerationRequest &r)
	: oasisSize(r.option("oasis-size")), oases(r.option("oases")),
	  caravanserais(r.option("caravanserais")), desert(r.option("desert")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
	if (desert == 3)
		desert = GenerationContext::choiceFromSeed(
			r.seed, "caravanserai/desert",
			GenerationRequest::control(r.method, "desert").searchValues());
}

GeneratorDefinition caravanseraiDefinition()
{
	return {"caravanserai",
			41,
			"Caravanserai",
			// Revision 5 (2.0): mesas of rock with quarry faces, roads of path, paved courtyards,
			// meadow shores, dune and country looks.
			5,
			false,
			// Home oasis size is the town's radius: 24 holds a lake, its field ring and a town of
			// 60-odd build sites. Two caravanserais per colony: one to each of its two nearest
			// neighbours on a square lattice's row and column.
			{GeneratorControl{"oasis-size", "Home oasis size", 18, 30, 2, 24, ControlGroup::Layout}
				 .withSearchRange(24, 30),
			 GeneratorControl::choice("oases", "Oases", {"Sparse", "Normal", "Many"}, 1)
				 .withSearchValues({0, 1, 2}),
			 GeneratorControl{"caravanserais", "Caravanserais per colony", 1, 3, 1, 2,
							  ControlGroup::Layout}
				 .withSearchRange(1, 3),
			 GeneratorControl::choice(
				 "desert", "Desert", {"Open erg", "Dunes and mesas", "Canyon country", "Random"}, 3)
				 .withSearchValues({0, 1, 2}),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			{"terrain:natural", "feature:desert", "feature:oases", "style:sprawling",
			 "fairness:stamped-lattice"}};
}
