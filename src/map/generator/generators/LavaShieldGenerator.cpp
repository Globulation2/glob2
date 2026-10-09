// SPDX-License-Identifier: GPL-3.0-or-later
#include "LavaShieldGenerator.h"
#include "Drawing.h"
#include "FertilityField.h"
#include "Farmland.h"
#include "ScoredSettlements.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Resources.h"
#include "Roads.h"
#include "Sketch.h"
#include "StartQuality.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
using namespace MapGeneration;

// Lava shield is a landscape, not an orbit arena. Each downhill tongue has its own
// length, persistent turns and optional side branches; homes are selected afterwards.
// The crater and its shared rim are reserved before the flows are drawn. See
// LAVA_SHIELD.md for the play contract, tuning history and actual evidence.
//
// The primary tongues are live lava: terrain of the lava group, which no ground unit
// crosses and fliers avoid because it burns them. A long tongue runs into the sea with
// no beach, so it seals its two wedges from each other except at one cooled ford of
// scree partway down; a short tongue stops inland and leaves a coastal gap. Side
// branches are older, cooled flows of scree and gravel that slow a walk but do not
// stop it. Building stone is a crust on the grass along the flows.
namespace
{
// Geometry is in tiles of the shorter side, stretched to at most 2:1. An island
// radius of .43 sides plus at most 6% outline wobble leaves ocean across every seam.
constexpr double kIslandShare = 0.43, kCoastRoughness = 0.06;
// A visible inland lake even at 128; six percent leaves room for a town and lava on
// the smallest slope. Beaches consume its outer grass, so reserve the rim separately.
constexpr double kLakeShare = 0.06;
constexpr int kMinimumLake = 8;
// Four-corner terrain conversion spoils a tile beside sand. A 1.5-corner road half
// width keeps an eight-connected sand circuit; crops cannot grow across its pure sand.
constexpr double kRoadHalfWidth = 1.5;
// A two-corner sand ring isolates a town from spreading crops without a new inland pond.
constexpr int kHomeRing = 2;
// Search on a four-tile grid: independent of angles and cheap enough to score whole
// finished candidate worlds. Four complete deals are tried, never a wall-clock budget.
constexpr int kSiteStride = 4, kDeals = 4;
// The home centre must be close enough to shore for external starter fields to renew,
// but not so close that its sand ring eats the beach. Engine probes reach 15 per axis.
constexpr int kHomeWaterMin = 12, kHomeWaterMax = 21;
// Islets off the coast, out where the ocean wraps the torus (a maintainer asked for a couple
// to keep the seams interesting): at least two, one more per this much sea, each tried this
// many times, with this much water between an islet and any coast or other islet, so they are
// reached by swimming only (raiseIslands) and each carries a prize (stockIslands). Their grass
// is far too small for the town search's clearance, so they are never a home.
constexpr int kFewestIslets = 2, kSeaPerIslet = 8000, kIsletAttempts = 40;
constexpr double kIsletMoat = 6;
// Final-world floors: overlapping 4x4 origins, not a promise of this many buildings.
// These exceed the shared emergency room target (16), while crop distances use the
// shared opening budgets.
constexpr int kMinimumSites = 48, kWheatRange = 24, kWoodRange = 32;
// Fairness is now the fitted model's (StartQuality.h): 1 when every town is as likely to win
// as any other. Over forty default 256x256 four-colony seeds Lava shield sits between 0.84 and
// 0.97, so this floor rejects towns that are genuinely lopsided without touching what the
// generator produces at its defaults. It was 0.65 on the old worst-over-best ratio, which is a
// different quantity on a different scale; the two numbers are not comparable.
constexpr double kMinimumFairness = 0.80;
// External crop patches bootstrap zero-abundance games. Their fertile placement and
// harvest frontage matter more than filling the whole home with a fixed resource kit.
constexpr int kStarterWheat = 40, kStarterWood = 32;
// A downhill walk advances three tiles radially per step. Persistent angular noise
// makes bends rather than zigzags; clamping to .30 of a local angular interval keeps
// unrelated primary flows apart without equalising their wedge sizes.
constexpr double kDownhillStep = 3.0, kTurnMemory = 0.72, kTurnNoise = 0.055;
constexpr double kAngularDrift = 0.30;
// Side branches stop six tiles from other flows, leaving at least a fighting lane.
// Paths with the same flow owner (parent and siblings) are exempt so a fork can form
// a connected lobe; children never gain permission to cross an unrelated flow.
constexpr double kBranchClearance = 6.0;
// A long tongue's ford cuts the flow across its whole width for this many tiles either side of
// one point 40-65% of the way down: a crossing about five tiles wide, slow scree underfoot.
constexpr double kFordHalfLength = 2.5;
// Flow girth: smooth noise over kGirthCell tiles swings the width by up to a quarter, a primary
// tongue's last three points spread its toe by a third, and no corner stroke is thinner than 1.25
// (a wall at least two pure tiles across).
constexpr double kGirthCell = 18.0, kGirthSwing = 0.25, kToeSpread = 1.35, kThinnestFlow = 1.25;
constexpr int kToePoints = 3;
constexpr double kFordFrom = 0.40, kFordSpan = 0.25;
// Crust stone, the colonies' building stone, stays this far from a ford so it never plugs one.
constexpr int kFordClearance = 4;
// Along a flow, live or cooled, the ground changes every dozen tiles or so (kEdgeCell). Below
// kCrustBelow on that noise, the grass within kCrustReach corners of the flow carries stone; up
// to kBareBelow it is left plain so units can walk right up to the flow; up to kScreeBelow a
// scree apron runs out to kScreeReach corners; above it, ash (dirt, or clay) runs out to
// kAshReach corners. The crust gives about a third of the old full-width stone tongues' tiles
// and keeps every colony's nearest quarry within the old distances. Each reach is for 256-tile
// sides and halves at 128, where the wedges are narrow and their open ground is the expansion.
constexpr double kEdgeCell = 12.0;
constexpr double kCrustBelow = 0.62, kBareBelow = 0.70, kScreeBelow = 0.85;
constexpr int kCrustReach = 3, kScreeReach = 3, kAshReach = 4;
// The upper slopes, from the lava roots a third of the way to the coast, are ash where a 9-tile
// noise exceeds a threshold rising from kAshFrom at the roots to 1 at the band's foot.
constexpr double kAshSlope = 0.35, kAshFrom = 0.45;
// The crater: a lava lake fringed with ember, one to three void vents near its middle, ash
// between the lake and the rim circuit, and volcanic loam (with moss) on the rim beyond the
// circuit, the contested prize.
constexpr int kMostVents = 3;
constexpr double kVentRadius = 1.5;
// Open sea about this many steps from any land is deep water: slow to swim, no algae. Noise moves
// the edge up to kDeepSwing steps either way, so neither the island nor an islet sits in a ruled
// ring or box of shallows. The rim's loam reaches kRimSwing tiles either side of the lava roots.
constexpr int kDeepWater = 10;
constexpr double kDeepSwing = 4, kRimSwing = 4;

struct Layout
{
	Torus t{1, 1};
	Stretch stretch;
	TerrainSketch terrain;
	// hot: tiles with any lava-group corner (never painted over). wall: tiles whose four corners
	// are all lava-group, the impassable flows and crater. fords: the cooled crossings' tiles;
	// fordTiles lists them per long tongue. flowEdge and fordCorners are the corners the
	// margins along the flows are measured from, once the towns are placed (furnishMargins).
	std::vector<unsigned char> rim, lake, hot, wall, fords, flowEdge, fordCorners;
	std::vector<std::vector<int>> fordTiles;
	std::vector<std::vector<StrokePoint>> flows;
	std::vector<int> flowOwner;
	std::vector<Island> islets;
	double cx = 0, cy = 0, lakeRadius = 0, rootRadius = 0, islandRadius = 0;
	std::string failure;
};

std::string requestFailure(const GenerationRequest &r)
{
	const int shorter = std::min(1 << r.wDec, 1 << r.hDec);
	const int longer = std::max(1 << r.wDec, 1 << r.hDec);
	// This envelope budgets a lake, shared rim, lava and ordinary towns. Tiny maps or
	// long ribbons would turn the island into the spoke arena this design avoids.
	if (shorter < 128 || longer > 512 || longer > 2 * shorter)
		return "Lava shield needs 128–512 tile sides and an aspect ratio no greater than 2:1.";
	const int maximumTeams = shorter == 128 ? 2 : (shorter == 256 ? 8 : 12);
	if (r.nbTeams > maximumTeams)
		return "Too many colonies for Lava shield's coastal towns; use a larger map or fewer "
			   "colonies.";
	// Combined extremes need a smaller envelope than individual controls. Held-out
	// 128-square/9-tongue/wide-rim maps often had no two roomy coastal sites; the
	// same layout with eight towns on 256 often had no protected approach. Keep
	// the tested default geometry budget at those densities, rather than shrinking
	// towns or carving through stone. Larger/sparser worlds retain every control.
	const bool compactTowns = shorter == 128 || (shorter == 256 && r.nbTeams > 4);
	if (compactTowns && (r.option("tongue-count") > 5 || r.option("rim-width") > 8))
		return "Lava shield needs at most 5 tongues and rim width 8 on 128-tile sides or "
			   "with more than 4 colonies on 256-tile sides. Use a larger map or fewer colonies.";
	// A 4x4 swarm has exactly twenty adjacent cells; placeSettlement requires every
	// initial worker to touch it. The shared UI currently fits this bound as well.
	if (r.nbWorkers > 20)
		return "Lava shield supports at most 20 starting workers per colony.";
	return {};
}

Layout design(const GenerationRequest &r, GenerationContext &context)
{
	Layout L;
	L.failure = requestFailure(r);
	if (!L.failure.empty())
		return L;
	const LavaShieldOptions o(r);
	L.t = {1 << r.wDec, 1 << r.hDec};
	const Torus &t = L.t;
	L.stretch = Stretch::toFill(t.w, t.h);
	L.cx = t.w / 2.0;
	L.cy = t.h / 2.0;
	const int shorter = std::min(t.w, t.h);
	L.lakeRadius = std::max(double(kMinimumLake), shorter * kLakeShare);
	// The five extra tiles are a shoreline margin plus road/grass transition; rimWidth
	// controls useful space beyond that fixed cost, not the lake's sand beach itself.
	L.rootRadius = L.lakeRadius + o.rimWidth + 5;
	L.islandRadius = shorter * kIslandShare;
	L.terrain.assign(t.size(), WATER);
	L.rim.assign(t.size(), 0);
	L.lake.assign(t.size(), 0);
	const RadialShape coast(shorter * kIslandShare, kCoastRoughness, context, "lava-coast");
	const RadialShape crater(L.lakeRadius, 0.08, context, "lava-crater");
	fillShape(L.terrain, t, L.cx, L.cy, coast, 0, GRASS, L.stretch);
	fillShape(L.lake, t, L.cx, L.cy, crater, 0, 1, L.stretch);
	// The circuit follows the same gently irregular crater outline. It is a reserved
	// path, not a second decorative concentric web thread. Its outer grass is the prize.
	std::vector<StrokePoint> circuit;
	const int samples = int(std::ceil(2 * kPi * (L.lakeRadius + 5)));
	for (int k = 0; k < samples; ++k)
	{
		const double a = 2 * kPi * k / samples;
		const ShapePoint p =
			L.stretch.apply(L.cx, L.cy, polarPoint(L.cx, L.cy, crater.radiusAt(a) + 5, a));
		circuit.push_back({p.x, p.y, kRoadHalfWidth});
	}
	strokePath(L.rim, t, circuit, 1, true);

	auto &rng = context.stream("lava-flows");
	const auto roll = [&]() { return rng() / 4294967296.0; };
	// Random interval weights span 0.65–1.65 before normalization: visibly unequal
	// wedges with a lower bound, rather than evenly spaced spokes with cosmetic jitter.
	std::vector<double> intervals(o.tongues);
	double sum = 0;
	for (double &v : intervals)
		sum += v = 0.65 + roll();
	for (double &v : intervals)
		v *= 2 * kPi / sum;
	std::vector<int> longOrder;
	for (int k = 0; k < o.tongues; ++k)
		longOrder.push_back(k);
	context.shuffle(longOrder.begin(), longOrder.end(), "lava-reach");
	const int longCount = std::clamp(int(scaledCount(o.tongues, o.longTongues)), 1, o.tongues - 1);
	std::vector<unsigned char> reachesSea(o.tongues, 0);
	for (int k = 0; k < longCount; ++k)
		reachesSea[longOrder[k]] = 1;
	double angle = roll() * 2 * kPi;
	int branchesWanted = 0, branchesPlaced = 0, refused = 0;
	for (int k = 0; k < o.tongues; ++k)
	{
		const double span = std::min(intervals[k], intervals[(k + o.tongues - 1) % o.tongues]);
		const double axis = angle;
		angle += intervals[k];
		// Long flows reach beyond the coast before legal-terrain clipping. Short flows
		// stop 12–22 tiles inland (scaled on larger maps) to leave a broad useful bypass.
		const double gap = reachesSea[k] ? 0 : (12 + 10 * roll()) * std::sqrt(shorter / 128.0);
		// Bound the coastline at every possible final heading. Measuring only at
		// axis would let a bend turn a "long" flow into an accidental broad gap.
		const double end = reachesSea[k] ? coast.maximumRadius() + 5
										 : shorter * kIslandShare * (1 - kCoastRoughness) - gap;
		const double width = std::clamp(shorter / 70.0, 2.0, 5.0);
		// Thick roots read as a broken summit. The shared downhill walk keeps radius
		// monotone, correlates turns and includes the exact terminal radius.
		const auto path =
			downhillPath({L.cx, L.cy}, L.rootRadius, end, axis, width * 1.8, width, rng,
						 {kDownhillStep, kTurnMemory, kTurnNoise, span * kAngularDrift}, L.stretch);
		L.flows.push_back(path);
		L.flowOwner.push_back(k);
	}
	// All primary flows exist before any branch competes for room. Otherwise the
	// first tree could occupy a future neighbour's entire wedge (Coral's growth lesson).
	for (int k = 0; k < o.tongues; ++k)
	{
		const auto parent = L.flows[k];
		for (int b = 0; b < o.branching && parent.size() >= 6; ++b)
		{
			++branchesWanted;
			// Forks occur in the middle half of the slope, leaving roots legible and
			// avoiding the stubby twigs caused by forking only at an existing coast tip.
			const int index = int(parent.size() * (0.25 + 0.5 * roll()));
			const auto root = parent[index];
			const double heading =
				std::atan2(parent[index + 1].y - root.y, parent[index + 1].x - root.x);
			const double side = (b % 2 ? -1 : 1);
			const double length = (parent.size() - index) * kDownhillStep * (0.35 + 0.25 * roll());
			auto branch = bentPath({root.x, root.y}, heading + side * (0.35 + 0.3 * roll()), length,
								   (roll() - 0.5) * 0.2, root.halfWidth * 0.75, 2.0,
								   std::max(3, int(length / kDownhillStep)));
			bool fits = true;
			for (size_t f = 0; f < L.flows.size(); ++f)
				if (L.flowOwner[f] != k && pathClearance(branch, L.flows[f]) < kBranchClearance)
					fits = false;
			// A side branch must also travel downhill and stop before the outer beach;
			// only the primary reach control determines which gaps remain broad.
			const auto origin = L.stretch.undo(root.x - L.cx, root.y - L.cy);
			double previousRadius = std::hypot(origin.x, origin.y);
			for (const auto &p : branch)
			{
				const ShapePoint q = L.stretch.undo(p.x - L.cx, p.y - L.cy);
				const double radial = std::hypot(q.x, q.y);
				if (radial + 1e-6 < previousRadius || radial < L.rootRadius ||
					radial + p.halfWidth + 8 > coast.radiusAt(std::atan2(q.y, q.x)))
					fits = false;
				previousRadius = radial;
			}
			if (!fits)
			{
				++refused;
				continue; // Optional growth is omitted, never clipped across another wedge.
			}
			L.flows.push_back(std::move(branch));
			L.flowOwner.push_back(k);
			++branchesPlaced;
		}
	}
	// The flows are vertex masks: primary tongues live, side branches cooled. A tile takes a
	// flow's terrain from any of its corners, so a corner stroke spoils one more row of tiles than
	// the tile stroke the old stone tongues used; half a tile less width keeps their footprint.
	// The width swells and narrows along the flow by up to kGirthSwing either way, and a primary
	// tongue spreads into a wider toe over its last kToePoints points, so flows read as poured
	// rather than drawn with a constant pen. A flow never thins below kThinnestFlow corners.
	std::vector<unsigned char> live(t.size(), 0), cooled(t.size(), 0);
	const PeriodicNoise girth(t.w, t.h, kGirthCell, context.stream("lava-girth"));
	for (size_t f = 0; f < L.flows.size(); ++f)
	{
		auto corners = L.flows[f];
		const int n = int(corners.size());
		for (int k = 0; k < n; ++k)
		{
			auto &p = corners[k];
			const double swell = 1 + kGirthSwing * (2 * girth.at(p.x, p.y) - 1);
			const double toe = int(f) < o.tongues && k >= n - kToePoints ? kToeSpread : 1.0;
			p.halfWidth = std::max(kThinnestFlow, p.halfWidth * swell * toe - 0.5);
		}
		strokePath(int(f) < o.tongues ? live : cooled, t, corners);
	}
	// A long tongue reaches the sea, so it gets one ford: every live vertex within
	// kFordHalfLength tiles, along the flow, of a point partway down.
	auto &cooling = context.stream("lava-cooling");
	std::vector<unsigned char> ford(t.size(), 0);
	std::vector<std::vector<int>> fordVertices;
	for (int k = 0; k < o.tongues; ++k)
	{
		const auto &path = L.flows[k];
		const int n = int(path.size());
		if (!reachesSea[k] || n < 3)
			continue;
		const int at =
			std::clamp(int(n * (kFordFrom + kFordSpan * (cooling() / 4294967296.0))), 1, n - 2);
		const StrokePoint &p = path[at];
		const double hx = path[at + 1].x - path[at - 1].x, hy = path[at + 1].y - path[at - 1].y;
		const double length = std::max(1e-6, std::hypot(hx, hy));
		const int px = int(std::floor(p.x)), py = int(std::floor(p.y));
		const int reach = int(std::ceil(p.halfWidth + kFordHalfLength)) + 2;
		std::vector<int> cut;
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
			{
				const double along = ((px + dx - p.x) * hx + (py + dy - p.y) * hy) / length;
				const int i = t.at(px + dx, py + dy);
				if (std::abs(along) <= kFordHalfLength && live[i] && !ford[i])
				{
					ford[i] = 1;
					cut.push_back(i);
				}
			}
		fordVertices.push_back(std::move(cut));
	}
	for (int i = 0; i < t.size(); ++i)
	{
		if (L.lake[i])
			L.terrain[i] = LAVA;
		else if (L.rim[i])
			L.terrain[i] = DIRT_TRACK;
	}
	if (o.islets)
	{
		int sea = 0;
		for (int i = 0; i < t.size(); ++i)
			sea += L.terrain[i] == WATER;
		const int wanted = std::max(kFewestIslets, sea / kSeaPerIslet);
		L.islets = raiseIslands(L.terrain, t, context,
								{"lava-islets", wanted, kIsletAttempts, kIsletMoat});
		context.telemetry.measure("lava-shield.islets.wanted", wanted);
		context.telemetry.measure("lava-shield.islets.actual", L.islets.size());
	}
	std::vector<unsigned char> land(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		land[i] = L.terrain[i] == GRASS;
	layBeaches(L.terrain, t);
	// Live lava runs straight into the sea, so it replaces the beach the pass just laid.
	// The fords and the cooled branches are scree; a branch's edge is gravel.
	for (int i = 0; i < t.size(); ++i)
	{
		if (!land[i])
			continue;
		if (ford[i])
			L.terrain[i] = SCREE;
		else if (live[i])
			L.terrain[i] = LAVA;
		else if (cooled[i] && L.terrain[i] == GRASS)
			L.terrain[i] = SCREE;
	}
	const auto around = [&](int i, auto test)
	{
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (test(t.at(x + dx, y + dy)))
					return true;
		return false;
	};
	{
		const TerrainSketch drawn = L.terrain;
		for (int i = 0; i < t.size(); ++i)
			if (drawn[i] == SCREE && cooled[i] && !ford[i] &&
				around(i, [&](int j) { return !cooled[j] && drawn[j] != LAVA; }))
				L.terrain[i] = GRAVEL;
	}
	// Vents: small void holes in the middle of the lava lake.
	auto &ventStream = context.stream("lava-vents");
	const int vents = 1 + int(ventStream() % kMostVents);
	for (int v = 0; v < vents; ++v)
	{
		const double a = 2 * kPi * (ventStream() / 4294967296.0);
		const double r = L.lakeRadius * 0.45 * (ventStream() / 4294967296.0);
		const ShapePoint c = L.stretch.apply(L.cx, L.cy, polarPoint(L.cx, L.cy, r, a));
		const int span = int(std::ceil(kVentRadius));
		for (int dy = -span; dy <= span; ++dy)
			for (int dx = -span; dx <= span; ++dx)
			{
				const int i = t.at(int(std::floor(c.x)) + dx, int(std::floor(c.y)) + dy);
				if (L.lake[i] && std::hypot(dx, dy) <= kVentRadius)
					L.terrain[i] = VOID_HOLE;
			}
	}
	// Ember fringes every lava vertex that touches anything but lava.
	{
		const TerrainSketch drawn = L.terrain;
		for (int i = 0; i < t.size(); ++i)
			if (drawn[i] == LAVA &&
				around(i, [&](int j) { return drawn[j] != LAVA && drawn[j] != VOID_HOLE; }))
				L.terrain[i] = EMBER_FIELD;
	}
	const auto isHot = [&](int i)
	{ return L.terrain[i] == LAVA || L.terrain[i] == EMBER_FIELD || L.terrain[i] == VOID_HOLE; };
	// The crater wall inside the circuit is ash; the rim outside it is volcanic loam. The
	// margins along the flows wait for the towns (furnishMargins).
	L.flowEdge.assign(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		L.flowEdge[i] = (live[i] && isHot(i)) ||
						(cooled[i] && (L.terrain[i] == SCREE || L.terrain[i] == GRAVEL));
	L.fordCorners = ford;
	auto &palette = context.stream("lava-palette");
	const PeriodicNoise ash(t.w, t.h, 7, palette), soil(t.w, t.h, 9, palette),
		rimEdge(t.w, t.h, 10, palette), seaFloor(t.w, t.h, 20, palette);
	int craterAsh = 0, loam = 0, deep = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if (L.terrain[i] != GRASS)
			continue;
		const ShapePoint q = L.stretch.undo(x - L.cx, y - L.cy);
		const double radius = std::hypot(q.x, q.y);
		const double circuit = crater.radiusAt(std::atan2(q.y, q.x)) + 5;
		if (radius < circuit - 2)
		{
			L.terrain[i] = ash.at(x, y) > 0.5 ? CLAY : DIRT;
			++craterAsh;
		}
		else if (radius > circuit + 2 &&
				 radius < L.rootRadius + 1 + kRimSwing * (2 * rimEdge.at(x, y) - 1))
		{
			L.terrain[i] = soil.at(x, y) > 0.62 ? MOSS : LOAM;
			++loam;
		}
	}
	// Open sea far from any land is deep water.
	{
		std::vector<unsigned char> ground(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			ground[i] = L.terrain[i] != WATER;
		const auto offshore = stepsFrom(t, ground);
		for (int i = 0; i < t.size(); ++i)
			if (L.terrain[i] == WATER &&
				offshore[i] >= kDeepWater + kDeepSwing * (2 * seaFloor.at(t.remainderX(i), i / t.w) - 1))
			{
				L.terrain[i] = DEEP_WATER;
				++deep;
			}
	}
	// Tile masks from the finished corners.
	L.hot.assign(t.size(), 0);
	L.wall.assign(t.size(), 0);
	L.fords.assign(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		const int corners[4] = {i, t.at(x + 1, y), t.at(x, y + 1), t.at(x + 1, y + 1)};
		int hot = 0;
		bool forded = false;
		for (int c : corners)
		{
			hot += isHot(c);
			forded = forded || ford[c];
		}
		L.hot[i] = hot > 0;
		L.wall[i] = hot == 4;
		L.fords[i] = forded && hot < 4;
	}
	for (const auto &cut : fordVertices)
	{
		std::vector<int> tiles;
		for (int i : cut)
			if (L.fords[i])
				tiles.push_back(i);
		L.fordTiles.push_back(std::move(tiles));
	}
	int lavaVertices = 0;
	for (int i = 0; i < t.size(); ++i)
		lavaVertices += live[i] && isHot(i);
	context.telemetry.measure("lava-shield.lava.vertices", lavaVertices);
	context.telemetry.measure("lava-shield.lava.fords", L.fordTiles.size());
	context.telemetry.measure("lava-shield.lava.vents", vents);
	context.telemetry.measure("lava-shield.crater.ash", craterAsh);
	context.telemetry.measure("lava-shield.ground.loam", loam);
	context.telemetry.measure("lava-shield.ground.deep-water", deep);
	context.telemetry.measure("lava-shield.tongues.requested", o.tongues);
	context.telemetry.measure("lava-shield.tongues.long", longCount);
	context.telemetry.measure("lava-shield.branches.requested", branchesWanted);
	context.telemetry.measure("lava-shield.branches.placed", branchesPlaced);
	context.telemetry.measure("lava-shield.branches.refused", refused);
	if (refused)
		context.telemetry.fallback("lava-shield.branches.omitted",
								   "Collision or coastal clearance");
	context.telemetry.measure("lava-shield.crater.radius", L.lakeRadius);
	context.telemetry.measure("lava-shield.rim.width", o.rimWidth);
	return L;
}

// The towns' plan: one shape per map, drawn at every town with the same frayed edge, so every
// colony starts on the same ground while maps differ (maintainer review 2026-09-16: "the most
// basic" square every time, "not so exacting"). Each shape seats about as many buildings as the
// 16x16 square it replaced: roughly 250 clear tiles.
enum class TownShape
{
	Square,  // 16x16
	Wide,    // 18x14, about 4:3
	Long,    // 19x13, about 3:2
	Strip,   // 22x11, 2:1
	Rounded, // 17x17 with rounded corners
	Octagon, // 17x17 with its corners cut
	Oval,    // 20x16
	Count
};
const char *const kTownShapeNames[] = {"square",  "wide",    "long", "strip",
									   "rounded", "octagon", "oval"};
struct Town
{
	TownShape shape = TownShape::Square;
	bool turned = false;
	// Corner offsets from the town's centre: its clear grass, and the sand ring round it.
	std::vector<std::pair<int, int>> grass, ring;
	// Half the town's longer and shorter sides, before fraying.
	int half = 0, narrow = 0;
	// Every tile the town needs usable (pure grass, no lava) before it is stamped: each tile with a
	// corner in the ring, thicker stretches included, so the sand can never take a corner from a
	// tile of stone.
	std::vector<std::pair<int, int>> footprint;
	// Where a town's protected approach may leave from: two corners beyond its plain ring.
	std::vector<std::pair<int, int>> departures;
};
Town townPlan(GenerationContext &context)
{
	Town town;
	town.shape = TownShape(context.bounded("lava-town", unsigned(TownShape::Count)));
	town.turned = context.bounded("lava-town", 2);
	static const int sizes[][2] = {{16, 16}, {18, 14}, {19, 13}, {22, 11},
								   {17, 17}, {17, 17}, {20, 16}};
	int w = sizes[int(town.shape)][0], h = sizes[int(town.shape)][1];
	if (town.turned)
		std::swap(w, h);
	town.narrow = std::min(w, h) / 2;
	// Spacing and starter catchments go by the plan's own size, not its frayed edge, so a square
	// town keeps the square's 28-tile spacing and 16-tile starter fields.
	town.half = std::max(w, h) / 2;
	// The edge frays: round the town, a stretch of its boundary sits a corner in or out of the
	// plan, and here and there the sand ring runs a corner thicker. Thirty-two stretches, each a
	// couple of tiles, so the edge wanders rather than jitters; the same for every town.
	constexpr int kStretches = 32;
	int fray[kStretches], thick[kStretches];
	for (int k = 0; k < kStretches; ++k)
	{
		const int roll = int(context.bounded("lava-town", 4));
		fray[k] = roll == 0 ? -1 : roll == 3 ? 1 : 0;
		thick[k] = context.bounded("lava-town", 3) == 0;
	}
	// Positions are doubled so odd sides stay centred: X runs -w..w over the corners 0..w.
	const auto stretch = [&](int X, int Y)
	{
		const int ax = std::abs(X), ay = std::abs(Y);
		const int q = ax + ay == 0 ? 0 : std::min(7, 8 * ay / (ax + ay));
		return X >= 0 ? (Y >= 0 ? q : 31 - q) : (Y >= 0 ? 15 - q : 16 + q);
	};
	const auto inside = [&](int X, int Y)
	{
		const int grow = 2 * fray[stretch(X, Y)];
		const int W = w + grow, H = h + grow, ax = std::abs(X), ay = std::abs(Y);
		if (ax > W || ay > H)
			return false;
		switch (town.shape)
		{
		case TownShape::Rounded:
		{
			constexpr int round = 10; // a five-tile corner radius
			const int qx = ax - (W - round), qy = ay - (H - round);
			return qx <= 0 || qy <= 0 || qx * qx + qy * qy <= round * round + round;
		}
		case TownShape::Octagon:
			return ax + ay <= W + H - 10; // corners cut five tiles back along each side
		case TownShape::Oval:
		{
			const long long rx = W + 1, ry = H + 1;
			return ax * ax * ry * ry + ay * ay * rx * rx <= rx * rx * ry * ry;
		}
		default:
			return true;
		}
	};
	const int reach = std::max(w, h) / 2 + 2, span = reach + kHomeRing + 3;
	for (int dy = -reach; dy <= reach; ++dy)
		for (int dx = -reach; dx <= reach; ++dx)
			if (inside(2 * dx + w / 2 * 2 - w, 2 * dy + h / 2 * 2 - h))
				town.grass.push_back({dx, dy});
	const int edge = reach + kHomeRing + 2;
	std::vector<unsigned char> footprint((2 * span + 1) * (2 * span + 1), 0);
	const auto cell = [&](int dx, int dy) { return (dy + span) * (2 * span + 1) + dx + span; };
	for (int dy = -edge; dy <= edge; ++dy)
		for (int dx = -edge; dx <= edge; ++dx)
		{
			int nearest = INT_MAX;
			for (const auto &[gx, gy] : town.grass)
				nearest = std::min(nearest, std::max(std::abs(dx - gx), std::abs(dy - gy)));
			const bool bump = thick[stretch(2 * dx + w / 2 * 2 - w, 2 * dy + h / 2 * 2 - h)];
			if (nearest >= 1 && nearest <= kHomeRing + bump)
				town.ring.push_back({dx, dy});
			if (nearest <= kHomeRing + 1)
				for (int ty = dy - 1; ty <= dy; ++ty)
					for (int tx = dx - 1; tx <= dx; ++tx)
						footprint[cell(tx, ty)] = 1;
			if (nearest == kHomeRing + 2)
				town.departures.push_back({dx, dy});
		}
	for (int dy = -span; dy <= span; ++dy)
		for (int dx = -span; dx <= span; ++dx)
			if (footprint[cell(dx, dy)])
				town.footprint.push_back({dx, dy});
	context.telemetry.choice("lava-shield.town.shape", kTownShapeNames[int(town.shape)]);
	context.telemetry.measure("lava-shield.town.grass-corners", town.grass.size());
	context.telemetry.measure("lava-shield.town.half-extent", town.half);
	return town;
}

// Rank terrain candidates after beaches and the flows, before their margins. The completed
// colony score below is the deciding measurement; this shortlist only bounds its cost.
std::vector<RankedSite> candidateSites(const Layout &L, Map &map, const Town &town)
{
	const Torus &t = L.t;
	// Pure grass, or the rim's loam, which a town turns back to grass (populate).
	TerrainSketch ground = L.terrain;
	for (auto &v : ground)
		if (v == LOAM || v == MOSS)
			v = GRASS;
	const auto usable = pureTiles(ground, t, GRASS);
	// A cheap Chebyshev bound first, then the town's own footprint: a long town fits ground a
	// square of its length would not.
	const auto room = clearance(t, usable);
	const auto fits = [&](int x, int y)
	{
		for (const auto &[dx, dy] : town.footprint)
			if (!usable[t.at(x + dx, y + dy)])
				return false;
		return true;
	};
	auto water = pureTiles(L.terrain, t, WATER);
	// Rank COASTAL candidates. Counting crater water here could select a town on
	// the neutral rim because it has the same nominal water distance as a beach.
	for (int i = 0; i < t.size(); ++i)
		water[i] = water[i] && !L.lake[i];
	const auto offshore = stepsFrom(t, water);
	// Before planting, measure potential fertility: deposit-gating would return zero
	// everywhere on this still-unseeded terrain. Final StartQuality uses the normal gate.
	const auto fertility = Fertility::forMap(map, false);
	std::vector<RankedSite> sites;
	for (int y = 0; y < t.h; y += kSiteStride)
		for (int x = 0; x < t.w; x += kSiteStride)
		{
			const int i = t.at(x, y);
			const auto q = L.stretch.undo(x - L.cx, y - L.cy);
			// Keep the summit neutral; shore distance alone would also admit crater homes.
			if (offshore[i] < kHomeWaterMin || offshore[i] > kHomeWaterMax ||
				std::hypot(q.x, q.y) < L.rootRadius + town.half ||
				room[i] < town.narrow + kHomeRing + 2 ||
				!fits(x, y))
				continue;
			double fertile = 0;
			for (int dy = -town.half - 3; dy <= town.half + 3; ++dy)
				for (int dx = -town.half - 3; dx <= town.half + 3; ++dx)
					fertile += fertility.at(t.x(x + dx), t.y(y + dy));
			sites.push_back({i, fertile});
		}
	std::stable_sort(sites.begin(), sites.end(),
					 [](const RankedSite &a, const RankedSite &b) { return a.weight > b.weight; });
	return sites;
}

std::vector<int> chooseSites(const Layout &L, const std::vector<RankedSite> &sites,
							 GenerationContext &context, int deal, const Town &town)
{
	if (sites.empty())
		return {};
	const std::string stream = "lava-homes/" + std::to_string(deal);
	const size_t first = context.bounded(stream, std::max<size_t>(1, sites.size() / 2));
	auto weighted = sites;
	// Start in the fertile half; then spread with a .6 weight floor so fertility
	// cannot overwhelm separation. Two town envelopes (the square's was 20 tiles) plus an
	// eight-tile gathering/expansion gap. Failed spreads do not shrink towns.
	for (auto &site : weighted)
		site.weight = 0.6 + 0.4 * site.weight / std::max(1.0, sites.front().weight);
	auto picked = spreadRankedSites(L.t, weighted, context.request.nbTeams,
									2 * (town.half + kHomeRing) + 8, first);
	dealStarts(context, picked, stream.c_str());
	return picked;
}

// The ground along the flows, laid once the towns, their rings and their roads are known so it
// never takes a town's room: by stretches, a crust of stone on the grass, bare grass, a scree
// apron or ash. `keep` marks the corners it must leave alone. Returns the crust's stone tiles.
std::vector<unsigned char> furnishMargins(const Layout &L, TerrainSketch &terrain,
										  const std::vector<unsigned char> &keep,
										  GenerationContext &context)
{
	const Torus &t = L.t;
	const auto fromFlow = stepsFrom(t, L.flowEdge);
	const auto fromFord = stepsFrom(t, L.fordCorners);
	auto &stream = context.stream("lava-margins");
	const PeriodicNoise edge(t.w, t.h, kEdgeCell, stream), ash(t.w, t.h, 7, stream),
		slope(t.w, t.h, 9, stream);
	const double scale = std::clamp(std::min(t.w, t.h) / 256.0, 0.5, 1.0);
	const auto reach = [&](int r) { return std::max(1, int(std::lround(r * scale))); };
	const int crustReach = reach(kCrustReach), screeReach = reach(kScreeReach),
			  ashReach = reach(kAshReach);
	std::vector<unsigned char> crust(t.size(), 0);
	int scree = 0, ashen = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		const int d = fromFlow[i];
		if (terrain[i] != GRASS || keep[i])
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		// The upper slopes are ashen in patches that thin out downhill.
		const ShapePoint q = L.stretch.undo(x - L.cx, y - L.cy);
		const double down =
			(std::hypot(q.x, q.y) - L.rootRadius) / (kAshSlope * (L.islandRadius - L.rootRadius));
		if (down >= 0 && down < 1 && slope.at(x, y) > kAshFrom + (1 - kAshFrom) * down)
		{
			terrain[i] = ash.at(x, y) > 0.5 ? CLAY : DIRT;
			++ashen;
			continue;
		}
		if (d < 1)
			continue;
		const double n = edge.at(x, y);
		if (n < kCrustBelow)
			crust[i] = d <= crustReach && (fromFord[i] < 0 || fromFord[i] > kFordClearance);
		else if (n < kBareBelow)
			continue;
		else if (n < kScreeBelow && d <= screeReach)
		{
			terrain[i] = SCREE;
			++scree;
		}
		else if (n >= kScreeBelow && d <= ashReach)
		{
			terrain[i] = ash.at(x, y) > 0.5 ? CLAY : DIRT;
			++ashen;
		}
	}
	const auto grass = pureTiles(terrain, t, GRASS);
	std::vector<unsigned char> rock(t.size(), 0);
	int stone = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		rock[i] = grass[i] && (crust[i] || crust[t.at(x + 1, y)] || crust[t.at(x, y + 1)] ||
							   crust[t.at(x + 1, y + 1)]);
		stone += rock[i];
	}
	context.telemetry.measure("lava-shield.margin.crust", stone);
	context.telemetry.measure("lava-shield.margin.scree", scree);
	context.telemetry.measure("lava-shield.margin.ash", ashen);
	return rock;
}

// Materialize a candidate and the winning world through exactly the same stages.
// The caller restores engine RNG before each trial, so rejected worlds cannot alter
// resource amounts/varieties in the chosen world. All other streams are local contexts.
bool populate(Game &game, GenerationContext &context, const Layout &L, const Town &town,
			  const std::vector<int> &homes)
{
	const Torus &t = L.t;
	const LavaShieldOptions o(context.request);
	TerrainSketch terrain = L.terrain;
	Farm clearings;
	clearings.water.assign(t.size(), 0);
	clearings.sand.assign(t.size(), 0);
	clearings.plot.assign(t.size(), 0);
	// A dry town: clear grass inside a sand ring, no farm rows or new water. The plot is every
	// tile whose four corners are the town's grass; a town reaching onto the rim's loam turns its
	// own ground back to grass, so no crop can take root inside it.
	std::vector<unsigned char> townGrass(t.size(), 0);
	for (int p : homes)
	{
		for (const auto &[dx, dy] : town.grass)
		{
			const int i = t.at(t.remainderX(p) + dx, p / t.w + dy);
			townGrass[i] = 1;
			if (terrain[i] == LOAM || terrain[i] == MOSS)
				terrain[i] = GRASS;
		}
		for (const auto &[dx, dy] : town.ring)
		{
			const int i = t.at(t.remainderX(p) + dx, p / t.w + dy);
			clearings.sand[i] = 1;
			terrain[i] = SAND;
		}
	}
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		clearings.plot[i] = townGrass[i] && townGrass[t.at(x + 1, y)] &&
							townGrass[t.at(x, y + 1)] && townGrass[t.at(x + 1, y + 1)];
	}
	auto reserve = roadTiles(t, clearings.sand);
	for (int i = 0; i < t.size(); ++i)
		reserve[i] = reserve[i] || clearings.plot[i];
	// The crater circuit alone cannot keep a town's approach open through late crop
	// growth. Reserve a three-tile lane from just outside each town's ring; it becomes a
	// dirt track like the circuit. Protect other towns and every lava corner.
	// A one-tile trail is an explicit fallback for narrow natural gaps; neither width
	// may cut stone, lava or water.
	auto protectedGround = L.hot;
	for (int i = 0; i < t.size(); ++i)
		protectedGround[i] = protectedGround[i] || clearings.plot[i];
	std::vector<unsigned char> track(t.size(), 0);
	// A constant-cost cardinal shortest path makes ruler-straight L-shaped roads.
	// A 24-tile smooth cost field (100..399) instead follows broad irregular hollows.
	// This affects scenery and travel cost, never the rock/water protection contract.
	const PeriodicNoise trails(t.w, t.h, 24, context.stream("lava-trails"));
	std::vector<int> trailCost(t.size());
	for (int i = 0; i < t.size(); ++i)
		trailCost[i] = 100 + int(300 * trails.at(t.remainderX(i), i / t.w));
	std::vector<unsigned char> existingPassage; // populated only if a wide approach fails
	for (int p : homes)
	{
		std::vector<int> sources;
		for (const auto &[dx, dy] : town.departures)
			sources.push_back(t.at(t.remainderX(p) + dx, p / t.w + dy));
		const TerrainSketch before = terrain;
		auto path = reserveSandRoute(terrain, t, sources, L.rim, protectedGround, 1, &trailCost,
									 GridNeighbors::Eight);
		int width = 3;
		if (path.empty())
		{
			// A legal beach bypass need not fit a newly painted pure-sand tile: a
			// mixed sand/grass tile may share corners with protected stone. Reuse
			// already walkable, non-grass ground unchanged; crops cannot colonize it.
			// New parts of the one-tile trail retain every water/stone corner guard.
			// The wide failure is atomic. Only the route width changes, never town
			// size, water, rock or quality floors. Single-file travel can be congested.
			if (existingPassage.empty())
			{
				// Use the engine's terrain predicates rather than guessing which mixed
				// shoreline tiles are walkable. No resources/buildings exist yet.
				// Later reservations only add sand, so this cached permission stays true.
				writeVertices(game.map, terrain);
				existingPassage.assign(t.size(), 0);
				for (int i = 0; i < t.size(); ++i)
					existingPassage[i] =
						!(game.map.canResourcesGrow(t.remainderX(i), i / t.w) &&
							(game.map.terrainSupportsResourceAtByIndex(t.remainderX(i), i / t.w, WHEAT) ||
							game.map.terrainSupportsResourceAtByIndex(t.remainderX(i), i / t.w, WOOD))) &&
						game.map.isHardSpaceForGroundUnit(t.remainderX(i), i / t.w, false, 0);
			}
			width = 1;
			path = reserveSandRoute(terrain, t, sources, L.rim, protectedGround, 0, &trailCost,
									GridNeighbors::Eight, &existingPassage);
			if (!path.empty())
				context.telemetry.fallback("lava-shield.approach.narrow",
										   "Three-tile approach unavailable; reserved one tile");
		}
		if (path.empty())
		{
			context.detail = "A coastal town has no room for a protected crater approach.";
			return false;
		}
		context.telemetry.measure("lava-shield.approach.steps", path.size());
		context.telemetry.measure("lava-shield.approach.width", width);
		for (int i = 0; i < t.size(); ++i)
			track[i] = track[i] || (before[i] != SAND && terrain[i] == SAND);
	}
	// The approaches become dirt tracks, except where they touch water: a beach stays sand.
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		bool shore = false;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const unsigned char v = terrain[t.at(x + dx, y + dy)];
				shore = shore || v == WATER || v == DEEP_WATER;
			}
		if (track[i] && !shore)
			terrain[i] = DIRT_TRACK;
	}
	std::vector<unsigned char> sand(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		sand[i] = terrain[i] == SAND || terrain[i] == DIRT_TRACK;
	const auto roadGround = roadTiles(t, sand);
	for (int i = 0; i < t.size(); ++i)
		reserve[i] = reserve[i] || roadGround[i];
	auto rock = furnishMargins(L, terrain, tileCorners(t, reserve), context);
	for (int i = 0; i < t.size(); ++i)
		rock[i] = rock[i] && !reserve[i];
	writeVertices(game.map, terrain);
	Map &map = game.map;
	for (int i = 0; i < t.size(); ++i)
		if (rock[i])
			map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1);
	for (int team = 0; team < context.request.nbTeams; ++team)
	{
		game.addTeam();
		const int p = homes[team];
		std::vector<unsigned char> own(t.size(), 0);
		for (const auto &[dx, dy] : town.grass)
		{
			const int i = t.at(t.remainderX(p) + dx, p / t.w + dy);
			own[i] = clearings.plot[i];
		}
		if (!placeSettlement(game, context, team, own, {t.remainderX(p) - 2, p / t.w - 2}, "lava-settle"))
			return false;
	}
	context.stage = "lava resources";
	// Before planting, measure potential fertility: deposit-gating would return zero
	// everywhere on this still-unseeded terrain. Final StartQuality uses the normal gate.
	const auto fertility = Fertility::forMap(map, false);
	// Two fields outside each town, selected by actual fertility, stay renewable at
	// zero ambient abundance. A Chebyshev radius eight past the town's edge (16 for the
	// square town) keeps their harvest edges near the workers while the town's sand ring
	// prevents them invading its building land.
	for (int team = 0; team < int(homes.size()); ++team)
	{
		const int p = homes[team];
		for (int type : {WHEAT, WOOD})
		{
			int seed = -1;
			std::uint32_t best = 0;
			const int catchment = town.half + 8;
			for (int dy = -catchment; dy <= catchment; ++dy)
				for (int dx = -catchment; dx <= catchment; ++dx)
				{
					const int i = t.at(t.remainderX(p) + dx, p / t.w + dy);
					if (reserve[i] || rock[i] || !clearGround(map, t.remainderX(i), i / t.w))
						continue;
					if (fertility.at(t.remainderX(i), i / t.w) > best)
					{
						best = fertility.at(t.remainderX(i), i / t.w);
						seed = i;
					}
				}
			if (seed < 0)
			{
				context.detail = "A coastal town has no fertile external starter field.";
				return false;
			}
			const int target = type == WHEAT ? kStarterWheat : kStarterWood;
			const auto eligible = [&](int i)
			{
				return !reserve[i] && !rock[i] &&
					   clearGround(map, t.remainderX(i), i / t.w) &&
					   map.isResourceAllowed(t.remainderX(i), i / t.w, type) &&
					   fertility.at(t.remainderX(i), i / t.w) > 0 &&
					   t.chebyshev(t.remainderX(p), p / t.w, t.remainderX(i), i / t.w) <= town.half + 10;
			};
			int planted = growPatch(map, t, seed, type, target, eligible);
			// The highest-fertility seed can sit on a tiny isolated tile beside a
			// sand approach or lava corner. Keep the original single compact field
			// whenever it supplies at least half the target. Only a failing field
			// tries up to three additional starts, ranked by eligible tiles in a
			// five-by-five neighbourhood before fertility. This is a bounded local
			// rescue, not a relaxation of crop frontage or town quality floors.
			// The existing patch and all other deposits remain protected by eligible;
			// no new RNG is drawn. An already-sufficient proposal remains identical;
			// the chosen world may improve if a formerly rejected proposal wins.
			for (int retry = 0; planted < target / 2 && retry < 3; ++retry)
			{
				const int extra = seedForPatchCapacity(
					t, t.remainderX(p), p / t.w, town.half + 8, 2, eligible,
					[&](int i) { return double(fertility.at(t.remainderX(i), i / t.w)); });
				if (extra < 0)
					break; // No legal fertile tile remains in the starter catchment.
				const int gained = growPatch(map, t, extra, type, target - planted, eligible);
				if (gained <= 0)
					break; // Defensive guard if the engine rejects an otherwise eligible tile.
				planted += gained;
				context.telemetry.fallback("lava-shield.starter.secondary",
									   "A small starter field used an additional compact patch");
			}
			context.telemetry.measure(type == WHEAT ? "lava-shield.starter.wheat"
													: "lava-shield.starter.wood",
									  planted, team);
			if (planted < target / 2)
			{
				context.detail = "A coastal town's renewable starter field is too small.";
				return false;
			}
		}
	}
	// Ambient crops form compact patches, never a uniform speckle over the dry slope.
	// A patch seed needs positive engine fertility; its growth is bounded to eight
	// tiles of radius so a lucky seed cannot monopolize an entire connected shore.
	auto &rng = context.stream("lava-crops");
	int wheatPlaced = 0, woodPlaced = 0, fruitPlaced = 0, stonePlaced = 0;
	for (int y = 0; y < t.h; y += 7)
		for (int x = 0; x < t.w; x += 7)
		{
			const int i =
				t.at(x + context.bounded("lava-crops", 7), y + context.bounded("lava-crops", 7));
			if (reserve[i] || rock[i] || !clearGround(map, t.remainderX(i), i / t.w))
				continue;
			const int type = rng() % 3 == 0 ? WOOD : WHEAT;
			const int amount = type == WHEAT ? o.wheat : o.wood;
			const int target = int(scaledCount(type == WHEAT ? 12 : 8, amount));
			if (fertility.at(t.remainderX(i), i / t.w) > 0 && target > 0)
			{
				const int planted =
					growPatch(map, t, i, type, target,
							  [&](int j)
							  {
								  return !reserve[j] && !rock[j] &&
										 clearGround(map, t.remainderX(j), j / t.w) &&
										 fertility.at(t.remainderX(j), j / t.w) > 0 &&
										 t.chebyshev(t.remainderX(i), i / t.w, t.remainderX(j), j / t.w) <= 8;
							  });
				(type == WHEAT ? wheatPlaced : woodPlaced) += planted;
			}
		}
	// Prize deposits stay sparse enough for inns and movement. The square lattice is
	// only a sampling grid; jitter breaks rows and eligibility restricts fruit to the rim.
	for (int y = 0; y < t.h; y += 5)
		for (int x = 0; x < t.w; x += 5)
		{
			const int i =
				t.at(x + context.bounded("lava-prizes", 5), y + context.bounded("lava-prizes", 5));
			if (reserve[i] || rock[i] || !clearGround(map, t.remainderX(i), i / t.w))
				continue;
			const auto q = L.stretch.undo(t.remainderX(i) - L.cx, i / t.w - L.cy);
			const double radius = std::hypot(q.x, q.y);
			if (radius > L.lakeRadius + 7 && radius < L.rootRadius + 3 &&
				context.bounded("lava-prizes", 1000) < unsigned(scaledCount(450, o.fruit)))
			{
				map.setResourceByIndex(t.remainderX(i), i / t.w, CHERRY + context.bounded("lava-prizes", 3), 1);
				++fruitPlaced;
			}
			else if (radius > L.rootRadius &&
					 context.bounded("lava-prizes", 10000) < unsigned(scaledCount(30, o.stone)))
			{
				map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1);
				++stonePlaced;
			}
		}
	seedAlgae(map, context, t, "lava-algae", o.algae,
			  AlgaeBand::shallows(1, 12, 100).thriving(0.5));
	stockIslands(map, context, L.islets, "lava-islets");
	secureStartingCrops(game, context, t, kWheatRange, kWoodRange, 0, &rock);
	// Crops may hide the approaches to the reserved circuit. Only clearable crops may
	// be opened; fruit, rock and water are never converted into an accidental shortcut.
	auto blocked = rock;
	for (int i = 0; i < t.size(); ++i)
	{
		const int type = map.getResource(t.remainderX(i), i / t.w).type;
		// NO_RES_TYPE is a high sentinel, not a fruit. Restrict the enum range.
		if (type == STONE || (type >= CHERRY && type <= PRUNE))
			blocked[i] = 1;
	}
	const auto workers = unitTilesByTeam(map, context.request.nbTeams);
	for (const auto &units : workers)
		if (!openRoad(map, t, units, L.rim, &blocked))
		{
			context.detail =
				"A town cannot reach the crater rim without crossing permanent terrain.";
			return false;
		}
	// Starter repair is allowed to clear congestion, but may not smuggle a crop
	// inside a growth-protected town. Such a trial is unsuitable, even if its score
	// momentarily looks excellent; another complete proposal must carry the opening.
	for (int i = 0; i < t.size(); ++i)
		if (clearings.plot[i] && map.isResource(t.remainderX(i), i / t.w))
		{
			context.detail = "Starter repair would plant inside a protected town.";
			return false;
		}
	context.telemetry.measure("lava-shield.ambient.wheat", wheatPlaced);
	context.telemetry.measure("lava-shield.ambient.wood", woodPlaced);
	context.telemetry.measure("lava-shield.prizes.fruit", fruitPlaced);
	context.telemetry.measure("lava-shield.ambient.stone", stonePlaced);
	return true;
}

std::string qualityFailure(const StartQualityReport &report)
{
	if (!report.measured)
		return "Lava shield could not measure its completed colonies.";
	for (const auto &colony : report.colonies)
		if (colony.wheatDistance < 0 || colony.wheatDistance > kWheatRange ||
			colony.woodDistance < 0 || colony.woodDistance > kWoodRange ||
			colony.buildSites < kMinimumSites)
			return "A Lava shield town lacks reachable crops or expansion room.";
	if (report.fairness < kMinimumFairness)
		return "The completed Lava shield towns are too unequal; try another seed.";
	return {};
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "lava layout";
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	writeVertices(game.map, L.terrain);
	const Town town = townPlan(context);
	const auto sites = candidateSites(L, game.map, town);
	context.telemetry.measure("lava-shield.starts.candidates", sites.size());
	std::vector<std::vector<int>> proposals;
	for (int deal = 0; deal < kDeals; ++deal)
		proposals.push_back(chooseSites(L, sites, context, deal, town));
	const auto build = [&](Game &world, GenerationContext &c, const std::vector<int> &homes)
	{ return populate(world, c, L, town, homes); };
	const auto selected = chooseScoredSettlements(context, proposals, build, qualityFailure);
	if (selected.selected < 0)
	{
		context.stage = "lava scored settlements";
		context.detail =
			selected.failure + " Use fewer tongues or colonies, a larger map, or another seed.";
		return false;
	}
	return build(game, context, selected.sites);
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (const auto mismatch = designMismatch(L, game.map, "lava shield"); !mismatch.empty())
		return mismatch;
	const auto sea = pureTiles(L.terrain, L.t, WATER);
	for (int i = 0; i < L.t.size(); ++i)
	{
		const int x = L.t.remainderX(i), y = i / L.t.w;
		if (sea[i] && !game.map.isWater(x, y))
			return "Lava shield lost part of its ocean.";
		if (L.wall[i] && game.map.terrainPropertiesAt(x, y).walkable)
			return "Lava shield lost part of a lava flow or its crater.";
		if (L.rim[i] && !game.map.isHardSpaceForGroundUnit(x, y, false, 0))
			return "Lava shield's reserved crater circuit is blocked.";
	}
	const auto walk =
		walkFromFirstColony(game.map, context.request.nbTeams, "Lava shield", "via the crater rim");
	if (!walk.error.empty())
		return walk.error;
	for (int i = 0; i < L.t.size(); ++i)
		if (L.rim[i] && walk.steps[i] < 0)
			return "A part of the crater circuit is unreachable from the colonies.";
	// Every ford stays open: some tile of it is free ground the colonies reach on foot.
	for (const auto &ford : L.fordTiles)
	{
		bool reached = false;
		for (int i : ford)
			reached = reached || (walk.steps[i] >= 0 &&
								  game.map.isHardSpaceForGroundUnit(L.t.remainderX(i), i / L.t.w, false, 0));
		if (!reached)
			return "A lava ford cannot be crossed on foot.";
	}
	return {};
}
} // namespace

LavaShieldOptions::LavaShieldOptions(const GenerationRequest &r)
	: tongues(r.option("tongue-count")), longTongues(r.option("long-tongues")),
	  branching(r.option("branching")), rimWidth(r.option("rim-width")),
	  islets(r.option("islets") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition lavaShieldDefinition()
{
	return {"lava-shield",
			51,
			"Lava shield",
			// Revision 3: the towns are chosen by the fitted fairness model now. Lava shield is
			// the one generator that ranks its own settlement proposals by the map score
			// (chooseScoredSettlements), and that score changed from the weakest town's quality
			// gated by a worst-over-best ratio to how evenly the towns share the chance of
			// winning, so a different proposal wins on some seeds.
			// Revision 4: towns take one of seven shapes per map, turned either way, with a frayed
			// edge every town shares.
			// Revision 5: the terrain catalogue. The tongues are live lava with one scree ford per
			// long tongue, the crater is a lava lake with vents, branches are cooled scree and
			// gravel, stone is a crust along the flows, the rim is loam, roads are dirt tracks and
			// the open sea is deep water.
			6,
			false,
			{GeneratorControl{"tongue-count", "Lava tongues", 3, 9, 1, 5, ControlGroup::Layout}
				 .withSearchRange(4, 7),
			 GeneratorControl{"long-tongues", "Long tongues", 25, 75, 25, 50, ControlGroup::Layout}
				 .withSearchRange(25, 75),
			 GeneratorControl{"branching", "Branching", 0, 3, 1, 2, ControlGroup::Layout}
				 .withSearchRange(1, 3),
			 GeneratorControl{"rim-width", "Crater rim width", 6, 12, 2, 8, ControlGroup::Terrain}
				 .withSearchRange(8, 12),
			 GeneratorControl::toggle("islets", "Islets", true).withSearchValues({0, 1}),
			 // Both percentage controls intentionally retain the shared 100% default.
			 // The 2026-09-15 paired AI probe tried 125% wheat / 75% wood: it put
			 // roughly 400 more wheat tiles on a 256-square island but increased
			 // Nicowar's final critical hunger and starvation deaths on both tested
			 // seeds, and cost about 2% of initial overlapping building origins.
			 // Food service depends on inns, labor, route use and combat as well as
			 // crop count. See docs/map-generators/LAVA_SHIELD.md and the linked raw
			 // playtest telemetry before changing these defaults for hunger alone.
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			requestFailure,
			validateWorld,
			{"terrain:natural", "feature:volcanic", "feature:mountains", "style:contested-center",
			 "fairness:repeated-wedge"}};
}
