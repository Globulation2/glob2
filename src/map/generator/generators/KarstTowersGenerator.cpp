// SPDX-License-Identifier: GPL-3.0-or-later
#include "KarstTowersGenerator.h"
#include "Channels.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Growth.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Patterns.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
using namespace MapGeneration;

// Karst towers: river valleys among limestone pinnacles, the way Guilin or Ha Long Bay look. Towers
// of stone crowd into thickets with sinkhole ponds between them, rivers wind along the open valleys,
// and the flat ground on their banks is divided into paddies, some sown and some flooded.
//
// Three shared primitives combine in a way no other map uses them:
//  - the towers are the spots of a Turing pattern (Patterns.h). A slow noise lowers the cut where the
//    karst is thick, so towers grow fatter and closer in thickets and stand alone on the valley floor,
//    but a spot never pools into its neighbour;
//  - each river is the cheapest walk round the torus (Roads.h) under a cost that climbs near a tower
//    (Morphology.h's distance field), so it bends where the towers make it bend. It only ever steps
//    forwards along its axis and stays inside a band between two rows of homes, so it cannot double
//    back on itself;
//  - the paddies are the bank's distance bands cut across by the river's own length (a flood from its
//    centreline), so their bunds follow the river's curves.
//
// Homes stand on a lattice in rows, and a river runs in the middle of every gap between rows, so
// every colony has a river at the same distance, and each river parts the rows it lies between: the
// fords, evenly spaced between the homes, are where the rows meet. On a map whose homes make a single
// row there is one river, opposite them, and contact also runs along the row.
//
// Every home is a bowl: a clearing with its pond and an irrigated paddy of its own, walled by a ring
// of towers broken by gates. The ring is one stencil, the same offset for offset round every home, in
// one of four gate designs drawn once per map (or chosen), so the bowls are exact translations. Only
// the ground beyond the bowls, the towers' Turing field, differs between colonies.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Stone never runs out and cannot
// be cleared, so the towers are permanent terrain and a bowl's gates are its only ways in: home ground
// is safe and rich in stone, with one paddy's worth of food. The river paddies are the surplus, and
// they lie on the fronts. Swimming turns a river from a wall into a way through the paddy plain.
// The towers are structural and not scaled by any resource amount; the home paddy and starter kit
// are guarantees and not scaled either.
namespace
{

// Every home's starting kit, unscaled: wheat and wood beside its pond. No quarry: the ring is stone.
constexpr int kHomeWheat = 14, kHomeWood = 12;
// The ring of towers round a home clearing: the margin inside it, its solid core, how far its lumpy
// outer edge reaches, and the ground beyond kept clear of rivers and paddies so the gates open on land.
constexpr double kClearingMargin = 2, kRingCore = 3, kRingOuter = 3, kBowlApron = 4;
// How far the ring's inner edge wobbles in and out, in tiles, so a bowl is not drawn with compasses.
constexpr double kRingWobble = 1.5;
// The smallest home the Home size control offers, which a gap between rows must fit a river beside.
constexpr int kSmallestHome = 12;
// Homes whose cross-axis positions lie this close are one row.
constexpr int kRowTolerance = 16;
// The widest a river's band may be either side of its middle, and a river waypoint's spacing.
constexpr int kWidestBand = 24, kWaypointPitch = 32;
// The paddies: terraces along the bank, alternately a ribbon of crops and a ribbon of flooded paddies
// this many tiles deep (crops grow best in rows about 10 tiles wide between rows of water; the bunds
// take a tile of each), cut across every this many tiles of river.
constexpr int kCropRibbon = 9, kWaterRibbon = 6, kPaddyReach = 24;
// The share (percent) of the water ribbon's paddies left dry, of the crop ribbon's flooded, and of the
// dry ones sown with wheat at an amount of 100.
constexpr int kFallowWater = 12, kFloodedCrop = 8, kPlantedPaddies = 60;
// Lake fields' labels start here, above every river paddy's.
constexpr int kLakeFieldLabel = 100000000;
// Sinkhole ponds: one at every trough of the tower field deep in a thicket, this big.
constexpr int kSinkholeTiles = 48;
// A sinking stream: pools of this many tiles every this many steps along the cheapest way from a
// sinkhole towards the nearest river, gaps of land between them, drying up before the paddies and
// given up beyond this many steps.
constexpr int kStreamPoolTiles = 14, kStreamPoolPitch = 9, kLongestStream = 120;
// No wood that can spread grows this close to a bowl.
constexpr double kWetWoodClearance = 16;

// The gates of each home design, as headings (turns of the facing) and a half-width in radians.
struct HomeDesign
{
	std::vector<double> gates;
	double halfWidth;
};
// Every other gate (the first, the third) has a lobed pool of about this radius in the apron beside it,
// off its walking line.
constexpr double kGatePool = 3.2, kGatePoolTurn = 0.4;
const HomeDesign kHomeDesigns[4] = {
	{{kPi / 2}, 0.50},                                               // Horseshoe: one wide mouth
	{{0, kPi}, 0.30},                                                // Twin gates, opposite
	{{kPi / 2, kPi / 2 + 2 * kPi / 3, kPi / 2 + 4 * kPi / 3}, 0.24}, // Three gates
	{{kPi / 4, 3 * kPi / 4, 5 * kPi / 4, 7 * kPi / 4}, 0.17}};       // Four narrow gates

struct Ford
{
	ShapePoint from, to, centre;
};

struct River
{
	int middle = 0, band = 0; // cross-axis middle and half-width of the band it winds in
	std::vector<int> centreline;
};

struct Layout
{
	Torus t{1, 1};
	double homeRadius = 0, bowlRadius = 0, facing = 0;
	int homeDesign = 0, paddies = 0;
	bool alongX = true;
	std::vector<ShapePoint> homes, kits;
	std::vector<int> homeOf;  // the home clearing a tile is in, or -1
	std::vector<int> paddyOf; // the paddy a pure-grass tile belongs to, or -1; home paddies last
	std::vector<unsigned char> bowl, riverWater, water, tower, paddyZone, flooded, homePaddy;
	std::vector<unsigned char> thicket;  // how thick the karst is, 0 to 255
	std::vector<unsigned char> gateOpen; // tiles each gate's opening must keep free of stone
	std::vector<River> rivers;
	std::vector<Ford> fords;
	std::vector<ShapePoint> lakes; // doline lakes between neighbouring homes in a row
	std::vector<unsigned char> lakeWater; // the lakes' own water
	std::vector<unsigned char> lakeField; // by paddy id: a sealed field round a lake, not a river paddy
	double lakeRadius = 0;
	TerrainSketch terrain;
	std::string failure;
};

int alongOf(const Layout &L, int i) { return L.alongX ? i % L.t.w : i / L.t.w; }
int crossOf(const Layout &L, int i) { return L.alongX ? i / L.t.w : i % L.t.w; }
int tileAt(const Layout &L, int along, int cross)
{
	return L.alongX ? L.t.at(along, cross) : L.t.at(cross, along);
}
int wrapped(int v, int period) { return ((v % period) + period) % period; }
// The signed distance from `from` to `to` the short way round a period.
int towards(int from, int to, int period)
{
	const int d = wrapped(to - from, period);
	return d > period / 2 ? d - period : d;
}

// Every tile within `reach` (a square) of a home's centre, with its offset.
template <typename Visit>
void aroundHome(const Torus &t, const ShapePoint &home, int reach, Visit visit)
{
	const int hx = int(home.x), hy = int(home.y);
	for (int dy = -reach; dy <= reach; ++dy)
		for (int dx = -reach; dx <= reach; ++dx)
			visit(t.at(hx + dx, hy + dy), dx, dy);
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const KarstTowersOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size();
	const int teams = std::max(1, request.nbTeams);

	// Homes on a lattice, dealt to the colonies. Rivers run along the longer side, or either way on a
	// square map, one in the middle of every gap between rows of homes.
	L.homes = latticeSites(t.w, t.h, teams, context.bounded("karst-layout", std::uint32_t(t.w)),
						   context.bounded("karst-layout", std::uint32_t(t.h)))
				  .sites;
	dealStarts(context, L.homes);
	// A river runs in every gap between rows of homes wide enough for two of the smallest bowls and the
	// river; rows too close for that share a valley with no river. Rivers run along the longer side,
	// or on a square map along whichever axis leaves more such gaps (either, drawn, on a tie).
	const double ring = kClearingMargin + kRingWobble + kRingCore + kRingOuter + kBowlApron;
	const double neededGap = 2 * (kSmallestHome + ring) + o.riverWidth + 6;
	const auto gapsAlong = [&](bool alongX)
	{
		const int across = alongX ? t.h : t.w;
		std::vector<int> crosses;
		for (const ShapePoint &home : L.homes)
			crosses.push_back(int(alongX ? home.y : home.x));
		std::sort(crosses.begin(), crosses.end());
		std::vector<std::vector<int>> rows;
		for (int c : crosses)
			if (rows.empty() || c - rows.back().back() > kRowTolerance)
				rows.push_back({c});
			else
				rows.back().push_back(c);
		if (rows.size() > 1 && crosses.front() + across - crosses.back() <= kRowTolerance)
		{
			for (int c : rows.front())
				rows.back().push_back(c + across);
			rows.erase(rows.begin());
		}
		std::vector<std::pair<int, int>> gaps; // (width, middle)
		for (size_t r = 0; r < rows.size(); ++r)
		{
			const int next = r + 1 < rows.size() ? rows[r + 1].front() : rows[0].front() + across;
			gaps.push_back({next - rows[r].back(), wrapped((rows[r].back() + next) / 2, across)});
		}
		return gaps;
	};
	const auto fitting = [&](const std::vector<std::pair<int, int>> &gaps)
	{ return std::count_if(gaps.begin(), gaps.end(), [&](const auto &g) { return g.first >= neededGap; }); };
	if (t.w != t.h)
		L.alongX = t.w > t.h;
	else
	{
		const auto alongWidth = fitting(gapsAlong(true)), alongHeight = fitting(gapsAlong(false));
		L.alongX = alongWidth != alongHeight ? alongWidth > alongHeight
											 : context.bounded("karst-river-axis", 2) == 0;
	}
	const int length = L.alongX ? t.w : t.h, across = L.alongX ? t.h : t.w;
	const auto homeCross = [&](const ShapePoint &home) { return int(L.alongX ? home.y : home.x); };
	const std::vector<std::pair<int, int>> gaps = gapsAlong(L.alongX);
	for (const auto &gap : gaps)
		if (gap.first >= neededGap)
			L.rivers.push_back({gap.second, 0, {}});
	// Four or more rivers read as stripes: with an even number, every other one is kept, which still
	// leaves every row of homes a river on one side.
	if (L.rivers.size() >= 4 && L.rivers.size() % 2 == 0)
	{
		const size_t parity = context.bounded("karst-river-parity", 2);
		std::vector<River> kept;
		for (size_t r = parity; r < L.rivers.size(); r += 2)
			kept.push_back(L.rivers[r]);
		L.rivers = kept;
	}
	// With no gap that wide (a crowded map, or a lattice whose rows are single homes), one river runs
	// free across the whole map, winding round the bowls, and fairness is only statistical.
	bool freeRiver = false;
	if (L.rivers.empty())
	{
		L.rivers.push_back({std::max_element(gaps.begin(), gaps.end())->second, 0, {}});
		freeRiver = true;
		context.telemetry.fallback("karst.river.free", "No gap between rows fits a river beside its bowls.");
	}
	// Each home's cross-axis distance to its nearest river decides how big the bowls can be.
	double nearestRiver = across;
	for (const ShapePoint &home : L.homes)
		for (const River &river : L.rivers)
			if (!freeRiver)
				nearestRiver =
				std::min<double>(nearestRiver, std::abs(towards(homeCross(home), river.middle, across)));
	const double spacing = nearestSiteDistance(t, L.homes);
	L.homeRadius = std::floor(std::min<double>(
		{double(o.homeSize), spacing / 2 - ring, nearestRiver - ring - o.riverWidth / 2.0 - 3}));
	context.telemetry.measure("karst.home.radius-fitted", L.homeRadius);
	context.telemetry.measure("karst.rivers", L.rivers.size());
	context.telemetry.choice("karst.river.axis", L.alongX ? "along-width" : "along-height");
	if (L.homeRadius < o.homeSize)
		context.telemetry.fallback("karst.home.shrunk", "Homes shrank to leave room for their rings.");
	if (!homeHasRoom(L.homeRadius))
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	L.bowlRadius = L.homeRadius + ring;
	for (River &river : L.rivers)
	{
		double room = across;
		for (const ShapePoint &home : L.homes)
			room = std::min<double>(room, std::abs(towards(homeCross(home), river.middle, across)));
		river.band = freeRiver ? across / 2
							   : std::clamp(int(room - L.bowlRadius - o.riverWidth / 2.0 - 2), 0, kWidestBand);
	}
	L.homeDesign =
		o.homeDesign > 0 ? o.homeDesign - 1 : int(context.bounded("karst-home-design", 4));
	// One facing per map, turned so the gates open across the rivers, towards the paddies, rather than
	// along the row: the Horseshoe's and Three gates' first gate points at the design's quarter turn,
	// the Twin gates' pair lies along the axis. Which of the two ways across is a draw.
	const double across0 = L.alongX ? 0 : kPi / 2; // turns a south-pointing gate to point across the rivers
	const double facingBase[4] = {across0, across0 + kPi / 2, across0, 0};
	L.facing = facingBase[L.homeDesign] + context.bounded("karst-home-facing", 2) * kPi;
	context.telemetry.choice("karst.home.design", std::to_string(L.homeDesign));

	// The towers: a Turing pattern's spots, fatter and closer where a slow noise says the karst is thick.
	TuringStyle style;
	style.wavelength = o.towerSpacing;
	const std::vector<int> field = turingPattern(t, style, context.stream("karst-pattern"));
	const std::vector<int> thicketNoise = fractalNoise(
		t.w, t.h, std::max(48, std::min(t.w, t.h) / 3), 2, context.stream("karst-thickets"));
	const int thicketFrom = 32000 - o.towerDensity * 120;
	const int sparse = percentile(field, 97);
	const int dense = percentile(field, 60);
	L.thicket.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.thicket[i] = std::uint8_t(std::clamp((thicketNoise[i] - thicketFrom) * 255 / 8000, 0, 255));
	const auto levelAt = [&](int i, double thin)
	{ return sparse + int(std::int64_t(dense - sparse) * L.thicket[i] / 255 * thin); };
	L.tower.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.tower[i] = field[i] >= levelAt(i, 1.0);

	// Every home is a bowl: a clearing, a ring of towers with its gates open, and an apron. The ring
	// comes from one stencil of noise read at each tile's offset from its home, so every ring is the same.
	const HomeDesign &shape = kHomeDesigns[L.homeDesign];
	const double inner = L.homeRadius + kClearingMargin;
	const double halfGate = std::max(shape.halfWidth, 3.5 / inner);
	const std::vector<int> stencil = periodicNoise(128, 128, 8, context.stream("karst-ring"));
	const auto stencilAt = [&](int dx, int dy, int shift)
	{ return stencil[wrapped(dy + shift, 128) * 128 + wrapped(dx + shift, 128)]; };
	const int reach = int(std::ceil(L.bowlRadius)) + 2;
	L.homeOf.assign(n, -1);
	L.bowl.assign(n, 0);
	L.gateOpen.assign(n, 0);
	for (const ShapePoint &home : L.homes)
		aroundHome(t, home, reach,
				   [&](int i, int dx, int dy)
				   {
					   const double d = std::hypot(dx, dy);
					   if (d > L.bowlRadius + 2)
						   return;
					   L.bowl[i] = 1;
					   const double heading = std::atan2(dy, dx);
					   const double edge =
						   inner + kRingWobble * (stencilAt(dx, dy, 0) - 32768) / 32768.0;
					   bool gate = false;
					   for (double g : shape.gates)
						   gate = gate ||
								  std::abs(std::remainder(heading - L.facing - g, 2 * kPi)) <= halfGate;
					   if (gate)
						   L.gateOpen[i] = d > edge + 1 && d <= edge + kRingCore;
					   if (d <= std::max(edge, L.homeRadius + 1) || gate)
						   L.tower[i] = 0;
					   else if (d <= edge + kRingCore)
						   L.tower[i] = 1;
					   else if (d <= edge + kRingCore + kRingOuter)
						   L.tower[i] = stencilAt(dx, dy, 64) >= 32768;
					   else
						   L.tower[i] = 0;
				   });
	L.tower = dropSmallRegions(t, L.tower, 3);

	// The rivers: each the cheapest walk round the torus inside its band, always forwards along the
	// axis, dear near a tower, meandering by noise, never through a bowl.
	const std::vector<std::int64_t> nearTower = distanceSquaredTo(t, L.tower);
	const std::vector<int> meander =
		periodicNoise(t.w, t.h, std::max(12, o.towerSpacing), context.stream("karst-meander"));
	const std::vector<unsigned char> riverless = dilate(t, L.bowl, freeRiver ? o.riverWidth / 2 + 2 : 0);
	std::vector<int> cost(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const std::int64_t d2 = nearTower[i] < 0 ? 10000 : nearTower[i];
		cost[i] = riverless[i] ? -1
							: 20 + meander[i] * 160 / 65536 +
								  int(std::max<std::int64_t>(0, 64 - d2) * 20);
	}
	std::vector<unsigned char> centre(n, 0);
	for (size_t r = 0; r < L.rivers.size(); ++r)
	{
		River &river = L.rivers[r];
		const std::string stream = "karst-river-" + std::to_string(r);
		const auto inBand = [&](int i)
		{ return std::abs(towards(river.middle, crossOf(L, i), across)) <= river.band; };
		const int segments = std::max(3, length / kWaypointPitch);
		const int start = int(context.bounded(stream, std::uint32_t(length)));
		std::vector<int> waypoints;
		for (int s = 0; s < segments; ++s)
		{
			const int along = (start + s * length / segments) % length;
			const int offset =
				int(context.bounded(stream, std::uint32_t(2 * river.band + 1))) - river.band;
			// A free river's next waypoint stays near its last, so a bowl cannot fall between the two.
			const int wanted = freeRiver && !waypoints.empty()
								   ? crossOf(L, waypoints.back()) + offset * 12 / std::max(1, river.band)
								   : river.middle + offset * 7 / 10;
			int best = -1;
			for (int step = 0; step <= 2 * river.band && best < 0; ++step)
				for (int side : {1, -1})
				{
					const int i = tileAt(L, along, wanted + side * step);
					if (best < 0 && inBand(i) && cost[i] >= 0)
						best = i;
				}
			if (best < 0)
			{
				L.failure =
					"The rivers found no way between the homes; use a bigger map or fewer colonies.";
				return L;
			}
			waypoints.push_back(best);
		}
		for (int s = 0; s < segments; ++s)
		{
			const int from = waypoints[s], to = waypoints[(s + 1) % segments];
			const int from0 = alongOf(L, from);
			const int span = wrapped(alongOf(L, to) - from0, length);
			std::vector<unsigned char> goal(n, 0);
			goal[to] = 1;
			std::vector<int> walk = cheapestWalk(
				t, GridNeighbors::Eight, {from}, goal,
				[&](int, int m, int dx, int dy)
				{
					const int forwards = L.alongX ? dx : dy;
					const int along = wrapped(alongOf(L, m) - from0, length);
					if ((forwards < 0 && !freeRiver) || along > span || cost[m] < 0 || !inBand(m))
						return -1;
					return cost[m] * (dx && dy ? 14 : 10) / 10;
				});
			if (walk.empty())
			{
				L.failure =
					"The rivers found no way between the homes; use a bigger map or fewer colonies.";
				return L;
			}
			std::reverse(walk.begin(), walk.end());
			river.centreline.insert(river.centreline.end(), walk.begin(), walk.end() - 1);
		}
		for (int i : river.centreline)
			centre[i] = 1;
		context.telemetry.measure("karst.river.band", river.band, int(r));
		context.telemetry.measure("karst.river.length", river.centreline.size(), int(r));
	}
	L.riverWater = dilateRound(t, centre, (o.riverWidth - 1) / 2.0);

	// Fords on every river, evenly spaced and half a step off the first home, so they fall between homes.
	const int firstAlong = int(L.alongX ? L.homes[0].x : L.homes[0].y);
	for (const River &river : L.rivers)
	{
		const int count = int(river.centreline.size());
		for (int f = 0; f < o.fords; ++f)
		{
			const int along = wrapped(firstAlong + int((f + 0.5) * length / o.fords), length);
			int first = -1, last = -1;
			for (int p = 0; p < count; ++p)
				if (alongOf(L, river.centreline[p]) == along)
				{
					if (first < 0)
						first = p;
					last = p;
				}
			if (first < 0)
				continue;
			const int p = (first + last) / 2;
			const int a = river.centreline[wrapped(p - 4, count)], b = river.centreline[(p + 4) % count];
			const double tx = t.offsetX(a % t.w, b % t.w), ty = t.offsetY(a / t.w, b / t.w);
			const double norm = std::max(1e-6, std::hypot(tx, ty));
			const double half = o.riverWidth / 2.0 + 3;
			const double cx = river.centreline[p] % t.w + 0.5, cy = river.centreline[p] / t.w + 0.5;
			L.fords.push_back({{cx + ty / norm * half, cy - tx / norm * half},
							   {cx - ty / norm * half, cy + tx / norm * half},
							   {cx, cy}});
		}
	}
	context.telemetry.measure("karst.fords", L.fords.size());

	L.water = L.riverWater;

	// Doline lakes: one halfway between every two neighbouring homes in a row, the same stencil-wobbled
	// round lake each time, so the open ground beside every bowl's gates has water and the lakes are
	// shared out exactly.
	{
		std::vector<std::vector<int>> rowHomes;
		std::vector<int> order(L.homes.size());
		for (size_t k = 0; k < order.size(); ++k)
			order[k] = int(k);
		std::sort(order.begin(), order.end(),
				  [&](int a, int b) { return homeCross(L.homes[a]) < homeCross(L.homes[b]); });
		for (int k : order)
			if (rowHomes.empty() ||
				homeCross(L.homes[k]) - homeCross(L.homes[rowHomes.back().back()]) > kRowTolerance)
				rowHomes.push_back({k});
			else
				rowHomes.back().push_back(k);
		if (rowHomes.size() > 1 && homeCross(L.homes[order.front()]) + across -
										   homeCross(L.homes[order.back()]) <=
									   kRowTolerance)
		{
			for (int k : rowHomes.front())
				rowHomes.back().push_back(k);
			rowHomes.erase(rowHomes.begin());
		}
		const auto homeAlong = [&](int k) { return int(L.alongX ? L.homes[k].x : L.homes[k].y); };
		int shortest = length;
		for (auto &row : rowHomes)
		{
			std::sort(row.begin(), row.end(), [&](int a, int b) { return homeAlong(a) < homeAlong(b); });
			for (size_t j = 0; j < row.size(); ++j)
				shortest = std::min(
					shortest, row.size() == 1 ? length
											  : wrapped(homeAlong(row[(j + 1) % row.size()]) - homeAlong(row[j]), length));
		}
		L.lakeRadius = std::min(9.0, (shortest - 2 * L.bowlRadius) / 2 - 5);
		if (L.lakeRadius >= 4)
			for (const auto &row : rowHomes)
				for (size_t j = 0; j < row.size(); ++j)
				{
					const int a = row[j], b = row[(j + 1) % row.size()];
					const int span = row.size() == 1 ? length : wrapped(homeAlong(b) - homeAlong(a), length);
					const int along = wrapped(homeAlong(a) + span / 2, length);
					const int cross = wrapped(
						homeCross(L.homes[a]) + towards(homeCross(L.homes[a]), homeCross(L.homes[b]), across) / 2,
						across);
					L.lakes.push_back(L.alongX ? ShapePoint{double(along), double(cross)}
											   : ShapePoint{double(cross), double(along)});
				}
		const std::vector<unsigned char> riverSide = dilate(t, L.riverWater, 8);
		L.lakeWater.assign(n, 0);
		std::vector<ShapePoint> kept;
		for (const ShapePoint &lake : L.lakes)
		{
			bool clear = true;
			std::vector<int> tiles;
			aroundHome(t, lake, int(L.lakeRadius * 1.35 * 1.2) + 2,
					   [&](int i, int dx, int dy)
					   {
						   // Lobed, not round: the rim is read from the stencil at the tile's heading,
						   // and the lake is drawn out along the row.
						   const double heading = std::atan2(dy, dx);
						   const double lobes =
							   stencilAt(int(std::lround(9 * std::cos(heading))),
										 int(std::lround(9 * std::sin(heading))), 32) /
							   65536.0;
						   const double stretch = L.alongX ? std::hypot(dx / 1.35, dy) : std::hypot(dx, dy / 1.35);
						   if (stretch <= L.lakeRadius * (0.55 + 0.8 * lobes) + 0.5)
						   {
							   tiles.push_back(i);
							   clear = clear && !riverSide[i] && !L.bowl[i];
						   }
					   });
			if (!clear)
				continue;
			kept.push_back(lake);
			for (int i : tiles)
				L.water[i] = L.lakeWater[i] = 1;
		}
		L.lakes = kept;
		context.telemetry.measure("karst.lakes", L.lakes.size());
		context.telemetry.measure("karst.lake.radius", L.lakeRadius);
	}

	// Sinkhole ponds at the troughs of the tower field beyond the valleys, in thickets and in open ground
	// alike (dolines).
	const std::vector<std::int64_t> fromRiver = distanceSquaredTo(t, L.riverWater);
	{
		const std::vector<int> lowest = windowMinimum(t, field, 3 * o.towerSpacing / 2);
		const double sinkholeFrom = o.riverWidth / 2.0 + o.paddyDepth + 10;
		const std::vector<unsigned char> keepDry = dilate(t, L.bowl, 4);
		std::vector<int> queued(n, 0), sinkholes;
		int ponds = 0;
		for (int i = 0; i < n; ++i)
			if (field[i] == lowest[i] && !keepDry[i] && !L.tower[i] &&
				fromRiver[i] > std::int64_t((sinkholeFrom) * (sinkholeFrom)))
			{
				growWater(
					t, L.water, i, kSinkholeTiles, [&](int j) { return !keepDry[j]; },
					[&](int j)
					{
						return std::int64_t(field[j]) +
							   t.dist2(i % t.w, i / t.w, j % t.w, j / t.w) * 400;
					},
					queued, ++ponds);
				sinkholes.push_back(i);
			}
		context.telemetry.measure("karst.sinkholes", ponds);

		// Sinking streams: from each sinkhole, a chain of pools along the cheapest way towards the
		// river, round the towers, sinking into the ground before it reaches the paddies. The gaps of
		// land between the pools keep the thickets walkable.
		const double valleyEdge = o.riverWidth / 2.0 + o.paddyDepth + 6;
		std::vector<unsigned char> reached = L.riverWater;
		int streams = 0, pools = 0;
		for (int from : sinkholes)
		{
			std::vector<int> walk = cheapestWalk(
				t, GridNeighbors::Eight, {from}, reached,
				[&](int, int m, int dx, int dy)
				{ return keepDry[m] || L.tower[m] ? -1 : (dx && dy ? 14 : 10); });
			if (walk.empty() || int(walk.size()) > kLongestStream)
				continue;
			++streams;
			std::reverse(walk.begin(), walk.end());
			for (size_t p = kStreamPoolPitch; p + 4 < walk.size(); p += kStreamPoolPitch)
			{
				const int at = walk[p];
				if (fromRiver[at] < std::int64_t(valleyEdge * valleyEdge))
					break;
				growWater(
					t, L.water, at, kStreamPoolTiles, [&](int j) { return !keepDry[j]; },
					[&](int j)
					{
						return std::int64_t(t.dist2(at % t.w, at / t.w, j % t.w, j / t.w)) * 100 +
							   field[j] / 64;
					},
					queued, ++ponds);
				++pools;
			}
			for (int i : walk)
				reached[i] = 1;
		}
		context.telemetry.measure("karst.streams", streams);
		context.telemetry.measure("karst.stream-pools", pools);
	}

	// No tower on a shore, nor at a ford's landings; the valley floor thins out towards a river, so the
	// paddies lie on open ground and the thickets stand back from the banks.
	std::vector<unsigned char> shore = dilate(t, L.water, 3);
	for (const Ford &f : L.fords)
		for (const ShapePoint &end : {f.from, f.to})
			for (int dy = -4; dy <= 4; ++dy)
				for (int dx = -4; dx <= 4; ++dx)
					shore[t.at(int(end.x) + dx, int(end.y) + dy)] = 1;
	const double valley = o.riverWidth / 2.0 + o.paddyDepth + 4;
	for (int i = 0; i < n; ++i)
	{
		if (L.bowl[i])
			continue;
		const double d = std::sqrt(double(fromRiver[i]));
		if (shore[i])
			L.tower[i] = 0;
		else if (d < valley)
			L.tower[i] = field[i] >= levelAt(i, d / valley) + int((valley - d) / valley * 8000);
	}
	L.tower = dropSmallRegions(t, L.tower, 3);

	// The paddies: bank bands cut across by the river's length, on open ground within reach of a
	// river and clear of towers, bowls, ponds and fords.
	const std::vector<std::int64_t> fromWater = distanceSquaredTo(t, L.water);
	const std::vector<std::int64_t> fromTower = distanceSquaredTo(t, L.tower);
	std::vector<int> nearestReach(n, -1);
	{
		std::vector<int> queue;
		int offset = 0;
		for (const River &river : L.rivers)
		{
			for (size_t p = 0; p < river.centreline.size(); ++p)
				if (nearestReach[river.centreline[p]] < 0)
				{
					nearestReach[river.centreline[p]] = offset + int(p);
					queue.push_back(river.centreline[p]);
				}
			offset += int(river.centreline.size()) + kPaddyReach;
		}
		for (size_t head = 0; head < queue.size(); ++head)
		{
			const int i = queue[head];
			for (const auto &step : kCardinalSteps)
			{
				const int m = t.at(i % t.w + step[0], i / t.w + step[1]);
				if (nearestReach[m] < 0)
				{
					nearestReach[m] = nearestReach[i];
					queue.push_back(m);
				}
			}
		}
	}
	std::vector<unsigned char> fordGround(n, 0);
	for (const Ford &f : L.fords)
		for (const ShapePoint &end : {f.from, f.to})
			for (int dy = -5; dy <= 5; ++dy)
				for (int dx = -5; dx <= 5; ++dx)
					fordGround[t.at(int(end.x) + dx, int(end.y) + dy)] = 1;
	const std::int64_t depth2 = std::int64_t(o.paddyDepth + 2) * (o.paddyDepth + 2);
	const std::vector<unsigned char> apron = dilate(t, L.bowl, 2);
	L.paddyZone.assign(n, 0);
	std::vector<int> label(n, -1);
	const std::vector<int> jitter = periodicNoise(t.w, t.h, 24, context.stream("karst-paddy-jitter"));
	for (int i = 0; i < n; ++i)
		L.paddyZone[i] = !apron[i] && !fordGround[i] && fromWater[i] >= 9 && fromRiver[i] <= depth2 &&
						 (fromTower[i] < 0 || fromTower[i] >= 9);
	L.paddyZone = dropSmallRegions(t, L.paddyZone, 30);
	for (int i = 0; i < n; ++i)
		if (L.paddyZone[i])
		{
			const double wobble = (jitter[i] - 32768) / 32768.0;
			const double depth = std::sqrt(double(fromRiver[i])) + wobble * 0.5;
			const int terrace = int(depth / (kCropRibbon + kWaterRibbon));
			const int ribbon =
				2 * terrace + (depth - terrace * (kCropRibbon + kWaterRibbon) >= kCropRibbon);
			label[i] = ribbon * 100000 + int(nearestReach[i] + wobble * 4) / kPaddyReach;
		}
	// Fields round the doline lakes, sealed like the paddies so their wheat cannot spread over the ground
	// between neighbouring homes: a ring from the lake's beach out to ten tiles, cut into six by its
	// headings, the same round every lake.
	{
		const std::vector<unsigned char> outer = dilateRound(t, L.lakeWater, 10);
		const std::vector<unsigned char> inner = dilateRound(t, L.lakeWater, 2);
		for (size_t k = 0; k < L.lakes.size(); ++k)
			aroundHome(t, L.lakes[k], int(L.lakeRadius * 1.35 * 1.4) + 12,
					   [&](int i, int dx, int dy)
					   {
						   if (!outer[i] || inner[i] || apron[i] || L.paddyZone[i])
							   return;
						   L.tower[i] = 0; // the fields are cleared of towers, so they stay whole
						   const int sector = int(std::floor((std::atan2(dy, dx) + kPi) / (kPi / 3))) % 6;
						   L.paddyZone[i] = 1;
						   label[i] = kLakeFieldLabel + int(k) * 10 + sector;
					   });
	}
	L.terrain.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
	{
		if (!L.paddyZone[i])
			continue;
		const int x = i % t.w, y = i / t.w;
		// A corner is a bund where its right or lower neighbour is another paddy, or any neighbour is
		// off the paddies: then no tile spanning two paddies is pure grass, and a crop cannot cross.
		const int right = t.at(x + 1, y), down = t.at(x, y + 1);
		if (!L.paddyZone[right] || !L.paddyZone[down] || !L.paddyZone[t.at(x - 1, y)] ||
			!L.paddyZone[t.at(x, y - 1)] || label[right] != label[i] || label[down] != label[i])
			L.terrain[i] = SAND;
	}
	std::map<int, int> paddyIds;
	L.paddyOf.assign(n, -1);
	const auto pureGrass = [&](int i)
	{
		const int x = i % t.w, y = i / t.w;
		return L.terrain[i] == GRASS && L.terrain[t.at(x + 1, y)] == GRASS &&
			   L.terrain[t.at(x, y + 1)] == GRASS && L.terrain[t.at(x + 1, y + 1)] == GRASS;
	};
	for (int i = 0; i < n; ++i)
		if (label[i] >= 0 && pureGrass(i))
			L.paddyOf[i] = paddyIds.emplace(label[i], int(paddyIds.size())).first->second;
	L.paddies = int(paddyIds.size());
	// The water ribbon's paddies are flooded, and a few of the crop ribbon's: water on every corner inside
	// the bunds, so the bund is the paddy's only sand (pure sand beside a crop stops it regrowing) and
	// stays walkable. A paddy too small to hold four such corners stays dry.
	std::vector<int> ribbonOf(paddyIds.size(), 0);
	L.lakeField.assign(paddyIds.size(), 0);
	for (const auto &[key, id] : paddyIds)
	{
		ribbonOf[id] = key / 100000;
		L.lakeField[id] = key >= kLakeFieldLabel;
	}
	std::vector<int> pool;
	std::vector<int> poolSize(L.paddies, 0);
	for (int i = 0; i < n; ++i)
		if (label[i] >= 0 && L.terrain[i] == GRASS)
			if (const auto id = paddyIds.find(label[i]); id != paddyIds.end())
			{
				pool.push_back(i);
				++poolSize[id->second];
			}
	L.flooded.assign(L.paddies, 0);
	int flooded = 0;
	for (int p = 0; p < L.paddies; ++p)
	{
		const int draw = int(context.bounded("karst-flooded", 100));
		L.flooded[p] = !L.lakeField[p] && poolSize[p] >= 4 &&
					   (ribbonOf[p] % 2 ? draw >= kFallowWater : draw < kFloodedCrop);
		flooded += L.flooded[p];
	}
	for (int i : pool)
		if (L.flooded[paddyIds.at(label[i])])
			L.water[i] = 1;
	context.telemetry.measure("karst.paddies", L.paddies);
	context.telemetry.measure("karst.paddies.flooded", flooded);

	// Every home's own paddy, north of its middle: a block of corners bunded round with an irrigation
	// channel two tiles wide along its far side, the same offsets round every home.
	L.homePaddy.assign(n, 0);
	std::vector<int> homeChannel;
	const auto inBlock = [&](int dx, int dy)
	{ return dy >= -13 && dy <= -3 && std::abs(dx) <= 7 && std::hypot(dx, dy) <= L.homeRadius - 0.5; };
	for (const ShapePoint &home : L.homes)
		aroundHome(t, home, 14,
				   [&](int i, int dx, int dy)
				   {
					   if (!inBlock(dx, dy))
						   return;
					   const bool edge = !inBlock(dx + 1, dy) || !inBlock(dx - 1, dy) ||
										 !inBlock(dx, dy + 1) || !inBlock(dx, dy - 1);
					   L.terrain[i] = edge ? SAND : GRASS;
					   if (!edge && dy >= -12 && dy <= -10 && std::abs(dx) <= 5)
						   homeChannel.push_back(i);
					   L.homePaddy[i] = 1;
				   });
	// A pool beside every other gate, just inside the apron, the same for every home.
	const std::vector<double> &gates = kHomeDesigns[L.homeDesign].gates;
	for (const ShapePoint &home : L.homes)
		for (size_t gi = 0; gi < gates.size(); gi += 2)
		{
			const double heading = L.facing + gates[gi] + halfGate + kGatePoolTurn;
			const double reachOut = L.bowlRadius - kGatePool;
			const double px = reachOut * std::cos(heading), py = reachOut * std::sin(heading);
			aroundHome(t, home, int(std::ceil(L.bowlRadius)) + 2,
					   [&](int i, int dx, int dy)
					   {
						   const double lobes = stencilAt(dx, dy, 48) / 65536.0;
						   if (std::hypot(dx - px, dy - py) <= kGatePool * (0.65 + 0.7 * lobes))
						   {
							   homeChannel.push_back(i);
							   L.tower[i] = 0;
						   }
					   });
		}
	for (int i : homeChannel)
		L.water[i] = 1;

	// Terrain: the water, the home ponds and beaches, then the fords laid across the rivers.
	// The bowl's main water: a doline pool bigger than a round home's usual pond, against the clearing's
	// rim east-south-east of the middle, so it leaves the middle free for building and its beach keeps
	// off the home paddy. The starter kit is planted round it.
	const double pondRadius = std::min(4.2, 0.28 * L.homeRadius);
	const RadialShape pond(pondRadius, 0.2, context, "karst-pond");
	std::vector<unsigned char> ponds(n, 0);
	for (const ShapePoint &home : L.homes)
	{
		const double out = std::floor(L.homeRadius - pondRadius - 1.5);
		const ShapePoint centre{std::floor(home.x) + std::round(out * 0.94) + 0.5,
							std::floor(home.y) + std::round(out * 0.34) + 0.5};
		fillShape(ponds, t, centre.x, centre.y, pond);
		L.kits.push_back(centre);
	}
	for (size_t k = 0; k < L.homes.size(); ++k)
		aroundHome(t, L.homes[k], int(L.homeRadius) + 1,
			   [&](int i, int dx, int dy)
			   {
				   if (std::hypot(dx, dy) <= L.homeRadius)
					   L.homeOf[i] = int(k);
				   });
	for (int i = 0; i < n; ++i)
		if (L.water[i] || ponds[i])
			L.terrain[i] = WATER;
	layBeaches(L.terrain, t);
	for (const Ford &f : L.fords)
		bridgeAcross(L.terrain, t, f.from, f.to, 1.5);
	// The home paddies' tiles join the paddies, one id per home, for the containment check.
	for (size_t k = 0; k < L.homes.size(); ++k)
		aroundHome(t, L.homes[k], 14,
				   [&](int i, int, int)
				   {
					   if (L.homePaddy[i] && pureGrass(i))
						   L.paddyOf[i] = L.paddies + int(k);
				   });
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "karst layout";
	const KarstTowersOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "karst terrain";
	writeUndermap(map, L.terrain);
	int towers = 0;
	for (int i = 0; i < n; ++i)
		if (L.tower[i] && map.isResourceAllowed(i % t.w, i / t.w, STONE))
		{
			map.setResource(i % t.w, i / t.w, STONE, 1);
			++towers;
		}
	context.telemetry.measure("karst.tower.tiles", towers);

	context.stage = "karst colonies";
	if (!settleRoundColonies(game, context, "karst-starts", L.homeOf, L.homes, L.homeRadius))
		return false;

	context.stage = "karst resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const auto open = [&](int i) { return !reserved[i] && clearGround(map, i % t.w, i / t.w); };
	for (int k = 0; k < teams; ++k)
		plantHomeKit(map, t, context, L.kits[k], 0.0, L.homeRadius, kHomeWheat, kHomeWood,
					 [&](int i) { return L.homeOf[i] == k && !L.homePaddy[i] && open(i); });
	// Home paddies are sown whole, whatever the amounts: they are each colony's guaranteed food.
	for (int i = 0; i < n; ++i)
		if (L.paddyOf[i] >= L.paddies && open(i) && map.isResourceAllowed(i % t.w, i / t.w, WHEAT))
			map.setResource(i % t.w, i / t.w, WHEAT, 1);

	// River paddies: a share of the dry ones sown whole with wheat.
	const int planted = int(std::min(100.0, scaledShare(kPlantedPaddies, o.wheat)));
	int sownPaddies = 0;
	std::vector<unsigned char> sown(L.paddies, 0);
	for (int p = 0; p < L.paddies; ++p)
	{
		sown[p] = !L.flooded[p] && !L.lakeField[p] && int(context.bounded("karst-sown", 100)) < planted;
		sownPaddies += sown[p];
	}
	context.telemetry.measure("karst.paddies.sown", sownPaddies);
	for (int i = 0; i < n; ++i)
		if (L.paddyOf[i] >= 0 && L.paddyOf[i] < L.paddies && sown[L.paddyOf[i]] && open(i) &&
			map.isResourceAllowed(i % t.w, i / t.w, WHEAT))
			map.setResource(i % t.w, i / t.w, WHEAT, 1);

	// Woods in the thickets and in broken patches at the towers' feet. Wood that water could spread
	// stays well away from the bowls.
	const Fertility::Field growth = cropGrowthField(L.terrain, t);
	const std::vector<std::int64_t> fromTower = distanceSquaredTo(t, L.tower);
	const std::vector<std::int64_t> fromBowl = distanceSquaredTo(t, L.bowl);
	const std::vector<std::int64_t> fromLake = distanceSquaredTo(t, L.lakeWater);
	const std::vector<int> woods = periodicNoise(t.w, t.h, 14, context.stream("karst-woods"));
	const double woodShare = scaledShare(0.4, o.wood);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (fromTower[i] <= 0 || L.bowl[i] || L.paddyZone[i] || !open(i) ||
			!map.isResourceAllowed(x, y, WOOD))
			continue;
		if (growth.at(x, y) > 0 && (fromBowl[i] < kWetWoodClearance * kWetWoodClearance ||
									(fromLake[i] >= 0 && fromLake[i] < kWetWoodClearance * kWetWoodClearance)))
			continue;
		const double chance =
			fromTower[i] <= 5 ? woodShare * 0.6 : woodShare * L.thicket[i] / 255.0;
		if (woods[i] < chance * 65536)
			map.setResource(x, y, WOOD, 1);
	}

	// Fields round the doline lakes: the same patches of wheat round every lake, by offset, inside their
	// bunds.
	const double lakeShare = scaledShare(0.75, o.wheat);
	for (const ShapePoint &lake : L.lakes)
		aroundHome(t, lake, int(L.lakeRadius * 1.9) + 11,
				   [&](int i, int dx, int dy)
				   {
					   const double d = std::hypot(dx, dy);
					   // Patches of three tiles square, the same round every lake.
					   const unsigned hash =
						   unsigned(((dx + 99) / 3) * 73856093) ^ unsigned(((dy + 99) / 3) * 19349663);
					   if (d <= L.lakeRadius * 1.9 + 11 && (hash % 1000) < lakeShare * 1000 &&
						   L.paddyOf[i] >= 0 && L.paddyOf[i] < L.paddies && L.lakeField[L.paddyOf[i]] && open(i) &&
						   map.isResourceAllowed(i % t.w, i / t.w, WHEAT))
						   map.setResource(i % t.w, i / t.w, WHEAT, 1);
				   });

	// Orchards on the valley floor, off the paddies.
	const int groves = int(scaledCount(n / 2500, o.fruit));
	for (int g = 0; g < groves; ++g)
	{
		const int i = int(context.bounded("karst-groves", std::uint32_t(n)));
		growPatch(map, t, i, CHERRY + g % 3, 10,
				  [&](int j) { return !L.bowl[j] && !L.paddyZone[j] && !L.tower[j] && open(j); });
	}
	seedAlgae(map, context, t, "karst-algae", o.algae, AlgaeBand::shallows(1, 4, 70));
	secureStartingCrops(game, context, t, 24, 32, 0, &L.tower);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, 100, o.algae, o.fruit}, 24, 32, 0, &L.tower);

	context.stage = "karst routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, 40, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "karst towers"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	if (const std::string lost =
			homePondMissing(map, t, L.kits, context.request.nbTeams, "bowl", "pond");
		!lost.empty())
		return lost;
	const auto at = [&](int x, int y) { return "(" + std::to_string(x) + ", " + std::to_string(y) + ")"; };
	// Paddies are sealed: no pure-grass tile of a paddy touches pure grass that is not the same paddy.
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (L.paddyOf[i] < 0 || !map.isGrass(x, y))
			continue;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int m = t.at(x + dx, y + dy);
				if (map.isGrass(m % t.w, m / t.w) && L.paddyOf[m] != L.paddyOf[i])
					return "A paddy's bund is broken at " + at(x, y) + ": its crops could spread out.";
			}
	}
	// Every gate stays free of stone and every ford stays dry.
	for (int i = 0; i < t.size(); ++i)
		if (L.gateOpen[i] && map.isResource(i % t.w, i / t.w) &&
			map.getResource(i % t.w, i / t.w).type == STONE)
			return "A home's gate is closed by stone at " + at(i % t.w, i / t.w) + ".";
	for (const Ford &f : L.fords)
		if (map.isWater(int(f.centre.x), int(f.centre.y)))
			return "A ford is under water at " + at(int(f.centre.x), int(f.centre.y)) + ".";
	return walkFromFirstColony(map, context.request.nbTeams, "the karst", "through the towers and fords")
		.error;
}
} // namespace

KarstTowersOptions::KarstTowersOptions(const GenerationRequest &r)
	: towerSpacing(r.option("tower-spacing")), towerDensity(r.option("tower-density")),
	  homeDesign(r.option("home-design")), riverWidth(r.option("river-width")),
	  fords(r.option("fords")), paddyDepth(r.option("paddy-depth")), homeSize(r.option("home-size")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition karstTowersDefinition()
{
	return {"karst-towers",
			54,
			"Karst towers",
			2,
			false,
			{{"tower-spacing", "Tower spacing", 14, 32, 2, 16, ControlGroup::Terrain},
			 {"tower-density", "Tower density", 0, 100, 10, 50, ControlGroup::Terrain},
			 {"river-width", "River width", 4, 16, 1, 7, ControlGroup::Terrain},
			 {"fords", "Fords", 1, 6, 1, 2, ControlGroup::Terrain},
			 {"paddy-depth", "Paddy depth", 8, 40, 2, 24, ControlGroup::Terrain},
			 GeneratorControl::choice("home-design", "Home design",
									  {"Random", "Horseshoe", "Twin gates", "Three gates", "Four gates"},
									  0, ControlGroup::Layout),
			 {"home-size", "Home size", kSmallestHome, 22, 1, 15, ControlGroup::Layout},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld};
}
