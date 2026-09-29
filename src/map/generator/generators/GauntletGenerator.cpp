// SPDX-License-Identifier: GPL-3.0-or-later
#include "GauntletGenerator.h"
#include "Game.h"
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
// Home farms are capped irrigated rows; sandy circulation and garden caps contain growth.
// The repeated angular design is measured, not claimed to be exact integer-tile symmetry.
namespace
{
constexpr double kTau = 2 * kPi;
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
	g.inner = g.lake + 7;
	g.outer = g.inner + std::max(30., g.half * .27 * o.courtSize / 100.);
	g.rim = g.half - 4;
	g.home = g.outer + 18;
	// Keep both fronts within a compact defended frontage, including sparse large arenas.
	g.frontAngle = std::min(g.wedge * .25, 22. / g.outer);
	if (g.half < 128 || g.teams < 2 || g.teams > 12 || g.outer * g.wedge < 48 ||
		g.inner * g.wedge < 27 || g.rim - g.home < 22)
		g.failure =
			"The Gauntlet needs a larger map or fewer colonies to fit its two guarded fronts.";
	return g;
}
struct Layout
{
	Torus t{1, 1};
	Geometry g;
	double phase = 0, gateRadius = 0;
	int kind = 0;
	std::vector<double> radius, angle;
	std::vector<int> homeOf, courtOf, anchor, frontOf, teamSlot;
	std::vector<unsigned char> wall, road, farmRegion;
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
	for (int k = 0; k < g.teams; ++k)
	{
		const auto a = point(L, g.home, g.wedge * k);
		L.anchor.push_back(t.at(int(std::lround(a.x)), int(std::lround(a.y))));
	}
	for (int i = 0; i < n; ++i)
	{
		const double x = t.offsetX(t.w / 2, i % t.w), y = t.offsetY(t.h / 2, i / t.w);
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
		// The coastal walls stand entirely inland of their beaches.
		L.wall[i] = (std::abs(rad - (g.lake + 4.5)) < 1.5 || std::abs(rad - (g.rim - 4.5)) < 1.5);
		if (std::abs(rad - g.outer) <= 1.2)
			L.wall[i] = 1;
		// Each separator follows one of three court families: straight, bent or paired gardens.
		const double bend =
			L.kind == 1 ? 3 * std::sin((rad - g.inner) / (g.outer - g.inner) * kTau) : 0;
		const double separator = rad * std::sin(local) - bend;
		if (rad >= g.inner - 3 && rad <= g.outer + 2 &&
			std::abs(separator) < o.partitionWall * .5 + .4 &&
			std::abs(rad - L.gateRadius) > o.gateWidth * .5)
			L.wall[i] = 1;
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
					L.frontOf[i] = 2 * home + (side > 0);
				}
			}
		}
		if (L.homeOf[i] >= 0 && rad > g.outer + 13 && rad < g.rim - 9 &&
			t.dist2(i % t.w, i / t.w, L.anchor[home] % t.w, L.anchor[home] / t.w) > 9 * 9 &&
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

bool generate(Game &game, GenerationContext &c)
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
	Map &map = game.map;
	for (int k = 0; k < g.teams; ++k)
		game.addTeam();
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
		if (L.radius[i] < g.lake || L.radius[i] > g.rim)
			terrain[i] = WATER;
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
	// Farms and the promenade never paint a wall's four grass corners.
	for (int i = 0; i < n; ++i)
		if (L.road[i] && !L.wall[i])
			terrain[i] = SAND;
	for (int i = 0; i < n; ++i)
		if (L.wall[i])
			for (int dy = -1; dy <= 2; ++dy)
				for (int dx = -1; dx <= 2; ++dx)
					terrain[t.at(i % t.w + dx, i / t.w + dy)] = GRASS;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	for (int i = 0; i < n; ++i)
		if (L.wall[i] && map.isGrass(i % t.w, i / t.w))
			map.setResource(i % t.w, i / t.w, STONE, 1);
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
				return MapGeneratorPoint(L.anchor[k] % t.w - 2, L.anchor[k] / t.w - 2);
			}))
		return false;
	c.stage = "gauntlet towers";
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
			if (placeTower(game, team, o.towers - 1, p.x, p.y, 6, allowed, true,
						   {{int(std::lround(door.x)), int(std::lround(door.y))}}, true) < 0)
			{
				c.detail = "A guarded entrance has no supplied tower site: " + std::to_string(k) +
						   ":" + std::to_string(side);
				return false;
			}
		}
	c.stage = "gauntlet resources";
	const auto reserved = swarmSurroundings(t, c);
	const auto fertility = Fertility::forMap(map, false);
	for (int k = 0; k < g.teams; ++k)
	{
		const int ax = L.anchor[k] % t.w, ay = L.anchor[k] / t.w;
		const auto free = [&](int i)
		{
			return cropHome[i] == k && !L.wall[i] && !reserved[i] && farms[k].row[i] >= 0 &&
				   farms[k].row[i] % 2 == 0 && !farms[k].plot[i] &&
				   clearGround(map, i % t.w, i / t.w);
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
			{ return cropCourt[i] == k && !L.wall[i] && clearGround(map, i % t.w, i / t.w); });
		c.telemetry.measure("gauntlet.court.wheat", planted, k);
		for (int fruit = 0; fruit < 3; ++fruit)
			plantContainedPlot(map, t, orchards[k], fertility, CHERRY + fruit,
							   int(scaledCount(std::max(4, int(g.half / 24)), o.fruit)), false);
	}
	seedAlgae(map, c, t, "gauntlet-algae", o.algae, AlgaeBand::shallows(1, 4));
	secureStartingCrops(game, c, t, 24, 32, 0, &L.wall);
	return true;
}
std::string validateRequest(const GenerationRequest &r)
{
	return geometryFor(r).failure;
}
std::string validateWorld(const Game &game, const GenerationContext &c)
{
	GenerationContext replay(c.request);
	const auto L = design(c.request, replay);
	if (const auto error = designMismatch(L, game.map, "gauntlet"); !error.empty())
		return error;
	const auto &t = L.t;
	const auto &map = game.map;
	const auto open = walkableTiles(map);
	// Structural stone is eternal: unlike the conservative generic growth envelope,
	// it is a real barrier here. Ignore buildings and fertility, which can change in play.
	std::vector<unsigned char> cropSeeds(t.size(), 0), cropGround(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const int type = map.getResource(i % t.w, i / t.w).type;
		cropSeeds[i] = type == WHEAT || type == WOOD;
		cropGround[i] = map.isGrass(i % t.w, i / t.w) && type != STONE;
	}
	const auto growth = floodFrom(t, cropSeeds, cropGround);
	auto permanentBuilding = buildableTiles(map);
	for (int i = 0; i < t.size(); ++i)
		permanentBuilding[i] = permanentBuilding[i] && growth.steps[i] < 0;
	const auto sites = buildAnchors(t, permanentBuilding);
	for (int i = 0; i < t.size(); ++i)
	{
		if (L.wall[i] && map.getResource(i % t.w, i / t.w).type != STONE)
			return "A Gauntlet wall is missing at " + std::to_string(i % t.w) + ":" +
				   std::to_string(i / t.w);
		if (L.frontOf[i] >= 0 && (!open[i] || growth.steps[i] >= 0))
			return "A Gauntlet entrance is blocked.";
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
		permanent[i] = map.getResource(i % t.w, i / t.w).type != STONE && growth.steps[i] < 0;
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
		possible[i] = map.getResource(i % t.w, i / t.w).type != STONE;
	const auto possibleGraph = checkGatePartition(t, possible, labels, gates);
	if (possibleGraph.leakTile >= 0)
		return "A removable obstruction hides an unintended arena route.";
	const auto graph = checkGatePartition(t, permanent, labels, gates);
	if (graph.leakTile >= 0)
		return "The arena has an unintended route between regions at " +
			   std::to_string(graph.leakTile % t.w) + ":" + std::to_string(graph.leakTile / t.w);
	if (graph.badGate >= 0)
		return "An arena gate fails its two-region connection: " + std::to_string(graph.badGate);

	// Inspect the finished economy and keep building room beside every contested court.
	std::vector<int> courtRoom(L.g.teams, 0), grainCount(L.g.teams, 0);
	std::vector<std::array<int, 3>> fruitCount(L.g.teams);
	for (int i = 0; i < t.size(); ++i)
		if (L.courtOf[i] >= 0)
		{
			const int k = L.courtOf[i], type = map.getResource(i % t.w, i / t.w).type;
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
			if (distance[i] >= 0 && distance[i] <= 24 && sites[i] && growth.steps[i] < 0)
				++room;
			const int type = map.getResource(i % t.w, i / t.w).type;
			if (type != WHEAT && type != WOOD)
				continue;
			int best = 100000;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int d = distance[t.at(i % t.w + dx, i / t.w + dy)];
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
			1,
			false,
			{{"court-size", "Court size", 80, 120, 10, 100, ControlGroup::Layout},
			 {"gate-width", "Gate width", 5, 9, 2, 7, ControlGroup::Terrain},
			 {"partition-wall", "Partition thickness", 1, 3, 1, 2, ControlGroup::Terrain},
			 {"starting-towers", "Starting tower level", 0, 3, 1, 1, ControlGroup::Layout},
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
