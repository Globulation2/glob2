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
#include <climits>
#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Karst towers: river valleys among limestone pinnacles, the way Guilin or Ha Long Bay look. Towers
// of stone crowd into thickets with sinkhole ponds between them, rivers wind along the open valleys,
// and the flat ground on their banks is terraced into paddies, strips of wheat between flooded strips.
//
// Three shared primitives combine in a way no other map uses them:
//  - the towers are the spots of a Turing pattern (Patterns.h). A slow noise lowers the cut where the
//    karst is thick, so towers grow fatter and closer in thickets and stand alone on the valley floor,
//    while staying spots rather than one mass;
//  - each river is the cheapest walk round the torus (Roads.h) under a cost that climbs near a tower
//    (Morphology.h's distance field), so it bends where the towers make it bend. It only ever steps
//    forwards along its axis and stays inside a band between two rows of homes, so it cannot double
//    back on itself;
//  - the paddies are the bank's distance bands cut across by the river's own length (a flood from its
//    centreline), so their bunds follow the river's curves.
//
// Homes stand on a lattice in rows, and a river runs in the middle of every gap between rows that has
// room for one, so every colony has a river at the same distance and each river parts the rows it
// lies between: the fords, evenly spaced along each river, are where the rows meet. On a map whose
// homes make a single row there is one river, opposite them, and contact also runs along the row. On a
// map too crowded for any banded river, one river runs free round the bowls.
//
// Every home is a bowl: a clearing with an irrigated paddy, a doline pond on its rim and a pool beside
// one or two of its gates, walled by a ring of towers broken by gates. The ring, pools and pond are
// drawn at the same offsets round every home, the ring's lumps read from one noise stencil, in one of
// four gate designs drawn once per map (or chosen) and one facing per map, so every bowl is drawn the
// same. Between neighbouring homes in a row lies a doline lake in a ring of sealed fields, where
// there is room for one; beyond the valleys, sinkholes drain into chains of pools.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Stone never runs out and cannot
// be cleared, so the towers are permanent terrain and a bowl's gates are its only ways in. Crops
// regrow by the density of pure water near them, so the design puts water beside every field it
// plants: the home paddy's channel, the flooded terraces, the lakes. The river terraces are the
// surplus, and they lie on the fronts; swimming turns a river from a wall into a way through them.
// The towers are structural and not scaled by any resource amount; the home paddy and starter kit
// are guarantees and not scaled either. docs/map-generators/KARST_TOWERS.md has the measurements.
namespace
{

// Every home's starting kit, unscaled: wheat and wood beside its pond. No quarry: the ring is stone.
constexpr int kHomeWheat = 14, kHomeWood = 12;
// The ring of towers round a home clearing: the margin inside it, its solid core, how far its lumpy
// outer edge reaches, and the ground beyond kept clear of rivers and paddies so the gates open on land.
constexpr double kClearingMargin = 2, kRingCore = 3, kRingOuter = 3, kBowlApron = 4;
// How far the ring's inner edge wobbles in and out, in tiles, so a bowl is not drawn with compasses.
constexpr double kRingWobble = 1.5;
// Everything a bowl adds round its clearing.
constexpr double kBowlRing = kClearingMargin + kRingWobble + kRingCore + kRingOuter + kBowlApron;
// The smallest home the Home size control offers, which a gap between rows must fit a river beside.
constexpr int kSmallestHome = 12;
// The narrowest river the River width control offers, which a crowded map narrows its rivers towards.
constexpr int kNarrowestRiver = 4;
// Homes whose cross-axis positions lie this close are one row.
constexpr int kRowTolerance = 16;
// The widest a river's band may be either side of its middle, and a river waypoint's spacing.
constexpr int kWidestBand = 24, kWaypointPitch = 32;
// The paddies: terraces along the bank, alternately a ribbon of crops and a ribbon of flooded paddies
// this many tiles deep (crops grow best in rows about 10 tiles wide between rows of water; the bunds
// take a tile of each), cut across every this many tiles of river.
constexpr int kCropRibbon = 9, kWaterRibbon = 6, kPaddyReach = 24;
// The Flooded terraces control's default: the percentage of the water ribbon's paddies flooded.
constexpr int kDefaultFlooded = 88;
// The percentage of the crop ribbon's paddies flooded at that default (it scales with the control), and
// of the dry river paddies sown with wheat at a Wheat amount of 100.
constexpr int kFloodedCrop = 8, kPlantedPaddies = 60;
// Lake fields' labels start here, above every river paddy's.
constexpr int kLakeFieldLabel = 100000000;
// A sinkhole pond's tiles at a Sinkholes setting of 100.
constexpr int kSinkholeTiles = 48;
// A sinking stream: pools of this many tiles every this many steps along the cheapest way from a
// sinkhole to a river or an earlier stream, gaps of land between them, drying up before the paddies,
// and no stream at all when that way is longer than this many tiles.
constexpr int kStreamPoolTiles = 14, kStreamPoolPitch = 9, kLongestStream = 120;
// No wood is planted this close to a bowl or a lake on ground that water makes fertile, where it would
// spread.
constexpr double kWetWoodClearance = 16;
// The first gate, and the third where a design has one, each have a lobed pool of about this radius
// in the apron beside them, off their walking line, turned this far (radians) past the gate's edge.
constexpr double kGatePool = 3.2, kGatePoolTurn = 0.4;

// The gates of each home design, as headings in radians from the map's facing, and each gate's
// half-width in radians (widened where that is narrower than about seven tiles at the ring).
struct HomeDesign
{
	std::vector<double> gates;
	double halfWidth;
};
const HomeDesign kHomeDesigns[4] = {
	{{kPi / 2}, 0.50},                                               // Horseshoe: one wide mouth
	{{0, kPi}, 0.30},                                                // Twin gates, opposite
	{{kPi / 2, kPi / 2 + 2 * kPi / 3, kPi / 2 + 4 * kPi / 3}, 0.24}, // Three gates
	{{kPi / 4, 3 * kPi / 4, 5 * kPi / 4, 7 * kPi / 4}, 0.17}};       // Four narrow gates

const char *const kNoRiverWay =
	"The rivers found no way between the homes; use a bigger map or fewer colonies.";

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
	double homeRadius = 0, bowlRadius = 0, facing = 0, lakeRadius = 0;
	int homeDesign = 0, paddies = 0;
	bool alongX = true;
	std::vector<ShapePoint> homes, kits, lakes;
	std::vector<int> homeOf;  // the home clearing a tile is in, or -1
	std::vector<int> paddyOf; // the paddy a pure-grass tile belongs to, or -1; home paddies last
	std::vector<unsigned char> bowl, riverWater, lakeWater, water, tower, paddyZone, homePaddy;
	std::vector<unsigned char> thicket;   // how thick the karst is, 0 to 255
	std::vector<unsigned char> gateOpen;  // tiles each gate's opening must keep free of stone
	std::vector<unsigned char> flooded;   // by paddy id
	std::vector<unsigned char> lakeField; // by paddy id: a sealed field round a lake, not a river paddy
	std::vector<River> rivers;
	std::vector<Ford> fords;
	TerrainSketch terrain;
	std::string failure;
};

int wrapped(int v, int period) { return ((v % period) + period) % period; }

// What the design's stages share beyond the layout itself.
struct Work
{
	const KarstTowersOptions &o;
	GenerationContext &context;
	int riverWidth = 0, length = 0, across = 0; // the rivers' width; the map along and across them
	bool freeRiver = false;                     // one river running free round the bowls
	std::vector<int> field;             // the towers' Turing pattern
	int sparse = 0, dense = 0;          // its cut for a lone tower and for the thickest karst
	std::vector<int> stencil;           // 128x128 noise read at a tile's offset from its home or lake
	double halfGate = 0;                // the gates' half-width in radians, as widened for the ring
	std::vector<std::int64_t> fromRiver; // squared distance to river water

	// The cut a tower's spot must reach at tile `i`: the lone-tower cut moved towards the thicket's by
	// the karst's thickness there, times `share`.
	int levelAt(const Layout &L, int i, double share) const
	{
		return sparse + int(std::int64_t(dense - sparse) * L.thicket[i] / 255 * share);
	}
	// The stencil at an offset, shifted `shift` tiles on both axes so one stencil gives several fields.
	int stencilAt(int dx, int dy, int shift) const
	{
		return stencil[wrapped(dy + shift, 128) * 128 + wrapped(dx + shift, 128)];
	}
};
// The signed distance from `from` to `to` the short way round a period.
int towards(int from, int to, int period)
{
	const int d = wrapped(to - from, period);
	return d > period / 2 ? d - period : d;
}
int alongOf(const Layout &L, int i) { return L.alongX ? i % L.t.w : i / L.t.w; }
int crossOf(const Layout &L, int i) { return L.alongX ? i / L.t.w : i % L.t.w; }
int alongOf(const Layout &L, const ShapePoint &p) { return int(L.alongX ? p.x : p.y); }
int crossOf(const Layout &L, const ShapePoint &p) { return int(L.alongX ? p.y : p.x); }
int tileAt(const Layout &L, int along, int cross)
{
	return L.alongX ? L.t.at(along, cross) : L.t.at(cross, along);
}

// Whether all four undermap corners of tile `i` hold `type`: what the game will draw as a pure tile.
bool pureTile(const TerrainSketch &terrain, const Torus &t, int i, TerrainType type)
{
	const int x = i % t.w, y = i / t.w;
	return terrain[i] == type && terrain[t.at(x + 1, y)] == type && terrain[t.at(x, y + 1)] == type &&
		   terrain[t.at(x + 1, y + 1)] == type;
}

// Every tile within `reach` (a square) of a point, with its offset from the point's tile.
template <typename Visit>
void aroundPoint(const Torus &t, const ShapePoint &point, int reach, Visit visit)
{
	const int px = int(point.x), py = int(point.y);
	for (int dy = -reach; dy <= reach; ++dy)
		for (int dx = -reach; dx <= reach; ++dx)
			visit(t.at(px + dx, py + dy), dx, dy);
}

// Roads.h's cheapestWalk, eight-connected from one tile, for the many short walks the rivers and the
// sinking streams take. It is the same search, so it finds the same walk, but it keeps its arrays from
// one walk to the next rather than filling a fresh pair the size of the map for every walk, and it can
// give up once the cheapest tile left to try costs more than a walk worth having.
class Walker
{
  public:
	explicit Walker(const Torus &t) : t(t), cost(t.size(), INT_MAX), from(t.size(), -1) {}

	// The walk from `source` to the first tile `goal` accepts, back from that tile to the source, or
	// nothing when none is reached for at most `giveUp`.
	template <typename Goal, typename StepCost>
	std::vector<int> walk(int source, Goal goal, StepCost stepCost, int giveUp = INT_MAX)
	{
		using Entry = std::pair<int, int>;
		const std::greater<Entry> later;
		for (int i : touched)
		{
			cost[i] = INT_MAX;
			from[i] = -1;
		}
		touched.assign(1, source);
		cost[source] = 0;
		// A heap kept with the same calls std::priority_queue makes, so ties pop in the same order.
		heap.assign(1, {0, source});
		int reached = -1;
		while (!heap.empty())
		{
			std::pop_heap(heap.begin(), heap.end(), later);
			const auto [c, i] = heap.back();
			heap.pop_back();
			if (c > giveUp)
				break;
			if (c > cost[i])
				continue;
			if (goal(i))
			{
				reached = i;
				break;
			}
			const int x = i % t.w, y = i / t.w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (!dx && !dy)
						continue;
					const int m = t.at(x + dx, y + dy);
					const int step = stepCost(i, m, dx, dy);
					if (step < 0 || c + step >= cost[m])
						continue;
					if (cost[m] == INT_MAX)
						touched.push_back(m);
					cost[m] = c + step;
					from[m] = i;
					heap.push_back({cost[m], m});
					std::push_heap(heap.begin(), heap.end(), later);
				}
		}
		std::vector<int> route;
		for (int i = reached; i >= 0; i = from[i])
			route.push_back(i);
		return route;
	}

  private:
	const Torus &t;
	std::vector<int> cost, from, touched;
	std::vector<std::pair<int, int>> heap;
};

// The homes in rows across an axis: each row the homes' indices and cross-axis positions, sorted by
// position, a row wrapping round the torus kept whole (its wrapped positions carried past `across`).
using Row = std::vector<std::pair<int, int>>;
std::vector<Row> homeRows(const std::vector<ShapePoint> &homes, bool alongX, int across)
{
	Row sorted;
	for (size_t k = 0; k < homes.size(); ++k)
		sorted.push_back({int(k), int(alongX ? homes[k].y : homes[k].x)});
	std::stable_sort(sorted.begin(), sorted.end(),
					 [](const auto &a, const auto &b) { return a.second < b.second; });
	std::vector<Row> rows;
	for (const auto &home : sorted)
		if (rows.empty() || home.second - rows.back().back().second > kRowTolerance)
			rows.push_back({home});
		else
			rows.back().push_back(home);
	if (rows.size() > 1 && sorted.front().second + across - sorted.back().second <= kRowTolerance)
	{
		for (const auto &home : rows.front())
			rows.back().push_back({home.first, home.second + across});
		rows.erase(rows.begin());
	}
	return rows;
}

// The gaps between rows of homes across an axis, as (width, cross-axis middle).
std::vector<std::pair<int, int>> rowGaps(const std::vector<ShapePoint> &homes, bool alongX, int across)
{
	const std::vector<Row> rows = homeRows(homes, alongX, across);
	std::vector<std::pair<int, int>> gaps;
	for (size_t r = 0; r < rows.size(); ++r)
	{
		const int next = r + 1 < rows.size() ? rows[r + 1].front().second : rows[0].front().second + across;
		gaps.push_back({next - rows[r].back().second, wrapped((rows[r].back().second + next) / 2, across)});
	}
	return gaps;
}

// Homes on a lattice, dealt to the colonies, and the rivers between their rows: one in the middle of every
// gap wide enough for two of the smallest bowls and the river, running along the longer side or, on a
// square map, along whichever axis leaves more such gaps. The homes are sized to leave room for their
// rings, aprons and the rivers, and the gate design and facing are drawn. False, with `L.failure`, when
// no home has room.
bool placeHomesAndRivers(Layout &L, Work &w, int homeCap)
{
	const Torus &t = L.t;
	const KarstTowersOptions &o = w.o;
	GenerationContext &context = w.context;
	L.homes = latticeSites(t.w, t.h, int(std::max(1, context.request.nbTeams)),
						   context.bounded("karst-layout", std::uint32_t(t.w)),
						   context.bounded("karst-layout", std::uint32_t(t.h)))
				  .sites;
	dealStarts(context, L.homes);
	const double neededGap = 2 * (kSmallestHome + kBowlRing) + w.riverWidth + 6;
	const auto fitting = [&](const std::vector<std::pair<int, int>> &gaps)
	{ return std::count_if(gaps.begin(), gaps.end(), [&](const auto &g) { return g.first >= neededGap; }); };
	if (t.w != t.h)
		L.alongX = t.w > t.h;
	else
	{
		const auto alongWidth = fitting(rowGaps(L.homes, true, t.h));
		const auto alongHeight = fitting(rowGaps(L.homes, false, t.w));
		L.alongX = alongWidth != alongHeight ? alongWidth > alongHeight
											 : context.bounded("karst-river-axis", 2) == 0;
	}
	w.length = L.alongX ? t.w : t.h;
	w.across = L.alongX ? t.h : t.w;
	const std::vector<std::pair<int, int>> gaps = rowGaps(L.homes, L.alongX, w.across);
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
	if (L.rivers.empty())
	{
		L.rivers.push_back({std::max_element(gaps.begin(), gaps.end())->second, 0, {}});
		w.freeRiver = true;
		context.telemetry.fallback("karst.river.free", "No gap between rows fits a river beside its bowls.");
	}
	// How far across the axis a river's middle lies from the nearest home.
	const auto riverRoom = [&](const River &river)
	{
		double room = w.across;
		for (const ShapePoint &home : L.homes)
			room = std::min<double>(room, std::abs(towards(crossOf(L, home), river.middle, w.across)));
		return room;
	};
	double nearestRiver = w.across;
	if (!w.freeRiver)
		for (const River &river : L.rivers)
			nearestRiver = std::min(nearestRiver, riverRoom(river));
	L.homeRadius = std::floor(std::min<double>({double(std::min(o.homeSize, homeCap)),
												nearestSiteDistance(t, L.homes) / 2 - kBowlRing,
												nearestRiver - kBowlRing - w.riverWidth / 2.0 - 3}));
	context.telemetry.measure("karst.home.radius-fitted", L.homeRadius);
	context.telemetry.measure("karst.rivers", L.rivers.size());
	context.telemetry.choice("karst.river.axis", L.alongX ? "along-width" : "along-height");
	if (L.homeRadius < o.homeSize)
		context.telemetry.fallback("karst.home.shrunk", "Homes shrank to leave room for their rings.");
	if (!homeHasRoom(L.homeRadius))
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return false;
	}
	L.bowlRadius = L.homeRadius + kBowlRing;
	for (River &river : L.rivers)
		river.band = w.freeRiver ? w.across / 2
								 : std::clamp(int(riverRoom(river) - L.bowlRadius - w.riverWidth / 2.0 - 2), 0,
											  kWidestBand);
	L.homeDesign = o.homeDesign > 0 ? o.homeDesign - 1 : int(context.bounded("karst-home-design", 4));
	// One facing per map, turned so the gates open across the rivers, towards the paddies, rather than
	// along the row. A gate at a quarter turn points south, across rivers that run along the width, and
	// `acrossRivers` turns it across rivers that run along the height. The Horseshoe's and Three gates'
	// first gate is at a quarter turn; the Twin gates' pair lies east and west, so it takes a further
	// quarter; the Four gates' diagonals need no turn. Which of the two ways across is a draw.
	const double acrossRivers = L.alongX ? 0 : kPi / 2;
	const double facingBase[4] = {acrossRivers, acrossRivers + kPi / 2, acrossRivers, 0};
	L.facing = facingBase[L.homeDesign] + context.bounded("karst-home-facing", 2) * kPi;
	context.telemetry.choice("karst.home.design", std::to_string(L.homeDesign));
	w.halfGate = std::max(kHomeDesigns[L.homeDesign].halfWidth, 3.5 / (L.homeRadius + kClearingMargin));
	return true;
}

// The towers: a Turing pattern's spots, fatter and closer where a slow noise says the karst is thick.
// Tower density moves both the thickets' extent and how many lone towers stand between them.
void raiseTowers(Layout &L, Work &w)
{
	const Torus &t = L.t;
	const int n = t.size(), density = w.o.towerDensity;
	TuringStyle style;
	style.wavelength = w.o.towerSpacing;
	w.field = turingPattern(t, style, w.context.stream("karst-pattern"));
	const std::vector<int> thicketNoise = fractalNoise(
		t.w, t.h, std::max(48, std::min(t.w, t.h) / 3), 2, w.context.stream("karst-thickets"));
	const int thicketFrom = 26000 - (density - 50) * 240;
	w.sparse = percentile(w.field, 97 - (density - 50) / 25);
	w.dense = percentile(w.field, 60);
	L.thicket.assign(n, 0);
	L.tower.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		L.thicket[i] = std::uint8_t(std::clamp((thicketNoise[i] - thicketFrom) * 255 / 8000, 0, 255));
		L.tower[i] = w.field[i] >= w.levelAt(L, i, 1.0);
	}
}

// Every home is a bowl: a clearing, a ring of towers with its gates open, and an apron. The ring comes
// from one stencil of noise read at each tile's offset from its home, so every ring is the same.
void shapeBowls(Layout &L, Work &w)
{
	const Torus &t = L.t;
	const int n = t.size();
	const HomeDesign &shape = kHomeDesigns[L.homeDesign];
	const double inner = L.homeRadius + kClearingMargin;
	w.stencil = periodicNoise(128, 128, 8, w.context.stream("karst-ring"));
	L.homeOf.assign(n, -1);
	L.bowl.assign(n, 0);
	L.gateOpen.assign(n, 0);
	for (const ShapePoint &home : L.homes)
		aroundPoint(t, home, int(std::ceil(L.bowlRadius)) + 2,
					[&](int i, int dx, int dy)
					{
						const double d = std::hypot(dx, dy);
						if (d > L.bowlRadius + 2)
							return;
						L.bowl[i] = 1;
						const double heading = std::atan2(dy, dx);
						const double edge = inner + kRingWobble * (w.stencilAt(dx, dy, 0) - 32768) / 32768.0;
						bool gate = false;
						for (double g : shape.gates)
							gate = gate || std::abs(std::remainder(heading - L.facing - g, 2 * kPi)) <= w.halfGate;
						if (gate)
							L.gateOpen[i] = d > edge + 1 && d <= edge + kRingCore;
						if (d <= std::max(edge, L.homeRadius + 1) || gate)
							L.tower[i] = 0;
						else if (d <= edge + kRingCore)
							L.tower[i] = 1;
						else if (d <= edge + kRingCore + kRingOuter)
							L.tower[i] = w.stencilAt(dx, dy, 64) >= 32768;
						else
							L.tower[i] = 0;
					});
	L.tower = dropSmallRegions(t, L.tower, 3);
}

// The rivers: each the cheapest walk round the torus through waypoints inside its band, always forwards
// along the axis (a free river may step back), dear near a tower, meandering by noise, never through a
// bowl. False, with `L.failure`, when a river finds no way.
bool routeRivers(Layout &L, Work &w)
{
	const Torus &t = L.t;
	const int n = t.size(), length = w.length, across = w.across;
	const std::vector<std::int64_t> nearTower = distanceSquaredTo(t, L.tower);
	const std::vector<int> meander =
		periodicNoise(t.w, t.h, std::max(12, w.o.towerSpacing), w.context.stream("karst-meander"));
	const std::vector<unsigned char> riverless = dilate(t, L.bowl, w.freeRiver ? w.riverWidth / 2 + 2 : 0);
	std::vector<int> cost(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const std::int64_t d2 = nearTower[i] < 0 ? 10000 : nearTower[i];
		cost[i] = riverless[i] ? -1
							   : 20 + int(std::int64_t(meander[i]) * w.o.meander * 16 / 5 / 65536) +
									 int(std::max<std::int64_t>(0, 64 - d2) * 20);
	}
	std::vector<unsigned char> centre(n, 0);
	Walker walker(t);
	for (size_t r = 0; r < L.rivers.size(); ++r)
	{
		River &river = L.rivers[r];
		const std::string stream = "karst-river-" + std::to_string(r);
		const auto inBand = [&](int i)
		{ return std::abs(towards(river.middle, crossOf(L, i), across)) <= river.band; };
		const int segments = std::max(3, length / kWaypointPitch);
		const int start = int(w.context.bounded(stream, std::uint32_t(length)));
		std::vector<int> waypoints;
		for (int s = 0; s < segments; ++s)
		{
			const int along = (start + s * length / segments) % length;
			const int offset = int(w.context.bounded(stream, std::uint32_t(2 * river.band + 1))) - river.band;
			// A free river's next waypoint stays near its last, so a bowl cannot fall between the two.
			const int wanted =
				w.freeRiver && !waypoints.empty()
					? crossOf(L, waypoints.back()) + offset * 12 / std::max(1, river.band)
					: river.middle + std::clamp(offset * w.o.meander * 7 / 500, -river.band, river.band);
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
				L.failure = kNoRiverWay;
				return false;
			}
			waypoints.push_back(best);
		}
		for (int s = 0; s < segments; ++s)
		{
			const int from = waypoints[s], to = waypoints[(s + 1) % segments];
			const int from0 = alongOf(L, from);
			const int span = wrapped(alongOf(L, to) - from0, length);
			std::vector<int> walk = walker.walk(
				from, [&](int i) { return i == to; },
				[&](int, int m, int dx, int dy)
				{
					const int forwards = L.alongX ? dx : dy;
					const int along = wrapped(alongOf(L, m) - from0, length);
					if ((forwards < 0 && !w.freeRiver) || along > span || cost[m] < 0 || !inBand(m))
						return -1;
					return cost[m] * (dx && dy ? 14 : 10) / 10;
				});
			if (walk.empty())
			{
				L.failure = kNoRiverWay;
				return false;
			}
			std::reverse(walk.begin(), walk.end());
			river.centreline.insert(river.centreline.end(), walk.begin(), walk.end() - 1);
		}
		for (int i : river.centreline)
			centre[i] = 1;
		w.context.telemetry.measure("karst.river.band", river.band, int(r));
		w.context.telemetry.measure("karst.river.length", river.centreline.size(), int(r));
	}
	L.riverWater = dilateRound(t, centre, (w.riverWidth - 1) / 2.0);
	L.water = L.riverWater;
	w.fromRiver = distanceSquaredTo(t, L.riverWater);
	return true;
}

// Fords on every river, evenly spaced along it and starting half a spacing from the first home, so with
// as many fords as homes in a row they fall between the homes. Each lies straight across the river where
// its centreline passes that position, at the middle of any run of the centreline along it.
void placeFords(Layout &L, const Work &w)
{
	const Torus &t = L.t;
	const int firstAlong = alongOf(L, L.homes[0]);
	for (const River &river : L.rivers)
	{
		const int count = int(river.centreline.size());
		for (int f = 0; f < w.o.fords; ++f)
		{
			const int along = wrapped(firstAlong + int((f + 0.5) * w.length / w.o.fords), w.length);
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
			const double half = w.riverWidth / 2.0 + 3;
			const double cx = river.centreline[p] % t.w + 0.5, cy = river.centreline[p] / t.w + 0.5;
			L.fords.push_back({{cx + ty / norm * half, cy - tx / norm * half},
							   {cx - ty / norm * half, cy + tx / norm * half},
							   {cx, cy}});
		}
	}
	w.context.telemetry.measure("karst.fords", L.fords.size());
}

// The squares of `radius` round both landings of every ford.
std::vector<unsigned char> fordLandings(const Layout &L, int radius)
{
	std::vector<unsigned char> mask(L.t.size(), 0);
	for (const Ford &f : L.fords)
		for (const ShapePoint &end : {f.from, f.to})
			aroundPoint(L.t, end, radius, [&](int i, int, int) { mask[i] = 1; });
	return mask;
}

// Doline lakes: one halfway between every two neighbouring homes in a row, the same lobed lake each
// time (its rim read from the stencil at each heading, drawn out along the row), so the open ground
// beside every bowl has water and the lakes are shared out evenly. A lake that would touch a bowl or
// come within eight tiles of river water is left out.
void fillLakes(Layout &L, Work &w)
{
	const Torus &t = L.t;
	const int length = w.length, across = w.across;
	std::vector<Row> rows = homeRows(L.homes, L.alongX, across);
	const auto homeAlong = [&](int k) { return alongOf(L, L.homes[k]); };
	const auto homeCross = [&](int k) { return crossOf(L, L.homes[k]); };
	int shortest = length;
	for (Row &row : rows)
	{
		std::sort(row.begin(), row.end(),
				  [&](const auto &a, const auto &b) { return homeAlong(a.first) < homeAlong(b.first); });
		for (size_t j = 0; j < row.size(); ++j)
			shortest = std::min(shortest, row.size() == 1 ? length
														  : wrapped(homeAlong(row[(j + 1) % row.size()].first) -
																		homeAlong(row[j].first),
																	length));
	}
	L.lakeRadius = std::min(9.0, (shortest - 2 * L.bowlRadius) / 2 - 5);
	std::vector<ShapePoint> sites;
	if (L.lakeRadius >= 4 && w.o.lakes)
		for (const Row &row : rows)
			for (size_t j = 0; j < row.size(); ++j)
			{
				const int a = row[j].first, b = row[(j + 1) % row.size()].first;
				const int span = row.size() == 1 ? length : wrapped(homeAlong(b) - homeAlong(a), length);
				const int along = wrapped(homeAlong(a) + span / 2, length);
				const int cross =
					wrapped(homeCross(a) + towards(homeCross(a), homeCross(b), across) / 2, across);
				sites.push_back(L.alongX ? ShapePoint{double(along), double(cross)}
										 : ShapePoint{double(cross), double(along)});
			}
	const std::vector<unsigned char> riverSide = dilate(t, L.riverWater, 8);
	L.lakeWater.assign(t.size(), 0);
	for (const ShapePoint &lake : sites)
	{
		bool clear = true;
		std::vector<int> tiles;
		aroundPoint(t, lake, int(L.lakeRadius * 1.35 * 1.2) + 2,
					[&](int i, int dx, int dy)
					{
						const double heading = std::atan2(dy, dx);
						const double lobes = w.stencilAt(int(std::lround(9 * std::cos(heading))),
														 int(std::lround(9 * std::sin(heading))), 32) /
											 65536.0;
						const double stretched = L.alongX ? std::hypot(dx / 1.35, dy) : std::hypot(dx, dy / 1.35);
						if (stretched <= L.lakeRadius * (0.55 + 0.8 * lobes) + 0.5)
						{
							tiles.push_back(i);
							clear = clear && !riverSide[i] && !L.bowl[i];
						}
					});
		if (!clear)
			continue;
		L.lakes.push_back(lake);
		for (int i : tiles)
			L.water[i] = L.lakeWater[i] = 1;
	}
	w.context.telemetry.measure("karst.lakes", L.lakes.size());
	w.context.telemetry.measure("karst.lake.radius", L.lakeRadius);
}

// Sinkhole ponds at the troughs of the tower field beyond the valleys, in thickets and open ground alike
// (a trough counts when nothing lower lies within a window the Sinkholes control narrows), and from
// each a sinking stream: a chain of pools along the cheapest way towards the river, round the towers,
// sinking into the ground before it reaches the paddies. The gaps of land between the pools keep the
// thickets walkable.
void digSinkholes(Layout &L, const Work &w)
{
	const Torus &t = L.t;
	const KarstTowersOptions &o = w.o;
	const int n = t.size();
	const std::vector<int> lowest =
		windowMinimum(t, w.field, std::max(4, 3 * o.towerSpacing * 100 / (2 * std::max(25, o.sinkholes))));
	const double sinkholeFrom = w.riverWidth / 2.0 + o.paddyDepth + 10;
	const std::vector<unsigned char> keepDry = dilate(t, L.bowl, 4);
	// Up to double the size, and no bigger, however many sinkholes there are.
	const int sizePercent = std::min(o.sinkholes, 200);
	std::vector<int> queued(n, 0), sinkholes;
	int ponds = 0;
	for (int i = 0; i < n; ++i)
		if (o.sinkholes > 0 && w.field[i] == lowest[i] && !keepDry[i] && !L.tower[i] &&
			w.fromRiver[i] > std::int64_t(sinkholeFrom * sinkholeFrom))
		{
			growWater(
				t, L.water, i, kSinkholeTiles * sizePercent / 100, [&](int j) { return !keepDry[j]; },
				[&](int j)
				{ return std::int64_t(w.field[j]) + t.dist2(i % t.w, i / t.w, j % t.w, j / t.w) * 400; },
				queued, ++ponds);
			sinkholes.push_back(i);
		}
	w.context.telemetry.measure("karst.sinkholes", ponds);

	const double valleyEdge = w.riverWidth / 2.0 + o.paddyDepth + 6;
	std::vector<unsigned char> reached = L.riverWater;
	Walker walker(t);
	int streams = 0, pools = 0;
	for (int from : sinkholes)
	{
		// A walk of more than kLongestStream tiles is given up, and every step costs at most 14, so
		// once the cheapest tile left costs more than kLongestStream - 1 diagonal steps, any walk still
		// to be found is too long.
		std::vector<int> walk = walker.walk(
			from, [&](int i) { return reached[i] != 0; }, [&](int, int m, int dx, int dy)
			{ return keepDry[m] || L.tower[m] ? -1 : (dx && dy ? 14 : 10); },
			(kLongestStream - 1) * 14);
		if (walk.empty() || int(walk.size()) > kLongestStream)
			continue;
		++streams;
		std::reverse(walk.begin(), walk.end());
		for (size_t p = kStreamPoolPitch; p + 4 < walk.size(); p += kStreamPoolPitch)
		{
			const int at = walk[p];
			if (w.fromRiver[at] < std::int64_t(valleyEdge * valleyEdge))
				break;
			growWater(
				t, L.water, at, kStreamPoolTiles * sizePercent / 100, [&](int j) { return !keepDry[j]; },
				[&](int j)
				{ return std::int64_t(t.dist2(at % t.w, at / t.w, j % t.w, j / t.w)) * 100 + w.field[j] / 64; },
				queued, ++ponds);
			++pools;
		}
		for (int i : walk)
			reached[i] = 1;
	}
	w.context.telemetry.measure("karst.streams", streams);
	w.context.telemetry.measure("karst.stream-pools", pools);
}

// No tower on a shore, nor at a ford's landings; and the valley floor thins out towards a river, so the
// paddies lie on open ground and the thickets stand back from the banks.
void clearValleys(Layout &L, const Work &w)
{
	const Torus &t = L.t;
	std::vector<unsigned char> shore = dilate(t, L.water, 3);
	const std::vector<unsigned char> landings = fordLandings(L, 4);
	const double valley = w.riverWidth / 2.0 + w.o.paddyDepth + 4;
	for (int i = 0; i < t.size(); ++i)
	{
		if (L.bowl[i])
			continue;
		const double d = std::sqrt(double(w.fromRiver[i]));
		if (shore[i] || landings[i])
			L.tower[i] = 0;
		else if (d < valley)
			L.tower[i] = w.field[i] >= w.levelAt(L, i, d / valley) + int((valley - d) / valley * 8000);
	}
	L.tower = dropSmallRegions(t, L.tower, 3);
}

// Each tile's position along the rivers: the index along the nearest river's centreline, flooded out
// from the centrelines, with a gap between one river's numbering and the next.
std::vector<int> riverReach(const Layout &L)
{
	const Torus &t = L.t;
	std::vector<int> reach(t.size(), -1), queue;
	int offset = 0;
	for (const River &river : L.rivers)
	{
		for (size_t p = 0; p < river.centreline.size(); ++p)
			if (reach[river.centreline[p]] < 0)
			{
				reach[river.centreline[p]] = offset + int(p);
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
			if (reach[m] < 0)
			{
				reach[m] = reach[i];
				queue.push_back(m);
			}
		}
	}
	return reach;
}

// The paddies and lake fields, as labelled corners: bank terraces (alternating crop and water ribbons
// by distance from the river, cut across by the river's length) on open ground within reach of a river
// and clear of towers, bowls, ponds and fords; and round each lake a ring of six fields, cleared of
// towers so they stay whole. Every corner of a field is a bund where its right or lower neighbour is
// another field, or any neighbour is off the fields: then no tile spanning two fields is pure grass,
// and a crop cannot cross. The water ribbon's paddies are flooded, and a few of the crop ribbon's, on
// every corner inside the bunds: the bund is the paddy's only sand (pure sand beside a crop stops it
// regrowing) and stays walkable. A paddy too small to hold four such corners stays dry, and lake fields
// are never flooded.
void terracePaddies(Layout &L, Work &w)
{
	const Torus &t = L.t;
	const KarstTowersOptions &o = w.o;
	const int n = t.size();
	// A paddy keeps three tiles from water and towers: none within two tiles along both axes, the square
	// that a squared distance under 9 covers exactly.
	const std::vector<unsigned char> nearWater = dilate(t, L.water, 2);
	const std::vector<unsigned char> nearTower = dilate(t, L.tower, 2);
	const std::vector<int> reach = riverReach(L);
	const std::vector<unsigned char> landings = fordLandings(L, 5);
	const std::int64_t depth2 = std::int64_t(o.paddyDepth + 2) * (o.paddyDepth + 2);
	const std::vector<unsigned char> apron = dilate(t, L.bowl, 2);
	L.paddyZone.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.paddyZone[i] = !apron[i] && !landings[i] && !nearWater[i] && w.fromRiver[i] <= depth2 &&
						 !nearTower[i];
	L.paddyZone = dropSmallRegions(t, L.paddyZone, 30);
	std::vector<int> label(n, -1);
	const std::vector<int> jitter = periodicNoise(t.w, t.h, 24, w.context.stream("karst-paddy-jitter"));
	for (int i = 0; i < n; ++i)
		if (L.paddyZone[i])
		{
			const double wobble = (jitter[i] - 32768) / 32768.0;
			const double depth = std::sqrt(double(w.fromRiver[i])) + wobble * 0.5;
			const int terrace = int(depth / (kCropRibbon + kWaterRibbon));
			const int ribbon = 2 * terrace + (depth - terrace * (kCropRibbon + kWaterRibbon) >= kCropRibbon);
			label[i] = ribbon * 100000 + int(reach[i] + wobble * 4) / kPaddyReach;
		}
	// The ring between 2 and 10 tiles out from the lakes, from one distance field.
	const std::vector<std::int64_t> fromLake =
		L.lakes.empty() ? std::vector<std::int64_t>() : distanceSquaredTo(t, L.lakeWater);
	for (size_t k = 0; k < L.lakes.size(); ++k)
		aroundPoint(t, L.lakes[k], int(L.lakeRadius * 1.35 * 1.4) + 12,
					[&](int i, int dx, int dy)
					{
						if (fromLake[i] < 0 || fromLake[i] > 100 || fromLake[i] <= 4 ||
							apron[i] || L.paddyZone[i])
							return;
						L.tower[i] = 0;
						L.paddyZone[i] = 1;
						const int sector = int(std::floor((std::atan2(dy, dx) + kPi) / (kPi / 3))) % 6;
						label[i] = kLakeFieldLabel + int(k) * 10 + sector;
					});

	L.terrain.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
	{
		if (!L.paddyZone[i])
			continue;
		const int x = i % t.w, y = i / t.w;
		const int right = t.at(x + 1, y), down = t.at(x, y + 1);
		if (!L.paddyZone[right] || !L.paddyZone[down] || !L.paddyZone[t.at(x - 1, y)] ||
			!L.paddyZone[t.at(x, y - 1)] || label[right] != label[i] || label[down] != label[i])
			L.terrain[i] = SAND;
	}
	std::map<int, int> paddyIds;
	L.paddyOf.assign(n, -1);
	for (int i = 0; i < n; ++i)
		if (label[i] >= 0 && pureTile(L.terrain, t, i, GRASS))
			L.paddyOf[i] = paddyIds.emplace(label[i], int(paddyIds.size())).first->second;
	L.paddies = int(paddyIds.size());

	std::vector<int> ribbonOf(L.paddies, 0), poolSize(L.paddies, 0), pool;
	L.lakeField.assign(L.paddies, 0);
	for (const auto &[key, id] : paddyIds)
	{
		ribbonOf[id] = key / 100000;
		L.lakeField[id] = key >= kLakeFieldLabel;
	}
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
		const int draw = int(w.context.bounded("karst-flooded", 100));
		L.flooded[p] = !L.lakeField[p] && poolSize[p] >= 4 &&
					   (ribbonOf[p] % 2 ? draw >= 100 - o.flooded
										: draw < kFloodedCrop * o.flooded / kDefaultFlooded);
		flooded += L.flooded[p];
	}
	for (int i : pool)
		if (L.flooded[paddyIds.at(label[i])])
			L.water[i] = 1;
	w.context.telemetry.measure("karst.paddies", L.paddies);
	w.context.telemetry.measure("karst.paddies.flooded", flooded);
}

// Each home's own water, the same offsets round every home: a paddy north of its middle, bunded round,
// with an irrigation channel two tiles wide along its far side; a lobed pool just inside the apron beside
// its first gate, and its third where it has one; and a doline pond, bigger than a round home's usual
// one, against the clearing's rim east-south-east of the middle, so it leaves the middle free for
// building and its beach keeps off the paddy. The starter kit is planted round the pond. Then the
// beaches, and the fords laid across the rivers.
void waterHomes(Layout &L, const Work &w)
{
	const Torus &t = L.t;
	const int n = t.size();
	L.homePaddy.assign(n, 0);
	const auto inBlock = [&](int dx, int dy)
	{ return dy >= -13 && dy <= -3 && std::abs(dx) <= 7 && std::hypot(dx, dy) <= L.homeRadius - 0.5; };
	for (const ShapePoint &home : L.homes)
		aroundPoint(t, home, 14,
					[&](int i, int dx, int dy)
					{
						if (!inBlock(dx, dy))
							return;
						const bool edge = !inBlock(dx + 1, dy) || !inBlock(dx - 1, dy) || !inBlock(dx, dy + 1) ||
										  !inBlock(dx, dy - 1);
						L.terrain[i] = edge ? SAND : GRASS;
						if (!edge && dy >= -12 && dy <= -10 && std::abs(dx) <= 5)
							L.water[i] = 1;
						L.homePaddy[i] = 1;
					});
	const std::vector<double> &gates = kHomeDesigns[L.homeDesign].gates;
	for (const ShapePoint &home : L.homes)
		for (size_t g = 0; g < gates.size(); g += 2)
		{
			const double heading = L.facing + gates[g] + w.halfGate + kGatePoolTurn;
			const double out = L.bowlRadius - kGatePool;
			const double px = out * std::cos(heading), py = out * std::sin(heading);
			aroundPoint(t, home, int(std::ceil(L.bowlRadius)) + 2,
						[&](int i, int dx, int dy)
						{
							const double lobes = w.stencilAt(dx, dy, 48) / 65536.0;
							if (std::hypot(dx - px, dy - py) <= kGatePool * (0.65 + 0.7 * lobes))
							{
								L.water[i] = 1;
								L.tower[i] = 0;
							}
						});
		}
	const double pondRadius = std::min(4.2, 0.28 * L.homeRadius);
	const RadialShape pond(pondRadius, 0.2, w.context, "karst-pond");
	for (const ShapePoint &home : L.homes)
	{
		const double out = std::floor(L.homeRadius - pondRadius - 1.5);
		const ShapePoint centre{std::floor(home.x) + std::round(out * 0.94) + 0.5,
								std::floor(home.y) + std::round(out * 0.34) + 0.5};
		fillShape(L.water, t, centre.x, centre.y, pond);
		L.kits.push_back(centre);
	}
	for (size_t k = 0; k < L.homes.size(); ++k)
		aroundPoint(t, L.homes[k], int(L.homeRadius) + 1,
					[&](int i, int dx, int dy)
					{
						if (std::hypot(dx, dy) <= L.homeRadius)
							L.homeOf[i] = int(k);
					});
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			L.terrain[i] = WATER;
	layBeaches(L.terrain, t);
	for (const Ford &f : L.fords)
		bridgeAcross(L.terrain, t, f.from, f.to, 1.5);
	// The home paddies' tiles join the paddies, one id per home, for the containment check.
	for (size_t k = 0; k < L.homes.size(); ++k)
		aroundPoint(t, L.homes[k], 14,
					[&](int i, int, int)
					{
						if (L.homePaddy[i] && pureTile(L.terrain, t, i, GRASS))
							L.paddyOf[i] = L.paddies + int(k);
					});
}

// The layout with homes no bigger than `homeCap` and rivers no wider than `riverCap`.
Layout designAt(const GenerationRequest &request, GenerationContext &context, int homeCap, int riverCap)
{
	const KarstTowersOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	Work w{o, context};
	w.riverWidth = std::min(o.riverWidth, riverCap);
	if (!placeHomesAndRivers(L, w, homeCap))
		return L;
	raiseTowers(L, w);
	shapeBowls(L, w);
	if (!routeRivers(L, w))
		return L;
	placeFords(L, w);
	fillLakes(L, w);
	digSinkholes(L, w);
	clearValleys(L, w);
	terracePaddies(L, w);
	waterHomes(L, w);
	return L;
}

// The layout, with the homes shrunk a step at a time while their bowls leave a river no way through, and
// then the rivers narrowed: big homes or wide rivers on a crowded map close every gap, and smaller homes
// or a narrower river are a better map than none.
Layout designAfresh(const GenerationRequest &request, GenerationContext &context)
{
	const KarstTowersOptions o(request);
	int home = o.homeSize, river = o.riverWidth;
	for (;;)
	{
		Layout L = designAt(request, context, home, river);
		if (L.failure != kNoRiverWay)
			return L;
		if (home - 2 >= kSmallestHome)
		{
			home -= 2;
			context.telemetry.fallback("karst.home.shrunk-for-river", "Homes shrank so a river could pass.");
		}
		else if (river - 2 >= kNarrowestRiver)
		{
			river -= 2;
			context.telemetry.fallback("karst.river.narrowed", "The rivers narrowed to pass between the homes.");
		}
		else
			return L;
	}
}

// A generation asks for the same design three times: the request check (designFailure), generate and
// validateWorld, and building it was most of a generation's time. The design depends only on the request
// and the named streams it draws from, so the last one built on this thread, in a context of its own, is
// kept and handed out again for the same request (nbWorkers plays no part in it): its telemetry replayed
// into the asking context, and every stream it drew from wound on to where building it left that stream,
// so whatever the context draws next is unchanged. The streams are the ones the building context asked
// for, so a new draw in the design needs no list kept by hand. A context that has already drawn from one
// of those streams gets the design built afresh from where its streams stand.
struct DesignCache
{
	bool valid = false;
	GenerationRequest request;
	Layout layout;
	GenerationTelemetry telemetry;
	std::map<std::string, std::mt19937> streams; // each as the design left it
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	thread_local DesignCache cache;
	const GenerationRequest &was = cache.request;
	if (!cache.valid || was.method != request.method || was.wDec != request.wDec ||
		was.hDec != request.hDec || was.nbTeams != request.nbTeams || was.seed != request.seed ||
		was.options != request.options)
	{
		cache.valid = false;
		GenerationContext fresh(request, true);
		cache.layout = designAfresh(request, fresh);
		cache.telemetry = fresh.telemetry;
		cache.streams = fresh.namedStreams();
		cache.request = request;
		cache.valid = true;
	}
	for (const auto &[name, state] : cache.streams)
		if (context.stream(name) != std::mt19937(GenerationContext::deriveSeed(request.seed, name)))
			return designAfresh(request, context);
	for (const auto &[name, state] : cache.streams)
		context.stream(name) = state;
	context.telemetry.replay(cache.telemetry);
	return cache.layout;
}

// Wheat on every open tile of `L`'s paddies that `sown` (by paddy id) and `keep` (by tile) allow.
template <typename Sown, typename Keep>
void sowPaddies(Map &map, const Layout &L, Sown sown, Keep keep)
{
	const Torus &t = L.t;
	for (int i = 0; i < t.size(); ++i)
		if (L.paddyOf[i] >= 0 && sown(L.paddyOf[i]) && keep(i) && map.isResourceAllowed(i % t.w, i / t.w, WHEAT))
			map.setResource(i % t.w, i / t.w, WHEAT, 1);
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
	sowPaddies(map, L, [&](int p) { return p >= L.paddies; }, open);

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
	sowPaddies(map, L, [&](int p) { return p < L.paddies && sown[p]; }, open);

	// Woods in the thickets and in broken patches at the towers' feet. Wood that water could spread stays
	// well away from the bowls and lakes.
	const Fertility::Field growth = cropGrowthField(L.terrain, t);
	const std::vector<std::int64_t> fromTower = distanceSquaredTo(t, L.tower);
	const std::vector<std::int64_t> fromBowl = distanceSquaredTo(t, L.bowl);
	const std::vector<std::int64_t> fromLake = distanceSquaredTo(t, L.lakeWater);
	const std::vector<int> woods = periodicNoise(t.w, t.h, 14, context.stream("karst-woods"));
	const double woodShare = scaledShare(0.4, o.wood);
	const std::int64_t wetClearance2 = std::int64_t(kWetWoodClearance * kWetWoodClearance);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (fromTower[i] <= 0 || L.bowl[i] || L.paddyZone[i] || !open(i) || !map.isResourceAllowed(x, y, WOOD))
			continue;
		if (growth.at(x, y) > 0 &&
			(fromBowl[i] < wetClearance2 || (fromLake[i] >= 0 && fromLake[i] < wetClearance2)))
			continue;
		const double chance = fromTower[i] <= 5 ? woodShare * 0.6 : woodShare * L.thicket[i] / 255.0;
		if (woods[i] < chance * 65536)
			map.setResource(x, y, WOOD, 1);
	}

	// Lake fields: the same patches of wheat, three tiles square, round every lake, by offset.
	const double lakeShare = scaledShare(0.75, o.wheat);
	for (const ShapePoint &lake : L.lakes)
		aroundPoint(t, lake, int(L.lakeRadius * 1.9) + 11,
					[&](int i, int dx, int dy)
					{
						const unsigned hash =
							unsigned(((dx + 99) / 3) * 73856093) ^ unsigned(((dy + 99) / 3) * 19349663);
						const int p = L.paddyOf[i];
						if (std::hypot(dx, dy) <= L.lakeRadius * 1.9 + 11 && (hash % 1000) < lakeShare * 1000 &&
							p >= 0 && p < L.paddies && L.lakeField[p] && open(i) &&
							map.isResourceAllowed(i % t.w, i / t.w, WHEAT))
							map.setResource(i % t.w, i / t.w, WHEAT, 1);
					});

	// Orchards anywhere on open ground off the bowls, paddies and towers.
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
	if (const std::string lost = homePondMissing(map, t, L.kits, context.request.nbTeams, "bowl", "pond");
		!lost.empty())
		return lost;
	const auto at = [](int x, int y) { return "(" + std::to_string(x) + ", " + std::to_string(y) + ")"; };
	// Paddies and lake fields are sealed: no pure-grass tile of one touches pure grass that is not the same.
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
		if (L.gateOpen[i] && map.isResource(i % t.w, i / t.w) && map.getResource(i % t.w, i / t.w).type == STONE)
			return "A home's gate is closed by stone at " + at(i % t.w, i / t.w) + ".";
	for (const Ford &f : L.fords)
		if (map.isWater(int(f.centre.x), int(f.centre.y)))
			return "A ford is under water at " + at(int(f.centre.x), int(f.centre.y)) + ".";
	return walkFromFirstColony(map, context.request.nbTeams, "the karst", "through the towers and fords").error;
}
} // namespace

KarstTowersOptions::KarstTowersOptions(const GenerationRequest &r)
	: towerSpacing(r.option("tower-spacing")), towerDensity(r.option("tower-density")),
	  homeDesign(r.option("home-design")), riverWidth(r.option("river-width")), fords(r.option("fords")),
	  paddyDepth(r.option("paddy-depth")), homeSize(r.option("home-size")), meander(r.option("river-meander")),
	  sinkholes(r.option("sinkholes")), flooded(r.option("flooded-terraces")), lakes(r.option("lakes")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition karstTowersDefinition()
{
	return {
			"karst-towers",
			54,
			"Karst towers",
			2,
			false,
			{{"tower-spacing", "Tower spacing", 14, 32, 2, 16, ControlGroup::Terrain},
			 {"tower-density", "Tower density", 0, 100, 10, 50, ControlGroup::Terrain},
			 {"river-width", "River width", kNarrowestRiver, 16, 1, 7, ControlGroup::Terrain},
			 {"fords", "Fords", 1, 6, 1, 2, ControlGroup::Terrain},
			 {"paddy-depth", "Paddy depth", 16, 40, 2, 24, ControlGroup::Terrain},
			 {"river-meander", "River meander", 0, 100, 10, 50, ControlGroup::Terrain},
			 {"flooded-terraces", "Flooded terraces", 0, 100, 4, kDefaultFlooded, ControlGroup::Terrain},
			 {"sinkholes", "Sinkholes", 0, 300, 25, 100, ControlGroup::Terrain},
			 GeneratorControl::toggle("lakes", "Lakes", true, ControlGroup::Layout),
			 GeneratorControl::choice("home-design", "Home design",
									  {"Random", "Horseshoe", "Twin gates", "Three gates", "Four gates"}, 0,
									  ControlGroup::Layout),
			 {"home-size", "Home size", kSmallestHome, 22, 1, 15, ControlGroup::Layout},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount", 200),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			{"terrain:natural", "feature:river", "feature:mountains", "feature:lakes",
			 "style:tight-building", "fairness:stamped-lattice"}};
}
