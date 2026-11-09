// SPDX-License-Identifier: GPL-3.0-or-later
#include "GauntletGenerator.h"
#include "Climates.h"
#include "ResourceSemantics.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Growth.h"
#include "Room.h"
#include "Grid.h"
#include "Farmland.h"
#include "FertilityField.h"
#include "Homes.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Topology.h"
#include "Walls.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
using namespace MapGeneration;

// The Gauntlet: two guarded doors per home, each opening into a different fighting court.
// The courts form a circuit round a sealed lake, independently of the homes. Thin partitions
// separate neighbouring courts: towers shoot over them, armies use their offset gates. There
// is no capture mechanic. Holding a court means feeding and defending an army there.
// Home farms are capped irrigated rows; paved circulation and barren caps contain growth.
// The repeated angular design is measured, not claimed to be exact integer-tile symmetry.
//
// THE GROUND (revision 5). Every wall is terrain from the catalogue (TerrainGroup.h) where earlier
// revisions stood stone on grass: the partitions between courts are rock towers shoot over
// (Ridges), as stone was; the outer arena ring, the borders between homes and both coasts are
// cliffs (Obstacles), which no tower shoots through. A coast is a band of cliff from the water's edge
// to where the old sealing ring stood, so there is no beach to land on and no lane behind it. The
// promenade and the approaches are paved (Paths), with rough ground in every door so no building
// closes it.
// Every home has a quarry face of fixed size at the inside foot of its outer wall, between its two
// doors: the towers' only stone. The climate (Climates.h) paints the looks last.
namespace
{
constexpr double kTau = 2 * kPi;
constexpr double kInnerCourtWidth = 27;
constexpr double kOuterCourtWidth = 48;
constexpr double kLakeCourtGap = 7;
constexpr double kHomeFarmDepth = 22;
// A coast's cliff band reaches this far inland from the water, where the sealing ring's inner edge
// stood; the quarry face's size; open water this many vertices from land is deep.
constexpr double kCoastBand = 6;
constexpr int kQuarryFace = 16;
// Every door's tower also has a face of this much stone at the foot of the outer wall, seeded within
// this many tiles of the tower and kept this many steps from the door (plantTowerStone).
constexpr int kTowerStone = 6;
constexpr int kTowerStoneReach = 10;
constexpr int kTowerStoneDoorGap = 3;
// The validator wants 16 anchors of permanent building room within kRoomReach steps of each home's
// workers; the tower stone keeps clear of the nearest kKeptRoom of them, a margin of eight.
constexpr int kRoomReach = 24, kKeptRoom = 24;
constexpr int kDeepFrom = 5;
// A home's paved square reaches this far round its swarm, and its walls keep a grass verge this many
// vertices wide, where its quarry face stands (barren ground holds no stone).
constexpr double kCore = 7;
constexpr int kVerge = 4;
struct Geometry
{
	int width, height, teams;
	double half, wedge, lake, inner, outer, rim, home, frontAngle;
	std::string failure;
};
Geometry geometryFor(const GenerationRequest &r)
{
	const GauntletOptions o(r);
	Geometry g;
	g.width = 1 << r.wDec;
	g.height = 1 << r.hDec;
	g.teams = r.nbTeams;
	g.half = std::min(g.width, g.height) / 2.0;
	g.wedge = kTau / std::max(1, g.teams);
	g.lake = g.half * .22;
	g.inner = g.lake + kLakeCourtGap;
	// More courts need a longer inner circumference, not narrower defended fronts.
	// Keep the original geometry for the previously supported counts.
	if (g.teams > DENSE_COLONY_BASE_LIMIT)
	{
		g.inner = std::max(g.inner, (kInnerCourtWidth + 1) / g.wedge);
		g.lake = g.inner - kLakeCourtGap;
	}
	g.outer = g.inner + std::max(30., g.half * .27 * o.courtSize / 100.);
	g.rim = g.half - 4;
	g.home = g.outer + 18;
	// Keep both fronts within a compact defended frontage, including sparse large arenas.
	g.frontAngle = std::min(g.wedge * .25, 22. / g.outer);
	if (g.half < 128 || g.teams < 2 || g.teams > TEAM_MAX_COUNT ||
		g.outer * g.wedge < kOuterCourtWidth || g.inner * g.wedge < kInnerCourtWidth ||
		g.rim - g.home < kHomeFarmDepth)
		g.failure =
			"The Gauntlet needs a larger map or fewer colonies to fit its two guarded fronts.";
	if (const auto error = denseColonySizeFailure(r); !error.empty())
		g.failure = error;
	return g;
}
struct Layout
{
	Torus t{1, 1};
	Geometry g;
	double phase = 0, gateRadius = 0;
	int kind = 0, climate = 0;
	std::vector<double> radius, angle;
	std::vector<int> homeOf, courtOf, anchor, frontOf, teamSlot;
	std::vector<unsigned char> wall, road, farmRegion;
	// The walls towers shoot over (the partitions), and the coasts' cliff band.
	std::vector<unsigned char> partition, coast;
	std::string failure;
};
ShapePoint point(const Layout &L, double r, double a)
{
	return polarPoint(L.t.w / 2., L.t.h / 2., r, L.phase + a);
}
Layout design(const GenerationRequest &r, GenerationContext &c)
{
	Layout L;
	L.g = geometryFor(r);
	L.failure = L.g.failure;
	if (!L.failure.empty())
		return L;
	L.t = Torus(L.g.width, L.g.height);
	const auto &g = L.g;
	const auto &t = L.t;
	const int n = t.size();
	const GauntletOptions o(r);
	L.phase = c.bounded("gauntlet-phase", 3600) * kTau / 3600.;
	L.kind = c.bounded("gauntlet-court-kind", 3);
	L.climate = drawClimate(c, "gauntlet-climate");
	c.telemetry.choice("gauntlet.climate", kClimateLooks[L.climate].name);
	c.telemetry.measure("gauntlet.court.requested-design", L.kind);
	if (L.kind == 2 && g.inner * g.wedge < 45)
	{
		L.kind = 0;
		c.telemetry.fallback("gauntlet.court.unsplit",
							 "Two capped gardens would not fit; using one garden in every court.");
	}
	const double offset = (int(c.bounded("gauntlet-gate-offset", 3)) - 1) * .06;
	L.gateRadius =
		std::clamp(g.inner + (g.outer - g.inner) * (.5 + offset), g.inner + 18, g.outer - 12);
	L.radius.resize(n);
	L.angle.resize(n);
	L.homeOf.assign(n, -1);
	L.courtOf.assign(n, -1);
	L.frontOf.assign(n, -1);
	L.wall.assign(n, 0);
	L.road.assign(n, 0);
	L.farmRegion.assign(n, 0);
	L.partition.assign(n, 0);
	L.coast.assign(n, 0);
	for (int k = 0; k < g.teams; ++k)
	{
		const auto a = point(L, g.home, g.wedge * k);
		L.anchor.push_back(t.at(int(std::lround(a.x)), int(std::lround(a.y))));
	}
	for (int i = 0; i < n; ++i)
	{
		const double x = t.offsetX(t.w / 2, t.remainderX(i)), y = t.offsetY(t.h / 2, i / t.w);
		const double rad = std::hypot(x, y);
		double a = std::fmod(std::atan2(y, x) - L.phase + kTau * 2, kTau);
		L.radius[i] = rad;
		L.angle[i] = a;
		const int home = int(std::floor(a / g.wedge + .5)) % g.teams;
		const int court = std::min(g.teams - 1, int(a / g.wedge));
		double local = a - home * g.wedge;
		if (local > kPi)
			local -= kTau;
		if (local < -kPi)
			local += kTau;
		if (rad > g.outer && rad < g.rim - 3)
			L.homeOf[i] = home;
		if (rad >= g.inner && rad <= g.outer)
			L.courtOf[i] = court;
		// The coasts: cliff from the water's edge to kCoastBand inland, no beach in front of them.
		L.coast[i] = (rad >= g.lake && rad < g.lake + kCoastBand) ||
					 (rad <= g.rim && rad > g.rim - kCoastBand);
		L.wall[i] = L.coast[i];
		if (std::abs(rad - g.outer) <= 1.2)
			L.wall[i] = 1;
		// Each separator follows one of three court families: straight, bent or paired gardens.
		const double bend =
			L.kind == 1 ? 3 * std::sin((rad - g.inner) / (g.outer - g.inner) * kTau) : 0;
		const double separator = rad * std::sin(local) - bend;
		if (rad >= g.inner - 3 && rad <= g.outer + 2 &&
			std::abs(separator) < o.partitionWall * .5 + .4 &&
			std::abs(rad - L.gateRadius) > o.gateWidth * .5)
		{
			L.wall[i] = 1;
			L.partition[i] = 1;
		}
		// A protected winding promenade, with its gate crossings all at the same radius.
		const double promenade =
			L.gateRadius + (L.kind == 0 ? 2. : 4.) * std::sin(a / g.wedge * kTau);
		if (rad >= g.inner && rad < g.outer - 4 && std::abs(rad - promenade) < 2.2)
			L.road[i] = 1;
		// Broad radial approaches stop inside the court; no road cuts across a home farm.
		for (int side : {-1, 1})
		{
			const double da = local - side * g.frontAngle;
			if (std::abs(da) < g.wedge * .2 && std::abs(rad * std::sin(da)) < o.gateWidth * .5 &&
				rad >= L.gateRadius - 6 && rad <= g.outer + 9)
			{
				L.road[i] = 1;
				if (std::abs(rad - g.outer) < 3)
				{
					L.wall[i] = 0;
					L.partition[i] = 0;
					L.frontOf[i] = 2 * home + (side > 0);
				}
			}
		}
		if (L.homeOf[i] >= 0 && rad > g.outer + 13 && rad < g.rim - 9 &&
			t.dist2(t.remainderX(i), i / t.w, t.remainderX(L.anchor[home]), L.anchor[home] / t.w) > 9 * 9 &&
			(rad > g.home + 7 || std::abs(rad * std::sin(local)) > 10))
			L.farmRegion[i] = 1;
	}
	L.teamSlot.resize(g.teams);
	for (int k = 0; k < g.teams; ++k)
		L.teamSlot[k] = k;
	dealStarts(c, L.teamSlot, "gauntlet-deal");
	for (int team = 0; team < g.teams; ++team)
		c.telemetry.measure("gauntlet.home.slot", L.teamSlot[team], team);
	const auto borders = labelBorders(t, L.homeOf, 2);
	for (int i = 0; i < n; ++i)
		L.wall[i] = L.wall[i] || borders[i];
	c.telemetry.choice("gauntlet.court.design", L.kind == 0   ? "jousting"
												: L.kind == 1 ? "bent"
															  : "paired-gardens");
	c.telemetry.measure("gauntlet.court.radial-width", g.outer - g.inner);
	c.telemetry.measure("gauntlet.arena.outer-radius", g.outer);
	c.telemetry.measure("gauntlet.gate.width", o.gateWidth);
	c.telemetry.measure("gauntlet.partition.width", o.partitionWall);
	c.telemetry.measure("gauntlet.gate.radius", L.gateRadius);
	return L;
}

// The walls' vertices, a pure function of the design (so validateWorld rebuilds them): the
// partitions' are rock towers shoot over, every other wall's cliff, each leaning away from the open
// ground beside it (wallVertices) so no court, home or road loses a tile to it. Water vertices are
// left out: the cliff band meets the water with no beach between.
struct WallMasks
{
	std::vector<unsigned char> ridge, cliff;
};
WallMasks wallMasks(const Layout &L)
{
	const Torus &t = L.t;
	const int n = t.size();
	std::vector<unsigned char> ridge(n, 0), cliff(n, 0), open(n, 0), door(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const bool water = L.radius[i] < L.g.lake || L.radius[i] > L.g.rim;
		door[i] = L.frontOf[i] >= 0;
		ridge[i] = L.partition[i] && !water;
		cliff[i] = L.wall[i] && !L.partition[i] && !water;
		open[i] = !L.wall[i] && !water;
	}
	WallMasks m{wallVertices(t, ridge, &open, &door), wallVertices(t, cliff, &open, &door)};
	for (int i = 0; i < n; ++i)
	{
		if (L.radius[i] < L.g.lake || L.radius[i] > L.g.rim)
			m.ridge[i] = m.cliff[i] = 0;
		// Where a partition meets the ring, the vertex is the partition's: towers shoot over it.
		if (m.ridge[i])
			m.cliff[i] = 0;
	}
	return m;
}

// The building room the validator counts: footprint anchors (buildAnchors) on buildable ground that
// crops can never reach. Structural stone is eternal: unlike the conservative generic growth envelope,
// it is a real barrier here. Ignore buildings and fertility, which can change in play.
struct PermanentRoom
{
	Flood growth;
	std::vector<unsigned char> sites;
};
PermanentRoom permanentRoom(const GenerationMap &map, const Torus &t)
{
	std::vector<unsigned char> cropSeeds(t.size(), 0), cropGround(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const int type = map.getResource(t.remainderX(i), i / t.w).type;
		cropSeeds[i] = type == WHEAT || type == WOOD;
		cropGround[i] = (map.canResourcesGrow(t.remainderX(i), i / t.w) &&
			(map.terrainSupportsResourceAtByIndex(t.remainderX(i), i / t.w, WHEAT) ||
			map.terrainSupportsResourceAtByIndex(t.remainderX(i), i / t.w, WOOD))) && type != STONE;
	}
	PermanentRoom room{floodFrom(t, cropSeeds, cropGround), {}};
	auto permanentBuilding = buildableTiles(map);
	for (int i = 0; i < t.size(); ++i)
		permanentBuilding[i] = permanentBuilding[i] && room.growth.steps[i] < 0;
	room.sites = buildAnchors(t, permanentBuilding);
	return room;
}

bool generate(GenerationWorld &game, GenerationContext &c)
{
	c.stage = "gauntlet layout";
	const GauntletOptions o(c.request);
	const Layout L = design(c.request, c);
	if (!L.failure.empty())
	{
		c.detail = L.failure;
		return false;
	}
	const auto &t = L.t;
	const auto &g = L.g;
	const int n = t.size();
	GenerationMap &map = game.map;
	for (int k = 0; k < g.teams; ++k)
		game.addTeam();
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
		if (L.radius[i] < g.lake || L.radius[i] > g.rim)
			// The central lake is a lava lake in the volcanic climate.
			terrain[i] = L.radius[i] < g.lake && L.climate == kVolcanicClimate ? LAVA : WATER;
	const auto fromWall = stepsFrom(t, L.wall);
	std::vector<Farm> farms;
	std::vector<int> cropHome(n, -1), cropCourt(n, -1);
	const auto markInterior = [&](const std::vector<int> &depth, std::vector<int> &label, int k)
	{
		for (int i = 0; i < n; ++i)
			if (depth[i] >= 3)
				label[i] = k;
	};
	std::vector<int> depth;
	// Rows follow each home frontage; the nearby kit is placed before ambient crops.
	for (int k = 0; k < g.teams; ++k)
	{
		std::vector<unsigned char> region(n, 0);
		for (int i = 0; i < n; ++i)
			region[i] = L.homeOf[i] == k && L.farmRegion[i] && fromWall[i] >= 5;
		const double rows = L.phase + g.wedge * k + kPi / 2;
		farms.push_back(layFarm(terrain, t, region, rows, point(L, g.home + 12, g.wedge * k), 2,
								{16, 8}, nullptr, {24, true, true}, true, &depth));
		markInterior(depth, cropHome, k);
	}
	// Court gardens sit on the lake side, away from the promenade and the partitions.
	std::vector<Farm> gardens;
	std::vector<std::vector<int>> orchards;
	for (int k = 0; k < g.teams; ++k)
	{
		std::vector<unsigned char> region(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const double u = (L.angle[i] / g.wedge - k);
			const double span = std::min(u, 1 - u) * L.radius[i] * g.wedge;
			const bool middleGap = L.kind == 2 && std::abs(u - .5) * L.radius[i] * g.wedge < 4;
			region[i] = L.courtOf[i] == k && span > 7 && !middleGap && L.radius[i] > g.inner + 1 &&
						L.radius[i] < L.gateRadius - 6 && fromWall[i] >= 5 && !L.road[i];
		}
		gardens.push_back(layFarm(terrain, t, region, L.phase + g.wedge * (k + .5),
								  point(L, g.inner + 3, g.wedge * (k + .5)), 2, {12, 6}, nullptr,
								  {20, true, true}, true, &depth));
		markInterior(depth, cropCourt, k);
		const auto p = point(L, g.outer - 8, g.wedge * (k + .5));
		std::vector<int> tiles;
		const int extent = std::max(4, int(g.half / 24));
		for (int dy = -extent; dy <= extent; ++dy)
			for (int dx = -extent; dx <= extent; ++dx)
			{
				const int i = t.at(int(std::lround(p.x)) + dx, int(std::lround(p.y)) + dy);
				if (L.courtOf[i] == k && !L.wall[i] && !L.road[i] && L.radius[i] < g.outer - 4)
					tiles.push_back(i);
			}
		orchards.push_back(tiles);
	}
	// The farms' and gardens' crossings are road and their caps barren ground (dressFarmSand); the
	// promenade and approaches paved, rough ground in the doors so nothing can be built in one; then
	// the walls (wallMasks), meeting the water with no beach.
	for (const Farm &farm : farms)
		dressFarmSand(terrain, farm);
	for (const Farm &garden : gardens)
		dressFarmSand(terrain, garden);
	{
		std::vector<unsigned char> road(n, 0), door(n, 0), paved(n, 0);
		for (int i = 0; i < n; ++i)
		{
			road[i] = L.road[i] && !L.wall[i];
			door[i] = L.frontOf[i] >= 0;
		}
		paveTiles(terrain, t, road, door, paved, MUD);
	}
	const WallMasks walls = wallMasks(L);
	for (int i = 0; i < n; ++i)
		if (terrain[i] != WATER && (walls.ridge[i] || walls.cliff[i]))
			terrain[i] = walls.ridge[i] ? RIDGE_ROCK : CLIFFS;
	// Every partition at least three vertices thick, so its rock draws peaks; never into a road, a
	// door or a farm.
	{
		std::vector<unsigned char> keep(n, 0);
		for (int i = 0; i < n; ++i)
			keep[i] = L.road[i] || L.frontOf[i] >= 0;
		thickenRidges(terrain, t,
					  [&](int i)
					  {
						  return terrain[i] == GRASS && !L.farmRegion[i] && cropCourt[i] < 0 && cropHome[i] < 0 &&
								 !cornerOf(t, keep, i);
					  });
	}
	{
		const std::vector<unsigned char> deep = deepSea(
			terrain, t, [&](int i) { return L.radius[i] < g.lake || L.radius[i] > g.rim; }, kDeepFrom);
		LookRoles roles;
		roles.deep = [&](int i) { return deep[i] != 0; };
		roles.tended = [&](int i) { return L.homeOf[i] >= 0; };
		roles.farm = [&](int i) { return L.farmRegion[i] || cropCourt[i] >= 0 || cropHome[i] >= 0; };
		roles.path = [&](int) { return PathLook::Road; };
		// Every home is barren ground outside its farm, with a paved square round its swarm and a grass
		// verge at the foot of its walls where its quarry stands.
		barrenBase(
			terrain, t,
			[&](int i) { return L.homeOf[i] >= 0 && !L.farmRegion[i] && !L.wall[i] && !L.road[i]; },
			nullptr, kVerge);
		for (int k = 0; k < g.teams; ++k)
			paveCore(terrain, t, t.remainderX(L.anchor[k]), L.anchor[k] / t.w, kCore,
					 [&](int i) { return L.homeOf[i] == k && !L.farmRegion[i]; });
		paintClimate(terrain, t, kClimateLooks[L.climate], roles);
	}
	writeVertices(map, terrain);
	c.stage = "gauntlet colonies";
	const auto home = [&](int k)
	{
		std::vector<unsigned char> mask(n, 0);
		for (int i = 0; i < n; ++i)
			mask[i] = L.homeOf[i] == k && !L.wall[i] && !L.farmRegion[i] && !L.road[i];
		return mask;
	};
	if (!settleColonies(
			game, c, "gauntlet-starts", [&](int team) { return home(L.teamSlot[team]); },
			[&](int team)
			{
				const int k = L.teamSlot[team];
				return MapGeneratorPoint(t.remainderX(L.anchor[k]) - 2, L.anchor[k] / t.w - 2);
			}))
		return false;
	c.stage = "gauntlet towers";
	std::vector<std::vector<int>> towerTiles(g.teams);
	for (int k = 0; k < g.teams; ++k)
		for (int side = 0; side < 2; ++side)
		{
			if (o.towers == 0)
				continue;
			const double a = g.wedge * k + (side == 0 ? -1 : 1) * g.frontAngle;
			// Set guards back from the wall while retaining mouth coverage. This also keeps
			// the initial revealed-area centre on the home lawn for construction planners.
			const auto p =
				point(L, g.outer + 8 + 2 * (o.towers - 1), a + (side == 0 ? -1 : 1) * (o.gateWidth * .5 + 2) / g.outer);
			auto allowed = home(k);
			const auto door =
				point(L, g.outer + 3,
					  a + (side == 0 ? -1 : 1) * std::max(0., (o.gateWidth - 5) / 2.) / g.outer);
			const int team =
				int(std::find(L.teamSlot.begin(), L.teamSlot.end(), k) - L.teamSlot.begin());
			const int tower = placeTower(game, team, o.towers - 1, p.x, p.y, 6, allowed, true,
										 {{int(std::lround(door.x)), int(std::lround(door.y))}}, true);
			towerTiles[k].push_back(tower);
			if (tower < 0)
			{
				c.detail = "A guarded entrance has no supplied tower site: " + std::to_string(k) +
						   ":" + std::to_string(side);
				return false;
			}
		}
	c.stage = "gauntlet resources";
	const auto reserved = swarmSurroundings(t, c);
	// Every home's quarry face, at the inside foot of its outer wall between its two doors.
	{
		std::vector<unsigned char> doors(n, 0);
		for (int i = 0; i < n; ++i)
			doors[i] = L.frontOf[i] >= 0;
		const auto fromDoor = stepsFrom(t, doors);
		int faceTiles = 0;
		for (int k = 0; k < g.teams; ++k)
		{
			const auto at = point(L, g.outer + 3, g.wedge * k);
			faceTiles += plantQuarryFaces(
				map, t, {KitSeed{int(std::lround(at.x)), int(std::lround(at.y)), 10}}, kQuarryFace,
				[&](int i)
				{
					return L.homeOf[i] == k && !L.road[i] && !L.farmRegion[i] && !reserved[i] &&
						   fromDoor[i] > 6 && clearGround(map, t.remainderX(i), i / t.w);
				});
		}
		c.telemetry.measure("gauntlet.quarry-faces.tiles", faceTiles);
	}
	const auto fertility = Fertility::forMap(map, false);
	for (int k = 0; k < g.teams; ++k)
	{
		const int ax = t.remainderX(L.anchor[k]), ay = L.anchor[k] / t.w;
		const auto free = [&](int i)
		{
			return cropHome[i] == k && !L.wall[i] && !reserved[i] && farms[k].row[i] >= 0 &&
				   farms[k].row[i] % 2 == 0 && !farms[k].plot[i] &&
				   clearGround(map, t.remainderX(i), i / t.w);
		};
		const KitFrame frame{ax, ay, L.phase + g.wedge * k};
		const int wood = 30 + int(scaledCount(20, o.wood));
		const auto grain = frame.at(12, 0, 22);
		const auto wheatResult = growPatchesNear(map, t, grain.x, grain.y, 22, WHEAT, 90, free);
		int woodPlaced = 0;
		for (int side : {-1, 1})
		{
			const auto timber = frame.at(7, side * 12, 22);
			woodPlaced += growPatchesNear(map, t, timber.x, timber.y, 22, WOOD,
										  wood / 2 + (side > 0 ? wood % 2 : 0), free)
							  .tiles;
		}
		if (woodPlaced < 30 || wheatResult.tiles < 70)
		{
			c.detail =
				"A home farm cannot hold its nearby opening crops: slot=" + std::to_string(k) +
				" wheat=" + std::to_string(wheatResult.tiles) +
				" wood=" + std::to_string(woodPlaced);
			return false;
		}
		int area = 0;
		for (int i = 0; i < n; ++i)
			area += L.homeOf[i] == k && L.farmRegion[i];
		const int wheat = int(scaledCount(area / 9, o.wheat));
		const int planted = plantFarm(map, t, farms[k], wheat, 0, free);
		c.telemetry.measure("gauntlet.home.farm-area", area, k);
		c.telemetry.measure("gauntlet.home.crop-request", wheat + wood + 90, k);
		c.telemetry.measure("gauntlet.home.wheat", planted + wheatResult.tiles, k);
		c.telemetry.measure("gauntlet.home.wood", woodPlaced, k);
		c.telemetry.measure("gauntlet.home.crop-placed", planted + woodPlaced + wheatResult.tiles,
							k);
		c.telemetry.measure("gauntlet.home.towers", o.towers == 0 ? 0 : 2, k);
		c.telemetry.measure("gauntlet.home.tower-level", o.towers, k);
	}
	for (int k = 0; k < g.teams; ++k)
	{
		const int planted = plantFarm(
			map, t, gardens[k], int(scaledCount(g.half * .6, o.wheat)), 0, [&](int i)
			{ return cropCourt[i] == k && !L.wall[i] && clearGround(map, t.remainderX(i), i / t.w); });
		c.telemetry.measure("gauntlet.court.wheat", planted, k);
		for (int fruit = 0; fruit < 3; ++fruit)
			plantContainedPlot(map, t, orchards[k], fertility, CHERRY + fruit,
							   int(scaledCount(std::max(4, int(g.half / 24)), o.fruit)), false);
	}
	seedAlgae(map, c, t, "gauntlet-algae", o.algae, AlgaeBand::shallows(1, 4));
	secureStartingCrops(game, c, t, 24, 32, 0, &L.wall);
	// Last, each door's tower gets a face at the foot of the outer wall beside it: the towers stand
	// back from the wall by their doors, a long walk from the face between the doors. Stone takes
	// building room, so the face never touches the footprints of the kKeptRoom anchors of permanent
	// room nearest each home's workers, counted exactly as validateWorld counts them (after every
	// crop is down): a 16-colony map's small homes kept 15 of the 16 it needs when the faces went in
	// unguarded (#1583).
	{
		std::vector<unsigned char> doors(n, 0);
		for (int i = 0; i < n; ++i)
			doors[i] = L.frontOf[i] >= 0;
		const auto fromDoor = stepsFrom(t, doors);
		const PermanentRoom room = permanentRoom(map, t);
		const auto walk = walkFromFirstColony(map, g.teams, "the gauntlet", "");
		const auto open = walkableTiles(map);
		std::vector<unsigned char> kept(n, 0);
		for (int k = 0; k < g.teams && walk.error.empty(); ++k)
		{
			const auto distance = stepsFrom(t, tileMask(t, walk.workers[k]), open);
			std::vector<std::pair<int, int>> anchors;
			for (int i = 0; i < n; ++i)
				if (distance[i] >= 0 && distance[i] <= kRoomReach && room.sites[i] && room.growth.steps[i] < 0)
					anchors.push_back({distance[i], i});
			std::sort(anchors.begin(), anchors.end());
			for (size_t a = 0; a < anchors.size() && a < size_t(kKeptRoom); ++a)
				for (int dy = 0; dy < kBuildFootprint; ++dy)
					for (int dx = 0; dx < kBuildFootprint; ++dx)
						kept[t.at(t.remainderX(anchors[a].second) + dx, anchors[a].second / t.w + dy)] = 1;
		}
		int towerStone = 0;
		for (int k = 0; k < g.teams; ++k)
			towerStone += plantTowerStone(
				map, t, towerTiles[k], kTowerStone, kTowerStoneReach,
				[&](int i)
				{
					return L.homeOf[i] == k && !L.road[i] && !L.farmRegion[i] && !reserved[i] && !kept[i] &&
						   fromDoor[i] > kTowerStoneDoorGap && clearGround(map, t.remainderX(i), i / t.w);
				});
		c.telemetry.measure("gauntlet.tower-stone.tiles", towerStone);
	}
	return true;
}
std::string validateRequest(const GenerationRequest &r)
{
	return geometryFor(r).failure;
}
std::string validateWorld(const GenerationWorld &game, const GenerationContext &c)
{
	GenerationContext replay(c.request);
	const auto L = design(c.request, replay);
	if (const auto error = designMismatch(L, game.map, "gauntlet"); !error.empty())
		return error;
	const auto &t = L.t;
	const auto &map = game.map;
	const auto open = walkableTiles(map);
	// A wall is terrain no unit walks or swims, or a permanent deposit.
	const auto barrier = [&](int i)
	{
		const TerrainProperties &p = map.terrainPropertiesAt(t.remainderX(i), i / t.w);
		return (!p.walkable && !p.swimmable) || permanentResourceBarrier(map, i);
	};
	{
		const WallMasks walls = wallMasks(L);
		const int ridge = wallTerrainMismatch(map, t, walls.ridge, TerrainGroup::Ridges);
		const int cliff = wallTerrainMismatch(map, t, walls.cliff, TerrainGroup::Obstacles);
		if (ridge >= 0 || cliff >= 0)
		{
			const int at = ridge >= 0 ? ridge : cliff;
			return "A Gauntlet wall is missing at " + std::to_string(t.remainderX(at)) + ":" +
				   std::to_string(at / t.w);
		}
	}
	const PermanentRoom room = permanentRoom(map, t);
	const auto &growth = room.growth;
	const auto &sites = room.sites;
	for (int i = 0; i < t.size(); ++i)
	{
		if (L.frontOf[i] >= 0 && (!open[i] || growth.steps[i] >= 0))
			return "A Gauntlet entrance is blocked at " + std::to_string(t.remainderX(i)) + ":" +
				   std::to_string(i / t.w) + (open[i] ? " (crops can grow into it)." : ".");
	}
	const auto walk = walkFromFirstColony(map, L.g.teams, "the gauntlet", "");
	if (!walk.error.empty())
		return walk.error;
	std::vector<int> pieces(t.size(), -1);
	std::vector<unsigned char> shut(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		pieces[i] = L.homeOf[i] >= 0 ? L.homeOf[i] : L.courtOf[i] >= 0 ? L.g.teams : -1;
		shut[i] = L.frontOf[i] >= 0;
	}
	if (pieceLeak(map, t, pieces, shut) >= 0)
		return "A home has a route that bypasses its two doors.";
	std::vector<unsigned char> courts(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		courts[i] = open[i] && L.courtOf[i] >= 0 && growth.steps[i] < 0;
	int seed = -1;
	for (int i = 0; i < t.size(); ++i)
		if (courts[i])
		{
			seed = i;
			break;
		}
	if (seed < 0)
		return "The fighting circuit is missing.";
	// Reachability alone accepts a broken ring. Close every inter-court gate, then
	// reopen each one separately and require its own two neighbouring courts to join.
	std::vector<int> gateOf(t.size(), -1);
	const GauntletOptions o(c.request);
	for (int i = 0; i < t.size(); ++i)
		if (courts[i])
		{
			const int slot = int(std::floor(L.angle[i] / L.g.wedge + .5)) % L.g.teams;
			double a = L.angle[i] - slot * L.g.wedge;
			if (a > kPi)
				a -= kTau;
			if (std::abs(L.radius[i] * std::sin(a)) < 7 + o.partitionWall &&
				std::abs(L.radius[i] - L.gateRadius) < o.gateWidth * .5 + 2)
				gateOf[i] = slot;
		}
	std::vector<TileGate> gates;
	for (int k = 0; k < L.g.teams; ++k)
	{
		TileGate gate{{L.g.teams + (k + L.g.teams - 1) % L.g.teams, L.g.teams + k}, {}};
		for (int i = 0; i < t.size(); ++i)
			if (gateOf[i] == k)
				gate.tiles.push_back(i);
		gates.push_back(gate);
		for (int side = 0; side < 2; ++side)
		{
			TileGate door{{k, L.g.teams + (k + (side == 0 ? L.g.teams - 1 : 0)) % L.g.teams}, {}};
			for (int i = 0; i < t.size(); ++i)
				if (L.frontOf[i] == 2 * k + side)
					door.tiles.push_back(i);
			gates.push_back(door);
		}
	}
	if (gates.empty() || gates[0].tiles.empty())
		return "The court circuit has no entrance.";
	const auto circuitReach = stepsFrom(t, tileMask(t, {gates[0].tiles[0]}), courts);
	for (int k = 0; k < L.g.teams; ++k)
		if (gates[3 * k].tiles.empty() || circuitReach[gates[3 * k].tiles[0]] < 0)
			return "The permanent fighting circuit is disconnected.";
	std::vector<int> labels(t.size(), -1);
	std::vector<unsigned char> permanent(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		// Include water: swimmers must not bypass the sealed lake or outside coast.
		permanent[i] = !barrier(i) && growth.steps[i] < 0;
		if (L.homeOf[i] >= 0 && L.radius[i] < L.g.rim - 9)
			labels[i] = L.homeOf[i];
		else if (L.courtOf[i] >= 0)
		{
			const double u = L.angle[i] / L.g.wedge - L.courtOf[i];
			// Label court interiors only: bent walls can cross the nominal angular border.
			if (u > .2 && u < .8)
				labels[i] = L.g.teams + L.courtOf[i];
		}
	}
	auto possible = permanent;
	for (int i = 0; i < t.size(); ++i)
		possible[i] = !barrier(i);
	const auto possibleGraph = checkGatePartition(t, possible, labels, gates);
	if (possibleGraph.leakTile >= 0)
		return "A removable obstruction hides an unintended arena route.";
	const auto graph = checkGatePartition(t, permanent, labels, gates);
	if (graph.leakTile >= 0)
		return "The arena has an unintended route between regions at " +
			   std::to_string(t.remainderX(graph.leakTile)) + ":" + std::to_string(graph.leakTile / t.w);
	if (graph.badGate >= 0)
		return "An arena gate fails its two-region connection: " + std::to_string(graph.badGate);

	// Inspect the finished economy and keep building room beside every contested court.
	std::vector<int> courtRoom(L.g.teams, 0), grainCount(L.g.teams, 0);
	std::vector<std::array<int, 3>> fruitCount(L.g.teams);
	for (int i = 0; i < t.size(); ++i)
		if (L.courtOf[i] >= 0)
		{
			const int k = L.courtOf[i], type = map.getResource(t.remainderX(i), i / t.w).type;
			courtRoom[k] += sites[i] && growth.steps[i] < 0;
			if (type >= CHERRY && type <= PRUNE)
				++fruitCount[k][type - CHERRY];
			grainCount[k] += type == WHEAT;
		}
	for (int k = 0; k < L.g.teams; ++k)
	{
		if (courtRoom[k] < 16)
			return "A fighting court lacks permanent building room: " + std::to_string(k) + ":" +
				   std::to_string(courtRoom[k]);
		if (GauntletOptions(c.request).fruit > 0 &&
			*std::min_element(fruitCount[k].begin(), fruitCount[k].end()) < 1)
			return "A court is missing its orchard.";
		if (GauntletOptions(c.request).wheat > 0 && grainCount[k] < 4)
			return "A court is missing its bonus farm.";
		const auto distance = stepsFrom(t, tileMask(t, walk.workers[k]), open);
		int wheat = 100000, wood = 100000, room = 0;
		for (int i = 0; i < t.size(); ++i)
		{
			if (distance[i] >= 0 && distance[i] <= kRoomReach && sites[i] && growth.steps[i] < 0)
				++room;
			const int type = map.getResource(t.remainderX(i), i / t.w).type;
			if (type != WHEAT && type != WOOD)
				continue;
			int best = 100000;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int d = distance[t.at(t.remainderX(i) + dx, i / t.w + dy)];
					if (d >= 0)
						best = std::min(best, d + 1);
				}
			if (type == WHEAT)
				wheat = std::min(wheat, best);
			else
				wood = std::min(wood, best);
		}
		if (wheat > 20 || wood > 24 || room < 16)
			return "A home lacks nearby crops or building room: " + std::to_string(k) +
				   " wheat=" + std::to_string(wheat) + " wood=" + std::to_string(wood) +
				   " room=" + std::to_string(room);
		const int slot = L.teamSlot[k];
		for (int side = 0; side < 2; ++side)
		{
			bool reached = false;
			for (int i = 0; i < t.size(); ++i)
				if (L.frontOf[i] == 2 * slot + side && distance[i] >= 0)
					reached = true;
			if (!reached)
				return "A colony cannot reach both its entrances.";
		}
	}
	for (int k = 0; k < L.g.teams; ++k)
		if (countBuildings(game, k, "defencetower") !=
			(GauntletOptions(c.request).towers == 0 ? 0 : 2))
			return "A colony is missing an entrance tower.";
	return "";
}
} // namespace
GauntletOptions::GauntletOptions(const GenerationRequest &r)
	: courtSize(r.option("court-size")), gateWidth(r.option("gate-width")),
	  partitionWall(r.option("partition-wall")), towers(r.option("starting-towers")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition gauntletDefinition()
{
	return {"gauntlet",
			59,
			"The Gauntlet",
			7,
			false,
			{GeneratorControl{"court-size", "Court size", 80, 120, 10, 100, ControlGroup::Layout}
				 .withSearchRange(90, 120),
			 GeneratorControl{"gate-width", "Gate width", 5, 9, 2, 7, ControlGroup::Terrain}
				 .withSearchRange(7, 9),
			 GeneratorControl{"partition-wall", "Partition thickness", 1, 3, 1, 2,
							  ControlGroup::Terrain}
				 .withSearchRange(1, 3),
			 GeneratorControl{"starting-towers", "Starting tower level", 0, 3, 1, 1,
							  ControlGroup::Layout}
				 .withSearchRange(0, 1),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validateRequest,
			validateWorld,
			{"terrain:arena", "feature:stone-walls", "feature:orchard", "style:siege",
			 "fairness:repeated-wedge"}};
}
