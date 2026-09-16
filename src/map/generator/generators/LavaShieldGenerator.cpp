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
// The lake and its shared rim are reserved before rock grows. See LAVA_SHIELD.md for
// the play contract, the beach-bypass limitation, tuning history and actual evidence.
//
// IMPORTANT: legal stone cannot touch pure water. A long tongue therefore leaves a
// narrow beach detour, not a swimming-only seal. Never "repair" this by writing illegal
// stone on sand, changing simulation rules, or connecting the crater to the ocean.
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

struct Layout
{
	Torus t{1, 1};
	Stretch stretch;
	TerrainSketch terrain;
	std::vector<unsigned char> rock, rim, lake;
	std::vector<std::vector<StrokePoint>> flows;
	std::vector<int> flowOwner;
	std::vector<Island> islets;
	double cx = 0, cy = 0, lakeRadius = 0, rootRadius = 0;
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
	L.terrain.assign(t.size(), WATER);
	L.rock.assign(t.size(), 0);
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
	for (const auto &flow : L.flows)
		strokePath(L.rock, t, flow);
	for (int i = 0; i < t.size(); ++i)
	{
		if (L.lake[i])
			L.terrain[i] = WATER;
		else if (L.rim[i])
			L.terrain[i] = SAND;
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
	layBeaches(L.terrain, t);
	const auto grass = pureTiles(L.terrain, t, GRASS);
	for (int i = 0; i < t.size(); ++i)
		L.rock[i] = L.rock[i] && grass[i];
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

// Rank terrain candidates after beaches and structural stone. The completed colony
// score below is the deciding measurement; this shortlist only bounds its cost.
std::vector<RankedSite> candidateSites(const Layout &L, Map &map, const Town &town)
{
	const Torus &t = L.t;
	auto usable = pureTiles(L.terrain, t, GRASS);
	for (int i = 0; i < t.size(); ++i)
		usable[i] = usable[i] && !L.rock[i];
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
	// tile whose four corners are the town's grass.
	std::vector<unsigned char> townGrass(t.size(), 0);
	for (int p : homes)
	{
		for (const auto &[dx, dy] : town.grass)
			townGrass[t.at(p % t.w + dx, p / t.w + dy)] = 1;
		for (const auto &[dx, dy] : town.ring)
		{
			const int i = t.at(p % t.w + dx, p / t.w + dy);
			clearings.sand[i] = 1;
			terrain[i] = SAND;
		}
	}
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = i % t.w, y = i / t.w;
		clearings.plot[i] = townGrass[i] && townGrass[t.at(x + 1, y)] &&
							townGrass[t.at(x, y + 1)] && townGrass[t.at(x + 1, y + 1)];
	}
	auto reserve = roadTiles(t, clearings.sand);
	for (int i = 0; i < t.size(); ++i)
		reserve[i] = reserve[i] || clearings.plot[i];
	// The lake circuit alone cannot keep a town's approach open through late crop
	// growth. Reserve a three-tile sand lane from just outside each town's ring.
	// Protect other towns and all lava at corner resolution. A one-tile trail is an
	// explicit fallback for narrow natural gaps; neither width may cut rock or water.
	auto protectedGround = L.rock;
	for (int i = 0; i < t.size(); ++i)
		protectedGround[i] = protectedGround[i] || clearings.plot[i];
	// A constant-cost cardinal shortest path makes ruler-straight L-shaped roads.
	// A 24-tile smooth cost field (100..399) instead follows broad irregular hollows.
	// This affects scenery and travel cost, never the rock/water protection contract.
	const PeriodicNoise trails(t.w, t.h, 24, context.stream("lava-trails"));
	std::vector<int> trailCost(t.size());
	for (int i = 0; i < t.size(); ++i)
		trailCost[i] = 100 + int(300 * trails.at(i % t.w, i / t.w));
	std::vector<unsigned char> existingPassage; // populated only if a wide approach fails
	for (int p : homes)
	{
		std::vector<int> sources;
		for (const auto &[dx, dy] : town.departures)
			sources.push_back(t.at(p % t.w + dx, p / t.w + dy));
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
				// shoreline sprites are walkable. No resources/buildings exist yet.
				// Later reservations only add sand, so this cached permission stays true.
				writeUndermap(game.map, terrain);
				existingPassage.assign(t.size(), 0);
				for (int i = 0; i < t.size(); ++i)
					existingPassage[i] =
						!game.map.isGrass(i % t.w, i / t.w) &&
						game.map.isHardSpaceForGroundUnit(i % t.w, i / t.w, false, 0);
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
	}
	std::vector<unsigned char> sand(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		sand[i] = terrain[i] == SAND;
	const auto roadGround = roadTiles(t, sand);
	for (int i = 0; i < t.size(); ++i)
		reserve[i] = reserve[i] || roadGround[i];
	writeUndermap(game.map, terrain);
	Map &map = game.map;
	for (int i = 0; i < t.size(); ++i)
		if (L.rock[i])
			map.setResource(i % t.w, i / t.w, STONE, 1);
	for (int team = 0; team < context.request.nbTeams; ++team)
	{
		game.addTeam();
		const int p = homes[team];
		std::vector<unsigned char> own(t.size(), 0);
		for (const auto &[dx, dy] : town.grass)
		{
			const int i = t.at(p % t.w + dx, p / t.w + dy);
			own[i] = clearings.plot[i];
		}
		if (!placeSettlement(game, context, team, own, {p % t.w - 2, p / t.w - 2}, "lava-settle"))
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
					const int i = t.at(p % t.w + dx, p / t.w + dy);
					if (reserve[i] || L.rock[i] || !clearGround(map, i % t.w, i / t.w))
						continue;
					if (fertility.at(i % t.w, i / t.w) > best)
					{
						best = fertility.at(i % t.w, i / t.w);
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
				return !reserve[i] && !L.rock[i] &&
					   clearGround(map, i % t.w, i / t.w) &&
					   map.isResourceAllowed(i % t.w, i / t.w, type) &&
					   fertility.at(i % t.w, i / t.w) > 0 &&
					   t.chebyshev(p % t.w, p / t.w, i % t.w, i / t.w) <= town.half + 10;
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
					t, p % t.w, p / t.w, town.half + 8, 2, eligible,
					[&](int i) { return double(fertility.at(i % t.w, i / t.w)); });
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
			if (reserve[i] || L.rock[i] || !clearGround(map, i % t.w, i / t.w))
				continue;
			const int type = rng() % 3 == 0 ? WOOD : WHEAT;
			const int amount = type == WHEAT ? o.wheat : o.wood;
			const int target = int(scaledCount(type == WHEAT ? 12 : 8, amount));
			if (fertility.at(i % t.w, i / t.w) > 0 && target > 0)
			{
				const int planted =
					growPatch(map, t, i, type, target,
							  [&](int j)
							  {
								  return !reserve[j] && !L.rock[j] &&
										 clearGround(map, j % t.w, j / t.w) &&
										 fertility.at(j % t.w, j / t.w) > 0 &&
										 t.chebyshev(i % t.w, i / t.w, j % t.w, j / t.w) <= 8;
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
			if (reserve[i] || L.rock[i] || !clearGround(map, i % t.w, i / t.w))
				continue;
			const auto q = L.stretch.undo(i % t.w - L.cx, i / t.w - L.cy);
			const double radius = std::hypot(q.x, q.y);
			if (radius > L.lakeRadius + 7 && radius < L.rootRadius + 3 &&
				context.bounded("lava-prizes", 1000) < unsigned(scaledCount(450, o.fruit)))
			{
				map.setResource(i % t.w, i / t.w, CHERRY + context.bounded("lava-prizes", 3), 1);
				++fruitPlaced;
			}
			else if (radius > L.rootRadius &&
					 context.bounded("lava-prizes", 10000) < unsigned(scaledCount(30, o.stone)))
			{
				map.setResource(i % t.w, i / t.w, STONE, 1);
				++stonePlaced;
			}
		}
	seedAlgae(map, context, t, "lava-algae", o.algae,
			  AlgaeBand::shallows(1, 12, 100).thriving(0.5));
	stockIslands(map, context, L.islets, "lava-islets");
	secureStartingCrops(game, context, t, kWheatRange, kWoodRange, 0, &L.rock);
	// Crops may hide the approaches to the reserved circuit. Only clearable crops may
	// be opened; fruit, rock and water are never converted into an accidental shortcut.
	auto blocked = L.rock;
	for (int i = 0; i < t.size(); ++i)
	{
		const int type = map.getResource(i % t.w, i / t.w).type;
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
		if (clearings.plot[i] && map.isResource(i % t.w, i / t.w))
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
	writeUndermap(game.map, L.terrain);
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
	const auto lake = pureTiles(L.terrain, L.t, WATER);
	for (int i = 0; i < L.t.size(); ++i)
	{
		const int x = i % L.t.w, y = i / L.t.w;
		if (lake[i] && !game.map.isWater(x, y))
			return "Lava shield lost part of its crater or ocean.";
		if (L.rock[i] && (game.map.getResource(x, y).type != STONE || !game.map.isGrass(x, y)))
			return "Lava shield lost structural stone or placed it on illegal terrain.";
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
			4,
			false,
			{{"tongue-count", "Lava tongues", 3, 9, 1, 5, ControlGroup::Layout},
			 {"long-tongues", "Long tongues", 25, 75, 25, 50, ControlGroup::Layout},
			 {"branching", "Branching", 0, 3, 1, 2, ControlGroup::Layout},
			 {"rim-width", "Crater rim width", 6, 12, 2, 8, ControlGroup::Terrain},
			 GeneratorControl::toggle("islets", "Islets", true),
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
			validateWorld};
}
