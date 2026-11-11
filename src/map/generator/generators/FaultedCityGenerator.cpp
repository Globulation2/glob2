// SPDX-License-Identifier: GPL-3.0-or-later
#include "FaultedCityGenerator.h"
#include "Climates.h"
#include "Contact.h"
#include "Farmland.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Growth.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Resources.h"
#include "Room.h"
#include "ScoredSettlements.h"
#include "Sketch.h"
#include "Tessellation.h"
#include "Walls.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>
using namespace MapGeneration;

// A planned city broken by an earthquake. One source city is drawn once and sampled through a
// different integer translation in each district of a warped toroidal tessellation, so the same
// streets and block outlines continue at visibly different offsets across every fault.
//
// REVISION 6 (FAULTED CITY 2.0, 2026-11-11). Revisions 1 to 5 drew the city in grass, sand and
// water: sand faults everyone walked and nobody built on, grass streets indistinguishable from the
// lawns, stone-resource masonry, sand-ringed gardens and beaches. 2.0 draws it from the terrain
// catalogue, in one fixed urban look:
//  - Every fault is a RIFT. Each district has a random elevation; along every fault the higher
//    district's rim is a Ridges SCARP (towers shoot over it, units cannot pass), and the rest of
//    the fault is a rough rubble floor (walkable, slow, unbuildable) with sag ponds. A district
//    is entered across its scarps only by paved RAMPS at its surviving junctions, and from its
//    low sides straight off the rift floor: the ramps are the city's fronts.
//  - Streets are paved: twice as fast, buildable, and nothing grows on them, so they seal blocks.
//  - Standing facades, hall frontages and the plaza's colonnades are Ridges walls three vertices
//    thick. Collapsed ruins (the empty courtyard shells) stay stone: rubble a colony quarries,
//    with rubble heaps in their courtyards scaled by the stone amount.
//  - Garden caps and dividers are barren ground; there are no beaches.
//  - Colonies start in stamped HOME BLOCKS: one of three designs per map, one facing per map, each
//    a paved court round the swarm, farmable wings with their own water, and a remnant wall with a
//    quarry face at its foot. Every home is an exact translation of every other.
//  - The broken civic plaza is the PRIZE: a fruit avenue, fountains and a quarry face beside its
//    colonnades, split by a fault so its two pieces stand in different districts.
namespace
{
constexpr int kPitch = 32;
// The scarp's depth into the fault from the high district's rim, in tiles (plus a frayed tile).
constexpr int kScarpDepth = 3;
// A ramp: every fault tile within this many tiles of its gate (Euclidean) is paved.
constexpr int kRampRadius = 6;
// Stone at the foot of each home's remnant wall, and beside the plaza's colonnades.
constexpr int kHomeQuarry = 12, kPlazaQuarry = 16;
constexpr int kLayoutAttempts = 8;
enum BlockKind { Ruin, Square, Garden, Reservoir, Orchard };
struct Block { int kind, variant, facing; };

// The city's one look (fixed, like Canals' lagoon): slate streets, plaza and ramps, ridge-rock
// scarps and facades, scree rift floors, clay garden caps, loam round the sag ponds, tended lawns
// at home and lush farm rows. No sand, no deep water, no obstacles are drawn.
constexpr ClimateLook kCityLook = {"faulted-city", HEDGE,        RIDGE_ROCK,   LUSH_GRASS,
								   GRASS,          PALE_SAND,    SLATE_ASHLAR, SLATE_ASHLAR,
								   SLATE_ASHLAR,   SLATE_ASHLAR, BOARDWALK,    CLAY,
								   DEEP_WATER,     SCREE,        LOAM,         RIDGE_ROCK,
								   LUSH_GRASS};
static_assert(terrainGroup(kCityLook.rock) == TerrainGroup::Ridges &&
				  terrainGroup(kCityLook.rockAccent) == TerrainGroup::Ridges &&
				  terrainGroup(kCityLook.homeGrass) == TerrainGroup::Grass &&
				  terrainGroup(kCityLook.openGrass) == TerrainGroup::Grass &&
				  terrainGroup(kCityLook.farmGrass) == TerrainGroup::Grass &&
				  terrainGroup(kCityLook.street) == TerrainGroup::Paths &&
				  terrainGroup(kCityLook.scrub) == TerrainGroup::Barren &&
				  terrainGroup(kCityLook.rough) == TerrainGroup::Rough &&
				  terrainGroup(kCityLook.fertile) == TerrainGroup::Fertile,
			  "The city's look keeps every mechanic's rules.");

// HOME BLOCKS. Rectangles are inclusive, in the block's own 32-tile frame (streets on 0..2 and
// 29..31), before the map's facing turns them. A court is paved vertices, a farm's outer rectangle
// is its barren rim, its pond water, a divider barren, and its other cells wheat (inside `wheat`)
// or wood. The remnant is rock vertices: a surviving wall with a quarry face on the lawn beside it.
struct Rect
{
	int x0, y0, x1, y1;
	constexpr bool has(int x, int y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};
constexpr Rect kNone{1, 1, 0, 0};
struct HomeFarm { Rect outer, pond, wheat, divider; };
struct HomeDesign
{
	const char *name;
	Rect court;
	std::array<HomeFarm, 2> farms;
	Rect remnant;
	int swarmX, swarmY; // the swarm's anchor, in the court
};
constexpr std::array<HomeDesign, 3> kHomes = {{
	// A forum: a paved court and a lawn to the north, one farm across the south with its pond along
	// the back, wheat west and wood east of a divider.
	{"forum", {4, 4, 19, 15}, {{{{4, 17, 27, 27}, {5, 24, 26, 26}, {5, 18, 18, 23}, {19, 18, 19, 23}},
								 {kNone, kNone, kNone, kNone}}},
	 {24, 4, 27, 8}, 10, 11},
	// A cloister: a paved court down the middle, a wheat garden west along a canal, a wood garden
	// east below the lawn along another.
	{"cloister", {14, 4, 20, 27}, {{{{4, 4, 13, 27}, {5, 5, 7, 26}, {8, 5, 12, 26}, kNone},
									{{21, 15, 27, 27}, {25, 16, 26, 26}, kNone, kNone}}},
	 {24, 4, 27, 8}, 15, 14},
	// A terrace: a paved court in one corner, a lawn beside it, and a long farm whose pond parts
	// wheat from wood.
	{"terrace", {4, 4, 15, 15}, {{{{4, 17, 27, 27}, {13, 18, 18, 26}, {5, 18, 12, 26}, kNone},
								  {kNone, kNone, kNone, kNone}}},
	 {24, 4, 27, 8}, 8, 11},
}};

// Source designs are integer tile drawings: turns preserve streets, seals and crop widths.
std::pair<int, int> localPoint(int x, int y, int facing)
{
	for (int turn = 0; turn < facing; ++turn)
	{
		const int old = x;
		x = y;
		y = kPitch - 1 - old;
	}
	return {x, y};
}

// A source block lying whole inside one district, clear of its faults: where a home may stand.
struct Instance { int district, block, origin, swarm; };

// Everything the city's terrain depends on but the choice of homes: drawn from the request's
// layout controls on named streams, never from the resource amounts.
struct Plan
{
	Torus t{1, 1};
	int columns = 0, blockCount = 0, districtCount = 0;
	std::vector<Block> blocks;
	std::vector<std::pair<int, int>> shifts;
	int phaseX = 0, phaseY = 0;
	int plazaX = 0, plazaY = 0, plazaVariant = 0;
	bool verticalPlaza = false;
	int homeDesign = 0, homeFacing = 0;
	std::vector<int> district, faultSteps;
	std::vector<unsigned char> fault, scarp, floor, ramp, pond, street, junction;
	std::vector<int> gates;
	std::vector<Instance> instances;
	std::string failure;
	int sourceX(int i) const { return t.x(t.remainderX(i) - shifts[district[i]].first + phaseX); }
	int sourceY(int i) const { return t.y(i / t.w - shifts[district[i]].second + phaseY); }
	int blockOf(int i) const { return (sourceY(i) / kPitch) * columns + sourceX(i) / kPitch; }
	// The tile's position in the plaza's frame: u along it, v across.
	std::pair<int, int> plazaFrame(int i) const
	{
		const int pdx = t.offsetX(plazaX, sourceX(i)), pdy = t.offsetY(plazaY, sourceY(i));
		return verticalPlaza ? std::pair{pdy, pdx} : std::pair{pdx, pdy};
	}
	bool inPlaza(int i) const
	{
		const auto [u, v] = plazaFrame(i);
		return u >= -48 && u < 48 && v >= -16 && v < 16;
	}
};

std::string atTile(const Torus &t, int i)
{
	return "(" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) + ")";
}

Plan plan(const GenerationRequest &r, GenerationContext &c)
{
	const FaultedCityOptions o(r);
	Plan P;
	P.t = {1 << r.wDec, 1 << r.hDec};
	const Torus &t = P.t;
	const int n = t.size();
	P.columns = t.w / kPitch;
	P.blockCount = P.columns * (t.h / kPitch);
	P.blocks.resize(P.blockCount);
	std::array<int, 5> counts{};
	for (auto &b : P.blocks)
	{
		const int roll = c.bounded("faulted-city-blocks", 100);
		b.kind = roll < o.ruins ? Ruin :
			roll < o.ruins + (100 - o.ruins) * 25 / 65 ? Square :
			roll < o.ruins + (100 - o.ruins) * 50 / 65 ? Garden :
			roll < o.ruins + (100 - o.ruins) * 60 / 65 ? Reservoir : Orchard;
		b.variant = c.bounded("faulted-city-details", 3);
		b.facing = c.bounded("faulted-city-details", 4);
		++counts[b.kind];
	}
	// A coarse tiling describes the FINAL districts; inverse sampling has exactly one owner
	// per corner. No splat ordering, holes, doubled deposits or floating-point displacement.
	auto districts = squareTessellation(t.w, t.h, 96);
	std::vector<unsigned char> edges(districts.edges.size(), 1);
	warpCorners(districts, warpLimit(districts), edges, 30, 28, c, "faulted-city-faults");
	P.district = labelTiles(districts);
	if (P.district.empty()) { P.failure = "The blocks could not be labelled."; return P; }
	P.districtCount = districts.cellCount();
	P.shifts.resize(P.districtCount);
	for (int d = 0; d < P.districtCount; ++d)
	{
		auto &shift = P.shifts[d];
		shift.first = int(c.bounded("faulted-city-shifts", 2 * o.displacement + 1)) - o.displacement;
		shift.second = int(c.bounded("faulted-city-shifts", 2 * o.displacement + 1)) - o.displacement;
		// Force a different street phase at every previously assigned neighbour, deterministically.
		for (int tries = 0; tries < (2 * o.displacement + 1) * (2 * o.displacement + 1); ++tries)
		{
			bool distinct = true;
			for (int edge : districts.cells[d].edges)
			{
				const int other = districts.other(edge, d);
				if (other < d && std::max(std::abs(P.shifts[other].first - shift.first),
					std::abs(P.shifts[other].second - shift.second)) < std::max(2, o.displacement / 2)) distinct = false;
			}
			if (distinct) break;
			if (++shift.first > o.displacement)
			{ shift.first = -o.displacement; if (++shift.second > o.displacement) shift.second = -o.displacement; }
		}
		c.telemetry.measure("faulted-city.shift.x", shift.first, d);
		c.telemetry.measure("faulted-city.shift.y", shift.second, d);
	}
	P.phaseX = c.bounded("faulted-city-grid", kPitch);
	P.phaseY = c.bounded("faulted-city-grid", kPitch);
	// Put a three-block plaza across an actual fault, in the original city's frame. Both sides
	// sample this SAME civic drawing, so the displaced halves remain related.
	std::vector<int> landmarkEdges;
	for (int edge = 0; edge < int(districts.edges.size()); ++edge)
	{
		const auto e = districts.edgeEnds(edge, districts.edges[edge].cells[0]);
		const int x = t.x(int(subtileTile((e.first.x + e.second.x) / 2)));
		const int y = t.y(int(subtileTile((e.first.y + e.second.y) / 2)));
		if (x > 48 && x < t.w - 48 && y > 48 && y < t.h - 48) landmarkEdges.push_back(edge);
	}
	if (landmarkEdges.empty()) { P.failure = "The blocks could not be labelled."; return P; }
	const int landmarkEdge = landmarkEdges[c.bounded("faulted-city-landmark", landmarkEdges.size())];
	const int owner = districts.edges[landmarkEdge].cells[0];
	const auto ends = districts.edgeEnds(landmarkEdge, owner);
	const int mx = int(subtileTile((ends.first.x + ends.second.x) / 2));
	const int my = int(subtileTile((ends.first.y + ends.second.y) / 2));
	P.verticalPlaza = std::abs(ends.second.x - ends.first.x) > std::abs(ends.second.y - ends.first.y);
	const int sourceMX = t.x(mx - P.shifts[owner].first + P.phaseX);
	const int sourceMY = t.y(my - P.shifts[owner].second + P.phaseY);
	P.plazaX = (sourceMX / kPitch) * kPitch + 16;
	P.plazaY = (sourceMY / kPitch) * kPitch + 16;
	P.plazaVariant = c.bounded("faulted-city-landmark", 3);
	c.telemetry.choice("faulted-city.plaza-design", P.plazaVariant == 0 ? "arcade" : P.plazaVariant == 1 ? "twin-fountains" : "market");
	// One home design and one facing for the whole map: every home is a translation of the first.
	P.homeDesign = c.bounded("faulted-city-home", int(kHomes.size()));
	P.homeFacing = c.bounded("faulted-city-home", 4);
	c.telemetry.choice("faulted-city.home-design", kHomes[P.homeDesign].name);
	c.telemetry.measure("faulted-city.home-facing", P.homeFacing);

	P.fault = rasterizeBoundaries(districts, edges, o.faultWidth / 2);
	// Every district stands at its own height: the higher side of every fault is its scarp.
	std::vector<int> elevation(P.districtCount);
	for (int d = 0; d < P.districtCount; ++d)
		elevation[d] = int(c.bounded("faulted-city-uplift", 1 << 16)) * P.districtCount + d;
	P.faultSteps = stepsFrom(t, P.fault);
	P.street.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int sx = P.sourceX(i) % kPitch, sy = P.sourceY(i) % kPitch;
		P.street[i] = sx < 3 || sx >= 29 || sy < 3 || sy >= 29;
	}
	// The district across the fault from each fault tile: the nearest tile labelled otherwise.
	const int across = o.faultWidth / 2 + 3;
	std::vector<int> other(n, -1);
	for (int i = 0; i < n; ++i)
	{
		if (!P.fault[i]) continue;
		const int x = t.remainderX(i), y = i / t.w;
		int best = INT_MAX;
		for (int dy = -across; dy <= across; ++dy)
			for (int dx = -across; dx <= across; ++dx)
			{
				const int j = t.at(x + dx, y + dy), d2 = dx * dx + dy * dy;
				if (d2 < best && P.district[j] != P.district[i]) { best = d2; other[i] = P.district[j]; }
			}
	}
	std::vector<unsigned char> interior(n, 0);
	for (int i = 0; i < n; ++i) interior[i] = !P.fault[i];
	const auto depth = distanceSquaredTo(t, interior);
	const auto fray = periodicNoise(t.w, t.h, 8, c.stream("faulted-city-scarp"));
	P.scarp.assign(n, 0);
	std::vector<int> raised(P.districtCount, 0), lowered(P.districtCount, 0);
	for (int i = 0; i < n; ++i)
	{
		if (!P.fault[i] || other[i] < 0) continue;
		const int d = P.district[i];
		const bool high = elevation[d] > elevation[other[i]];
		(high ? raised : lowered)[d] = 1;
		const int reach = kScarpDepth + (fray[i] > 32768 ? 1 : 0);
		P.scarp[i] = high && depth[i] <= std::int64_t(reach) * reach;
	}
	int highQuarters = 0, lowQuarters = 0;
	for (int d = 0; d < P.districtCount; ++d)
	{
		highQuarters += raised[d] && !lowered[d];
		lowQuarters += lowered[d] && !raised[d];
	}
	c.telemetry.measure("faulted-city.districts.raised", highQuarters);
	c.telemetry.measure("faulted-city.districts.sunken", lowQuarters);

	// RAMPS where a source avenue meets a district's own scarp, spread round the district. A gate
	// is a street tile whose every neighbour is street or fault, so its approach is a full avenue.
	P.ramp.assign(n, 0);
	P.junction.assign(n, 0);
	std::vector<std::vector<int>> eligible(P.districtCount);
	for (int i = 0; i < n; ++i)
	{
		if (P.fault[i] || !P.street[i]) continue;
		const int x = t.remainderX(i), y = i / t.w, d = P.district[i];
		bool avenue = true, beside = false;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				avenue = avenue && (P.street[j] || P.fault[j]);
				beside = beside || (P.scarp[j] && P.district[j] == d);
			}
		if (avenue && beside) eligible[d].push_back(i);
	}
	for (int d = 0; d < P.districtCount; ++d)
	{
		if (!raised[d]) continue;
		std::vector<int> selected;
		for (int k = 0; k < 2 + o.junctions; ++k)
		{
			int best = -1, bestGap = -1;
			for (int i : eligible[d])
			{
				int gap = INT_MAX;
				for (int s : selected) gap = std::min(gap, t.dist2(t.remainderX(i), i / t.w, t.remainderX(s), s / t.w));
				if (gap > bestGap) { best = i; bestGap = gap; }
			}
			if (best < 0 || bestGap < 144) break;
			selected.push_back(best);
			P.gates.push_back(best);
			c.telemetry.measure("faulted-city.junction.x", t.remainderX(best), P.gates.size() - 1);
			c.telemetry.measure("faulted-city.junction.y", best / t.w, P.gates.size() - 1);
		}
		if (selected.size() < 2) { P.failure = "The city has too few usable junctions."; return P; }
		c.telemetry.measure("faulted-city.junctions.actual", selected.size(), d);
	}
	for (int gate : P.gates)
	{
		const int gx = t.remainderX(gate), gy = gate / t.w;
		for (int dy = -kRampRadius; dy <= kRampRadius; ++dy)
			for (int dx = -kRampRadius; dx <= kRampRadius; ++dx)
			{
				const int j = t.at(gx + dx, gy + dy);
				if (P.fault[j] && dx * dx + dy * dy <= kRampRadius * kRampRadius) P.ramp[j] = 1;
				if (std::max(std::abs(dx), std::abs(dy)) <= 4) P.junction[j] = 1;
			}
	}
	// SAG PONDS: water where the rift floor is broad, at the middle of some faults.
	P.floor.assign(n, 0);
	std::vector<unsigned char> notFloor(n, 0);
	for (int i = 0; i < n; ++i)
	{
		P.floor[i] = P.fault[i] && !P.scarp[i] && !P.ramp[i];
		notFloor[i] = !P.floor[i];
	}
	P.pond.assign(n, 0);
	int ponds = 0;
	const auto floorDepth = distanceSquaredTo(t, notFloor);
	for (int edge = 0; edge < int(districts.edges.size()); ++edge)
	{
		const bool wanted = c.bounded("faulted-city-ponds", 100) < 55;
		const auto e = districts.edgeEnds(edge, districts.edges[edge].cells[0]);
		const int cx = t.x(int(subtileTile((e.first.x + e.second.x) / 2)));
		const int cy = t.y(int(subtileTile((e.first.y + e.second.y) / 2)));
		if (!wanted) continue;
		// The broadest floor tile near the edge's middle.
		int best = -1;
		std::int64_t bestDepth = 0;
		for (int dy = -8; dy <= 8; ++dy)
			for (int dx = -8; dx <= 8; ++dx)
			{
				const int j = t.at(cx + dx, cy + dy);
				if (P.floor[j] && floorDepth[j] > bestDepth && !P.inPlaza(j)) { best = j; bestDepth = floorDepth[j]; }
			}
		// Two clear tiles of floor round the pond on every side keep the rift walkable past it.
		if (best < 0 || bestDepth < 25) continue;
		const double radius = std::min(4.0, std::sqrt(double(bestDepth)) - 2.0);
		const int bx = t.remainderX(best), by = best / t.w, r = int(std::ceil(radius));
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx)
				if (dx * dx + dy * dy <= radius * radius) P.pond[t.at(bx + dx, by + dy)] = 1;
		++ponds;
	}
	for (int i = 0; i < n; ++i) P.floor[i] = P.floor[i] && !P.pond[i];
	c.telemetry.measure("faulted-city.sag-ponds", ponds);

	// HOME CANDIDATES: source blocks lying whole inside one district, three tiles clear of its
	// faults and of the plaza.
	std::vector<int> count(size_t(P.districtCount) * P.blockCount, 0), origin(count.size(), -1);
	std::vector<unsigned char> spoiled(count.size(), 0);
	for (int i = 0; i < n; ++i)
	{
		const int key = P.district[i] * P.blockCount + P.blockOf(i);
		if (P.faultSteps[i] <= 3 || P.inPlaza(i)) { spoiled[key] = 1; continue; }
		++count[key];
		if (P.sourceX(i) % kPitch == 0 && P.sourceY(i) % kPitch == 0) origin[key] = i;
	}
	const HomeDesign &home = kHomes[P.homeDesign];
	int rawX = -1, rawY = -1;
	for (int y = 0; y < kPitch && rawX < 0; ++y)
		for (int x = 0; x < kPitch; ++x)
			if (localPoint(x, y, P.homeFacing) == std::pair{home.swarmX, home.swarmY}) { rawX = x; rawY = y; break; }
	for (int key = 0; key < int(count.size()); ++key)
		if (!spoiled[key] && count[key] == kPitch * kPitch && origin[key] >= 0)
		{
			const int o0 = origin[key];
			P.instances.push_back({key / P.blockCount, key % P.blockCount, o0,
								   t.at(t.remainderX(o0) + rawX, o0 / t.w + rawY)});
		}
	c.telemetry.measure("faulted-city.districts", P.districtCount);
	c.telemetry.measure("faulted-city.starts.candidates", P.instances.size());
	c.telemetry.measure("faulted-city.fault.corners", std::count(P.fault.begin(), P.fault.end(), 1));
	c.telemetry.measure("faulted-city.scarp.tiles", std::count(P.scarp.begin(), P.scarp.end(), 1));
	for (int k = 0; k < 5; ++k) c.telemetry.measure("faulted-city.blocks", counts[k], k);
	return P;
}

// The finished city for one choice of homes: every terrain, mask and plot, ready to populate.
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain, finished;
	std::vector<int> plotOf, crop, wheatPlot, woodPlot, wheatSteps, woodSteps, wheatEntry, woodEntry, plotCapacity;
	std::vector<int> homeOf, homeSites, homeRemnant;
	std::vector<unsigned char> street, fault, floor, ramp, wall, lawn, court, junction, renewable, rubble,
		plazaFruit, plazaVerge, rock;
	std::vector<std::vector<int>> plots;
	std::vector<int> gates, plazaCentres;
	int unthickened = 0, plazaPieces = 0;
	std::string failure;
};

Layout draw(const Plan &P, const std::vector<int> &sites)
{
	Layout L;
	L.t = P.t;
	const Torus &t = L.t;
	const int n = t.size();
	L.street = P.street;
	L.fault = P.fault;
	L.floor = P.floor;
	L.ramp = P.ramp;
	L.junction = P.junction;
	L.gates = P.gates;
	// Which instance each site names, and which home each (district, block) is.
	std::unordered_map<int, int> homeOfKey;
	for (int site : sites)
		for (const Instance &instance : P.instances)
			if (instance.swarm == site)
			{
				const int key = instance.district * P.blockCount + instance.block;
				if (homeOfKey.count(key)) { L.failure = "Two colonies chose one home block."; return L; }
				homeOfKey[key] = int(L.homeSites.size());
				L.homeSites.push_back(site);
			}
	if (L.homeSites.size() != sites.size()) { L.failure = "A colony has no home block."; return L; }
	const HomeDesign &home = kHomes[P.homeDesign];
	L.terrain.assign(n, GRASS);
	L.crop.assign(n, -1);
	L.plotOf.assign(n, -1);
	L.homeOf.assign(n, -1);
	L.homeRemnant.assign(sites.size(), -1);
	L.wall.assign(n, 0);
	L.lawn.assign(n, 0);
	L.court.assign(n, 0);
	L.rubble.assign(n, 0);
	L.plazaFruit.assign(n, 0);
	L.plazaVerge.assign(n, 0);
	std::vector<unsigned char> wallTiles(n, 0), plazaPaved(n, 0), colonnade(n, 0), remnantVertex(n, 0);
	for (int i = 0; i < n; ++i)
	{
		if (P.fault[i])
		{
			// The scarp is drawn on its vertices at its full depth; only the facades and
			// colonnades, a tile or two thick, go through wallVertices.
			L.terrain[i] = P.ramp[i] ? TRAIL : P.pond[i] ? WATER : P.scarp[i] ? RIDGE_ROCK : MUD;
			continue;
		}
		const int block = P.blockOf(i);
		const auto &b = P.blocks[block];
		const int rx = P.sourceX(i) % kPitch, ry = P.sourceY(i) % kPitch;
		if (const auto found = homeOfKey.find(P.district[i] * P.blockCount + block); found != homeOfKey.end())
		{
			// A home block, in the map's one design and facing.
			const int h = found->second;
			const auto [x, y] = localPoint(rx, ry, P.homeFacing);
			L.homeOf[i] = h;
			if (L.street[i]) { L.terrain[i] = TRAIL; continue; }
			if (home.court.has(x, y)) L.terrain[i] = TRAIL;
			// A court tile is one whose four corners are all court.
			L.court[i] = x >= home.court.x0 && x < home.court.x1 && y >= home.court.y0 && y < home.court.y1;
			bool farmed = false;
			for (const HomeFarm &farm : home.farms)
			{
				if (!farm.outer.has(x, y)) continue;
				farmed = true;
				if (x == farm.outer.x0 || x == farm.outer.x1 || y == farm.outer.y0 || y == farm.outer.y1 ||
					farm.divider.has(x, y))
					L.terrain[i] = DIRT;
				else if (farm.pond.has(x, y))
					L.terrain[i] = WATER;
				else
					L.crop[i] = farm.wheat.has(x, y) ? WHEAT : WOOD;
			}
			if (home.remnant.has(x, y)) { L.terrain[i] = RIDGE_ROCK; remnantVertex[i] = 1; }
			L.lawn[i] = !farmed && !home.court.has(x, y) && !home.remnant.has(x, y) && x >= 3 && x <= 28 && y >= 3 && y <= 28;
			// The quarry's seed: the lawn beside the remnant's middle, towards the court.
			if (x == home.remnant.x0 - 2 && y == (home.remnant.y0 + home.remnant.y1) / 2) L.homeRemnant[h] = i;
			continue;
		}
		const auto [x, y] = localPoint(rx, ry, b.facing);
		const bool reclaimed = b.kind == Ruin && b.variant != 0;
		const int civicKind = reclaimed ? Garden : b.kind;
		if (b.kind == Ruin && !reclaimed)
		{
			// A collapsed courtyard: its shell is stone rubble a colony can quarry, and heaps of
			// rubble lie in the courtyard (planted at the stone amount).
			L.lawn[i] = (x >= 4 && x <= 27 && y >= 4 && y <= 27) && !(x >= 9 && x <= 22 && y >= 9 && y <= 22);
			L.rubble[i] = x >= 9 && x <= 22 && y >= 9 && y <= 22;
			const bool doorX = x >= 12 && x <= 19, doorY = y >= 12 && y <= 19;
			L.wall[i] = ((x == 5 || x == 6 || x == 25 || x == 26) && y >= 5 && y <= 26 && !doorY) ||
				((y == 5 || y == 6 || y == 25 || y == 26) && x >= 5 && x <= 26 && !doorX);
		}
		else
		{
			// Distinct civic spaces: a market's irrigated garden, a deep productive garden,
			// a long reservoir with a bank field, and a formal orchard with side lawns.
			const int right = 27;
			const int top = civicKind == Garden ? 12 : civicKind == Reservoir ? 10 : 13;
			const int divider = 17;
			if (x >= 4 && x <= right && y >= top && y <= 27)
			{
				if (x == 4 || x == right || y == top || y == 27 || x == divider)
					L.terrain[i] = DIRT;
				// Broad rear reservoirs irrigate both crops without consuming the street.
				// Wood has a full compartment rather than a two-tile strip.
				else if (y >= 21 || (b.variant == 1 && x >= 6 && x <= 10 && y >= 19) ||
					(b.variant == 2 && x >= 21 && x <= 25 && y >= 19))
					L.terrain[i] = WATER;
				else L.crop[i] = x < divider ? WHEAT : WOOD;
			}
			// A small sealed service court inside the large farm puts an inn beside the
			// grain without asking an AI to feed across the street and the plot's cap.
			if (civicKind == Reservoir && x >= 6 && x <= 13 && y >= 11 && y <= 18)
			{
				L.crop[i] = -1;
				L.terrain[i] = (x == 6 || x == 13 || y == 11 || y == 18) ? DIRT : GRASS;
			}
			L.lawn[i] = x >= 4 && x <= 27 && y >= 4 &&
				(y < top - 1 || (civicKind == Square && x > right + 1));
			// A market square is paved, with a fountain in the middle and fallen columns.
			if (civicKind == Square && x >= 7 && x <= 24 && y >= 4 && y <= top - 2)
			{
				const int fx = x - 15, fy = y - (top + 2) / 2;
				L.terrain[i] = fx * fx * 4 + fy * fy * 4 <= 25 ? WATER : TRAIL;
				L.lawn[i] = 0;
			}
			if (civicKind == Square)
				L.wall[i] = (y == 5 && (x == 5 || x == 25)) ||
					(x == 26 && y >= 6 && y <= 10 && b.variant == 2);
			if (civicKind == Orchard && x >= 8 && x <= 24 && x % 7 == 1 && (y == 6 || y == 11))
				L.crop[i] = CHERRY + block % 3;
			if (reclaimed)
			{
				// Retain substantial ruined hall/corner frontages around the reclaimed
				// garden, with broad gaps and an open lawn between the upper wings.
				const bool sideDoor = b.variant == 1 ? (y >= 12 && y <= 19) : (y >= 8 && y <= 15);
				const bool endDoor = b.variant == 1 ? (x >= 8 && x <= 15) : (x >= 16 && x <= 23);
				// Never over the garden's cap or crops: only on lawn and verge.
				wallTiles[i] = L.terrain[i] == GRASS && L.crop[i] < 0 &&
					((((x >= 4 && x <= 6) || (x >= 25 && x <= 27)) && y >= 4 && y <= 11) ||
					 (x >= 27 && x <= 28 && y >= 5 && y <= 26 && !sideDoor) ||
					 (y >= 27 && y <= 28 && x >= 5 && x <= 26 && !endDoor));
			}
		}
		// Surviving facades make the old avenues visible. They are separate block outlines,
		// not a continuous fortification around a district.
		if (b.kind != Ruin && L.terrain[i] == GRASS && L.crop[i] < 0)
		{
			const bool opening = (x >= 12 && x <= 20) || (y >= 11 && y <= 19);
			if (!opening && ((x >= 4 && x <= 5 && y >= 4 && y <= 27) ||
				(x >= 26 && x <= 27 && y >= 4 && y <= 27) ||
				(b.kind != Garden && y >= 4 && y <= 5 && x >= 4 && x <= 27))) wallTiles[i] = 1;
		}
		if (L.street[i]) L.terrain[i] = TRAIL;
		// THE PLAZA replaces whole source blocks across its fault: a paved promenade with fountains,
		// a fruit avenue either side, paved walks, and a grass verge with broken colonnades.
		if (P.inPlaza(i))
		{
			const auto [u, v] = P.plazaFrame(i);
			const int av = std::abs(v), au = std::abs(u);
			L.crop[i] = -1; L.wall[i] = 0; L.lawn[i] = 0; L.rubble[i] = 0; wallTiles[i] = 0;
			L.street[i] = av >= 13 || u < -45 || u >= 45;
			L.terrain[i] = TRAIL;
			plazaPaved[i] = 1;
			if (!L.street[i] && au <= 42)
			{
				if (av >= 3 && av <= 6)
				{
					L.terrain[i] = GRASS; plazaPaved[i] = 0;
					L.plazaFruit[i] = av == 4 && (u + 48) % 4 == 1;
				}
				if (av >= 9 && av <= 12)
				{
					L.terrain[i] = GRASS; plazaPaved[i] = 0;
					L.plazaVerge[i] = 1;
					colonnade[i] = av >= 10 && av <= 11 && (P.plazaVariant == 0 ? (u + 48) % 10 < 6 : (u + 48) % 8 < 4);
					wallTiles[i] = colonnade[i];
				}
			}
			const bool fountain = (P.plazaVariant == 0 && au <= 3 && av <= 2) ||
				(P.plazaVariant == 1 && std::abs(au - 30) <= 3 && av <= 3) ||
				(P.plazaVariant == 2 && std::abs(au - 28) <= 2 && av <= 4) ||
				(P.plazaVariant == 2 && au <= 2 && av <= 2);
			if (fountain) { L.terrain[i] = WATER; plazaPaved[i] = 0; L.plazaFruit[i] = 0; }
			if (u == 0 && v == 0) L.plazaCentres.push_back(i);
		}
	}
	// The scarp keeps off every tile outside the fault, so the streets and lawns along its foot keep
	// all four corners, and every ramp tile is paved on all four.
	for (int i = 0; i < n; ++i)
	{
		if (L.terrain[i] != RIDGE_ROCK || !P.fault[i]) continue;
		const int x = t.remainderX(i), y = i / t.w;
		bool inside = true, ramp = false;
		for (int dy = -1; dy <= 0; ++dy)
			for (int dx = -1; dx <= 0; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				inside = inside && P.fault[j];
				ramp = ramp || P.ramp[j];
			}
		if (ramp) L.terrain[i] = TRAIL;
		else if (!inside) L.terrain[i] = MUD;
	}
	for (int i = 0; i < n; ++i)
		if (P.ramp[i])
		{
			const int x = t.remainderX(i), y = i / t.w;
			for (int c = 0; c < 4; ++c)
			{
				const int j = t.at(x + c % 2, y + c / 2);
				if (L.terrain[j] == MUD || L.terrain[j] == RIDGE_ROCK) L.terrain[j] = TRAIL;
			}
		}
	// A broken plaza must show both pieces: colonnades in at least two districts.
	{
		std::vector<int> pieces(P.districtCount, 0);
		for (int i = 0; i < n; ++i) if (colonnade[i]) ++pieces[P.district[i]];
		L.plazaPieces = int(std::count_if(pieces.begin(), pieces.end(), [](int k) { return k >= 6; }));
		if (L.plazaPieces < 2) { L.failure = "The fractured civic plaza did not survive."; return L; }
	}
	// THE WALLS to vertices, leaning away from streets, ramps, the rift floor and the plaza's
	// paving; ramps keep every tile. Then thickened to three vertices, never onto a street, a ramp,
	// a home, a crop, water, a pond, the plaza's avenue or the masonry.
	std::vector<unsigned char> open(n, 0), strict(n, 0);
	for (int i = 0; i < n; ++i)
	{
		open[i] = (L.street[i] && !P.fault[i]) || P.ramp[i] || P.floor[i] || plazaPaved[i];
		strict[i] = P.ramp[i];
	}
	const auto keepCorners = tileCorners(t, open);
	const auto rampCorners = tileCorners(t, strict);
	std::vector<unsigned char> reserved(n, 0);
	for (int i = 0; i < n; ++i)
		reserved[i] = L.homeOf[i] >= 0 || L.crop[i] >= 0 || L.wall[i] || L.plazaFruit[i] || P.pond[i] || P.ramp[i];
	const auto reservedCorners = tileCorners(t, reserved);
	const auto wallCorners = wallVertices(t, wallTiles, &open, &strict);
	for (int i = 0; i < n; ++i)
		if (wallCorners[i] && !keepCorners[i] && !rampCorners[i] && !reservedCorners[i] &&
			(L.terrain[i] == GRASS || L.terrain[i] == MUD || L.terrain[i] == DIRT))
			L.terrain[i] = RIDGE_ROCK;
	const auto canGrow = [&](int i)
	{
		return (L.terrain[i] == GRASS || L.terrain[i] == MUD) && !keepCorners[i] && !rampCorners[i] &&
			!reservedCorners[i] && !P.pond[i];
	};
	thickenRidges(L.terrain, t, canGrow, &L.unthickened, RIDGE_ROCK);
	// A home's remnant is drawn whole; it stays as drawn.
	for (int i = 0; i < n; ++i) if (remnantVertex[i]) L.terrain[i] = RIDGE_ROCK;
	L.rock.assign(n, 0);
	for (int i = 0; i < n; ++i) L.rock[i] = L.terrain[i] == RIDGE_ROCK;

	// Stone stands only on pure grass; masonry anything else spoiled is dropped from the design.
	const auto grass = pureTiles(L.terrain, t, GRASS);
	for (int i = 0; i < n; ++i)
	{
		L.wall[i] = L.wall[i] && grass[i];
		if (P.faultSteps[i] <= 5 && L.homeOf[i] < 0) L.lawn[i] = 0;
		L.lawn[i] = L.lawn[i] && grass[i];
		L.rubble[i] = L.rubble[i] && grass[i] && !L.wall[i];
		L.plazaFruit[i] = L.plazaFruit[i] && grass[i];
		L.plazaVerge[i] = L.plazaVerge[i] && grass[i];
	}
	// A translated plot crossing a fault becomes two independently sealed plots. Label the
	// final grass components rather than reusing source labels across a disconnected fragment.
	std::vector<unsigned char> cropGround(n, 0);
	for (int i = 0; i < n; ++i)
		cropGround[i] = grass[i] && (L.crop[i] == WHEAT || L.crop[i] == WOOD);
	L.plotOf = connectedRegions(cropGround, t.w, t.h, true, GridNeighbors::Eight);
	for (int i = 0; i < n; ++i)
		if (L.plotOf[i] >= 0)
		{
			if (L.plotOf[i] >= int(L.plots.size())) L.plots.resize(L.plotOf[i] + 1);
			L.plots[L.plotOf[i]].push_back(i);
		}
	// Propagate the identity of the nearest sustainable plot along real streets. A tiny
	// clipped garden cannot hide a useful full field behind it merely by being closer.
	const auto fertility = cropGrowthField(L.terrain, t);
	std::vector<unsigned char> walkOpen(n, 0);
	const auto walkable = walkableTiles(L.terrain, t);
	L.renewable.assign(n, 0);
	L.plotCapacity.assign(L.plots.size(), 0);
	for (int i = 0; i < n; ++i)
	{
		walkOpen[i] = walkable[i] && !L.wall[i] && !cropGround[i] && L.crop[i] < CHERRY && !L.plazaFruit[i];
		L.renewable[i] = cropGround[i] && fertility.at(t.remainderX(i), i / t.w) > 0;
		if (L.renewable[i]) ++L.plotCapacity[L.plotOf[i]];
	}
	const auto cropAccess = [&](int resource, std::vector<int> &owner, std::vector<int> &steps, std::vector<int> &entry)
	{
		owner.assign(n, -1); steps.assign(n, -1); entry.assign(n, -1);
		std::vector<int> queue;
		queue.reserve(n);
		for (int p = 0; p < int(L.plots.size()); ++p)
		{
			const auto &plot = L.plots[p];
			if (plot.empty() || L.crop[plot.front()] != resource) continue;
			std::uint64_t yield = 0;
			int capacity = 0;
			for (int i : plot) if (fertility.at(t.remainderX(i), i / t.w) > 0)
			{ yield += fertility.at(t.remainderX(i), i / t.w); ++capacity; }
			if (capacity < (resource == WHEAT ? 48 : 16) || (resource == WHEAT && yield < 3 * 65536ULL)) continue;
			for (int i : plot) if (fertility.at(t.remainderX(i), i / t.w) > 0)
			{ owner[i] = p; entry[i] = i; steps[i] = 0; queue.push_back(i); }
		}
		for (size_t q = 0; q < queue.size(); ++q)
		{
			const int i = queue[q];
			for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(t.remainderX(i) + dx, i / t.w + dy);
				if (steps[j] < 0 && walkOpen[j])
				{ steps[j] = steps[i] + 1; owner[j] = owner[i]; entry[j] = entry[i]; queue.push_back(j); }
			}
		}
	};
	cropAccess(WHEAT, L.wheatPlot, L.wheatSteps, L.wheatEntry);
	cropAccess(WOOD, L.woodPlot, L.woodSteps, L.woodEntry);

	// THE LOOK, painted last over the mechanics.
	L.finished = L.terrain;
	LookRoles roles;
	roles.tended = [&L](int i) { return L.homeOf[i] >= 0; };
	roles.farm = [&L](int i) { return L.crop[i] == WHEAT || L.crop[i] == WOOD; };
	roles.path = [](int) { return PathLook::Street; };
	paintClimate(L.finished, t, kCityLook, roles);
	return L;
}

// Each occupied quarter must open onto the rift in two places, through ramps or its low sides,
// and every ramp's approach must stay clear.
std::string colonyRoutes(const GenerationMap &map, const Layout &L, int teams)
{
	const Torus &t = L.t;
	const auto walking = walkableTiles(map);
	for (int gate : L.gates)
		for (int dy = -2; dy <= 2; ++dy)
			for (int dx = -2; dx <= 2; ++dx)
			{
				const int j = t.at(t.remainderX(gate) + dx, gate / t.w + dy);
				if ((L.ramp[j] || L.street[j]) && !walking[j])
					return "The city has a blocked junction at " + atTile(t, gate) + ".";
			}
	auto interior = walking;
	for (int i = 0; i < t.size(); ++i) if (L.fault[i]) interior[i] = 0;
	const auto components = connectedRegions(interior, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<int> mouths(t.size(), 0);
	for (int gate : L.gates) if (components[gate] >= 0) ++mouths[components[gate]];
	std::vector<unsigned char> lowSide(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		if (components[i] < 0 || L.junction[i]) continue;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(t.remainderX(i) + dx, i / t.w + dy);
				if (L.floor[j] && walking[j]) lowSide[components[i]] = 1;
			}
	}
	for (const auto &units : unitTilesByTeam(map, teams))
	{
		bool exits = false;
		for (int unit : units)
			if (components[unit] >= 0 && mouths[components[unit]] + 2 * lowSide[components[unit]] >= 2) exits = true;
		if (!exits) return "A city quarter has too few ways onto the rift.";
	}
	return {};
}

// Prove actual non-overlapping construction room, not just many overlapping anchors.
std::string colonyRoom(const GenerationMap &map, const Layout &L, int teams)
{
	const Torus t(map);
	const auto walking = walkableTiles(map);
	auto building = potentialBuildingTiles(map);
	for (int i = 0; i < t.size(); ++i) if (L.renewable[i]) building[i] = 0;
	const auto units = unitTilesByTeam(map, teams);
	for (int k = 0; k < teams; ++k)
	{
		if (units[k].empty()) return "A city colony has no workers.";
		const int site = units[k].front(), x = t.remainderX(site), y = site / t.w;
		const auto reach = reachFrom(t, units[k], walking, 24);
		auto local = tileMask(t, reach.tiles);
		for (int i : reach.tiles) local[i] = building[i];
		bool fits = false;
		// A city street is narrower than the packing grid's pitch. Check its alignment
		// instead of mistaking one arbitrary grid phase for the available building room.
		for (int oy = 0; oy < 6 && !fits; oy += 2)
			for (int ox = 0; ox < 6 && !fits; ox += 2)
			{
				const auto arrangement = arrangeBuildingGrid(t, local, walking,
					{{x - 24 + ox, y - 24 + oy, x + 25, y + 25}, 4, 4, 2, 1}, units[k]);
				fits = arrangement.failure.empty() && arrangement.footprints.size() >= 6;
			}
		if (!fits) return "A city home lacks building room.";
	}
	return {};
}

std::string colonyFood(const GenerationMap &map, const Layout &L, int teams,
					   const std::vector<int> *sites = nullptr)
{
	const auto open = walkableTiles(map);
	const auto units = unitTilesByTeam(map, teams);
	const auto fertility = Fertility::forMap(map, false);
	for (int k = 0; k < teams; ++k)
	{
		const auto reach = reachFrom(L.t, units[k], open, 24);
		std::vector<unsigned char> seen(L.t.size(), 0), plots(L.plots.size(), 0);
		int close = 0, total = 0;
		bool wood = false;
		for (size_t p = 0; p < reach.tiles.size(); ++p)
		{
			const int i = reach.tiles[p];
			for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = L.t.at(L.t.remainderX(i) + dx, i / L.t.w + dy);
				const int resource = map.getResource(L.t.remainderX(j), j / L.t.w).type;
				if (seen[j] || (resource != WHEAT && resource != WOOD) || reach.steps[p] + 1 > 24) continue;
				seen[j] = 1;
				if (resource == WOOD)
				{
					if (!sites || L.plotOf[j] == L.woodPlot[(*sites)[k]]) wood = true;
					continue;
				}
				// Starter guarantees must work on their own at zero ambient abundance.
				if (sites && L.plotOf[j] != L.wheatPlot[(*sites)[k]]) continue;
				++total;
				if (reach.steps[p] + 1 <= 12) ++close;
				if (L.plotOf[j] >= 0) plots[L.plotOf[j]] = 1;
			}
		}
		std::uint64_t yield = 0;
		for (size_t p = 0; p < plots.size(); ++p) if (plots[p])
			for (int i : L.plots[p]) yield += fertility.at(L.t.remainderX(i), i / L.t.w);
		if (!wood || close < 12 || total < 24 || yield < 3 * 65536ULL)
			return "A city home lacks reachable crops.";
	}
	return {};
}

bool populate(GenerationWorld &game, GenerationContext &c, const Layout &L,
			  const std::vector<int> &sites)
{
	const FaultedCityOptions o(c.request);
	const Torus &t = L.t;
	const int n = t.size();
	GenerationMap &map = game.map;
	writeVertices(map, L.finished);
	for (int k = 0; k < c.request.nbTeams; ++k) game.addTeam();
	// The collapsed courtyards' shells: permanent stone.
	for (int i = 0; i < n; ++i)
		if (L.wall[i]) map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1);
	// Each home's quarry, at the foot of its remnant wall, before any worker stands near it:
	// every home is a translation of the first, so every home gets the same face.
	int homeStone = 0;
	for (int h = 0; h < int(L.homeSites.size()); ++h)
	{
		const int at = L.homeRemnant[h];
		if (at < 0) return false;
		homeStone += plantQuarryFaces(map, t, {KitSeed{t.remainderX(at), at / t.w, 8}}, kHomeQuarry,
			[&](int i) { return L.homeOf[i] == h && L.lawn[i] && clearGround(map, t.remainderX(i), i / t.w); });
	}
	// The plaza's quarry, at the colonnades' feet on its verge.
	int plazaStone = 0;
	for (int centre : L.plazaCentres)
		plazaStone += plantQuarryFaces(map, t, {KitSeed{t.remainderX(centre), centre / t.w, 16}},
			kPlazaQuarry / int(std::max<size_t>(1, L.plazaCentres.size())),
			[&](int i) { return L.plazaVerge[i] && clearGround(map, t.remainderX(i), i / t.w); });
	const auto fertility = Fertility::forMap(map, false);
	std::vector<int> starterOwner(L.plots.size(), -1);
	for (int site : sites)
	{
		if (L.wheatPlot[site] >= 0)
		{
			if (starterOwner[L.wheatPlot[site]] >= 0) return false;
			starterOwner[L.wheatPlot[site]] = site;
		}
		if (L.woodPlot[site] >= 0) starterOwner[L.woodPlot[site]] = site;
	}
	int planted[2]{};
	for (const auto &plot : L.plots)
	{
		if (plot.empty()) continue;
		const int resource = L.crop[plot.front()];
		const bool starter = starterOwner[L.plotOf[plot.front()]] >= 0;
		const int amount = resource == WHEAT ? o.wheat : o.wood;
		const int capacity = L.plotCapacity[L.plotOf[plot.front()]];
		const int minimum = starter ? capacity : 0;
		// 100% seeds 80% of renewable capacity; the upper half of the slider fills
		// the remaining space gradually, reaching capacity only at 300%.
		const int fill = amount <= 100 ? amount * 160 : 16000 + (amount - 100) * 20;
		const int wanted = std::max(minimum, capacity * fill / 20000);
		int guaranteed = 0;
		if (starter)
		{
			const int label = L.plotOf[plot.front()];
			const int owner = starterOwner[label];
			const int seed = (resource == WHEAT ? L.wheatEntry[owner] : L.woodEntry[owner]);
			const auto eligible = [&](int i)
			{ return L.plotOf[i] == label && fertility.at(t.remainderX(i), i / t.w) > 0 && clearGround(map, t.remainderX(i), i / t.w); };
			// Put the first grain on the town-facing edge, not only at the deepest water.
			// Inns need several grains within their own working radius from the opening.
			if (seed >= 0) guaranteed = growPatch(map, t, seed, resource, minimum, eligible);
		}
		planted[resource == WOOD] += guaranteed + plantContainedPlot(map, t, plot, fertility, resource, std::max(0, wanted - guaranteed));
	}
	for (int k = 0; k < c.request.nbTeams; ++k)
	{
		const int site = sites[k];
		const int h = L.homeOf[site];
		if (h < 0) return false;
		std::vector<unsigned char> court(n, 0);
		for (int i = 0; i < n; ++i) court[i] = L.court[i] && L.homeOf[i] == h;
		if (!placeSettlement(game, c, k, court, {t.remainderX(site), site / t.w}, "faulted-city-starts")) return false;
	}
	// Rubble heaps in the collapsed courtyards, more of them at a higher stone amount.
	const auto heaps = periodicNoise(t.w, t.h, 4, c.stream("faulted-city-rubble"));
	const int threshold = 65535 - std::min(65535, o.stone * 260);
	int rubble = 0, fruits = 0;
	for (int i = 0; i < n; ++i)
		if (L.rubble[i] && heaps[i] > threshold && clearGround(map, t.remainderX(i), i / t.w))
		{ map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1); ++rubble; }
	for (int i = 0; i < n; ++i)
		if (L.crop[i] >= CHERRY && !L.junction[i] && clearGround(map, t.remainderX(i), i / t.w) &&
			c.bounded("faulted-city-fruit", 300) < unsigned(o.fruit))
		{ map.setResourceByIndex(t.remainderX(i), i / t.w, L.crop[i], 1); ++fruits; }
	// The plaza's fruit avenue: at least half its trees whatever the amount, every tree from 100%.
	int prize = 0;
	const int avenue = std::clamp(o.fruit, 50, 100);
	int tree = 0;
	for (int i = 0; i < n; ++i)
		if (L.plazaFruit[i] && clearGround(map, t.remainderX(i), i / t.w))
		{
			const int type = CHERRY + (tree++ % 3);
			if (c.bounded("faulted-city-plaza-fruit", 100) < unsigned(avenue))
			{ map.setResourceByIndex(t.remainderX(i), i / t.w, type, 1); ++prize; }
		}
	seedAlgae(map, c, t, "faulted-city-algae", o.algae, AlgaeBand::anyWater());
	if (const auto routes = colonyRoutes(map, L, c.request.nbTeams); !routes.empty())
	{ c.telemetry.choice("faulted-city.rejected-check", "routes"); c.detail = routes; return false; }
	if (const auto food = colonyFood(map, L, c.request.nbTeams, &sites); !food.empty())
	{ c.telemetry.choice("faulted-city.rejected-check", "food"); c.detail = food; return false; }
	if (const auto room = colonyRoom(map, L, c.request.nbTeams); !room.empty())
	{ c.telemetry.choice("faulted-city.rejected-check", "room"); c.detail = room; return false; }
	c.telemetry.measure("faulted-city.materials.wheat", planted[0]);
	c.telemetry.measure("faulted-city.materials.wood", planted[1]);
	c.telemetry.measure("faulted-city.materials.rubble", rubble);
	c.telemetry.measure("faulted-city.materials.fruit", fruits);
	c.telemetry.measure("faulted-city.quarry-faces.home", homeStone);
	c.telemetry.measure("faulted-city.quarry-faces.plaza", plazaStone);
	c.telemetry.measure("faulted-city.plaza.fruit", prize);
	c.telemetry.measure("faulted-city.walls.unthickened-vertices", L.unthickened);
	c.telemetry.measure("faulted-city.plaza.surviving-districts", L.plazaPieces);
	if (c.telemetry.enabled())
	{
		// Each colony's walk to the nearer half of the plaza.
		const auto walking = walkableTiles(map);
		std::vector<unsigned char> centre(n, 0);
		for (int i : L.plazaCentres) centre[i] = 1;
		const auto steps = stepsFrom(t, centre, walking);
		const auto units = unitTilesByTeam(map, c.request.nbTeams);
		for (int k = 0; k < c.request.nbTeams; ++k)
			c.telemetry.measure("faulted-city.plaza.walk", units[k].empty() ? -1 : steps[units[k].front()], k);
	}
	return true;
}

std::string qualityFailure(const StartQualityReport &q)
{
	for (const auto &s : q.colonies)
		if (s.wheatDistance < 0 || s.wheatDistance > 12 || s.woodDistance < 0 || s.woodDistance > 24 ||
			s.materials[materialIndex(MaterialId::Stone)].nearestDistance < 0 || s.materials[materialIndex(MaterialId::Stone)].nearestDistance > 32 || s.buildSites < 48 ||
			s.distanceBands[0].depositTiles[materialIndex(MaterialId::Food)] < 12 || s.materials[materialIndex(MaterialId::Food)].catchmentDeposits < 24)
			return "A city home lacks reachable crops or building room.";
	if (q.fairness < 0.80) return "The city's homes are too unequal; try another seed.";
	return {};
}

GenerationRequest layoutRequest(const GenerationRequest &request, int attempt)
{
	auto r = request;
	if (attempt) r.seed = GenerationContext::deriveSeed(request.seed,
		"faulted-city-layout-" + std::to_string(attempt));
	return r;
}

bool generate(GenerationWorld &game, GenerationContext &c)
{
	// Search whole, unmodified city layouts when fractures leave too few intact blocks for homes.
	for (int attempt = 0; attempt < kLayoutAttempts; ++attempt)
	{
		const auto request = layoutRequest(c.request, attempt);
		auto canonical = request;
		for (const char *key : {"wheat-amount", "wood-amount", "stone-amount", "algae-amount", "fruit-amount"})
			canonical.options[key] = 300;
		GenerationContext trial(canonical, c.telemetry.enabled());
		c.stage = "faulted city layout";
		const Plan P = plan(request, trial);
		if (!P.failure.empty()) { c.detail = P.failure; continue; }
		std::vector<RankedSite> candidates;
		for (const Instance &instance : P.instances) candidates.push_back({instance.swarm, 1.0});
		std::vector<std::vector<int>> proposals;
		for (int p = 0; p < 6 && int(candidates.size()) >= request.nbTeams; ++p)
		{
			auto sites = spreadRankedSites(P.t, candidates, request.nbTeams, 30,
				trial.bounded("faulted-city-proposals", candidates.size()));
			if (int(sites.size()) < request.nbTeams) continue;
			dealStarts(trial, sites, "faulted-city-deal");
			proposals.push_back(std::move(sites));
		}
		trial.telemetry.measure("faulted-city.starts.proposals", proposals.size());
		if (proposals.empty())
		{
			c.detail = "The city has too few intact blocks for every colony's home.";
			c.telemetry.fallback("faulted-city.layout.rejected", c.detail, attempt);
			continue;
		}
		c.stage = "faulted city settlements";
		const auto build =
			[&](GenerationWorld &g, GenerationContext &probe, const std::vector<int> &sites)
		{
			const Layout L = draw(P, sites);
			if (!L.failure.empty()) { probe.detail = L.failure; return false; }
			return populate(g, probe, L, sites);
		};
		const auto choice = chooseScoredSettlements(trial, proposals, build, qualityFailure);
		if (choice.selected < 0)
		{
			c.detail = choice.failure;
			c.telemetry.fallback("faulted-city.layout.rejected", choice.failure, attempt);
			continue;
		}
		// Resource controls furnish the selected city; they never choose a different
		// terrain merely because an abundance changes a placement score.
		GenerationContext actual(request, c.telemetry.enabled());
		if (!build(game, actual, choice.sites))
		{ c.telemetry.replay(actual.telemetry); c.detail = actual.detail; return false; }
		c.bootX = actual.bootX; c.bootY = actual.bootY;
		c.telemetry.replay(trial.telemetry);
		c.telemetry.replay(actual.telemetry);
		c.telemetry.measure("faulted-city.layout.attempts", attempt + 1);
		c.detail.clear();
		return true;
	}
	return false;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (const auto error = denseColonySizeFailure(r); !error.empty())
		return error;
	if (r.wDec < 8 || r.hDec < 8 || r.wDec > 9 || r.hDec > 9 || r.nbTeams < 1 ||
		r.nbTeams > TEAM_MAX_COUNT)
		return "Too many colonies for this map; use a bigger map or fewer colonies.";
	return {};
}

std::string validateWorld(const GenerationWorld &game, const GenerationContext &c)
{
	const GenerationMap &map = game.map;
	const int teams = c.request.nbTeams;
	const auto units = unitTilesByTeam(map, teams);
	Layout L;
	bool matched = false;
	for (int attempt = 0; attempt < kLayoutAttempts && !matched; ++attempt)
	{
		const auto request = layoutRequest(c.request, attempt);
		GenerationContext replay(request);
		const Plan P = plan(request, replay);
		if (!P.failure.empty() || P.t.w != map.getW() || P.t.h != map.getH()) continue;
		// Each colony's home is the intact block its workers stand in.
		std::unordered_map<int, int> instanceOf;
		for (int k = 0; k < int(P.instances.size()); ++k)
			instanceOf[P.instances[k].district * P.blockCount + P.instances[k].block] = k;
		std::vector<int> sites;
		for (int k = 0; k < teams; ++k)
		{
			if (units[k].empty()) break;
			const int u = units[k].front();
			const auto found = instanceOf.find(P.district[u] * P.blockCount + P.blockOf(u));
			if (found == instanceOf.end()) break;
			sites.push_back(P.instances[found->second].swarm);
		}
		if (int(sites.size()) != teams) continue;
		Layout candidate = draw(P, sites);
		if (!designMismatch(candidate, map, "faulted city").empty()) continue;
		matched = true;
		for (int i = 0; i < candidate.t.size() && matched; ++i)
			matched = candidate.finished[i] == map.vertexTerrainAt(candidate.t.remainderX(i), i / candidate.t.w);
		if (matched) L = std::move(candidate);
	}
	if (!matched) return "The city terrain no longer matches its design.";
	const Torus &t = L.t;
	if (const auto plots = containedPlotsMismatch(map, t, L.plotOf); !plots.empty()) return plots;
	if (const int bad = wallTerrainMismatch(map, t, L.rock, TerrainGroup::Ridges); bad >= 0)
		return "A scarp or facade is not Ridges at " + atTile(t, bad) + ".";
	std::vector<int> homeStone(L.homeSites.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const bool stone = map.getResource(t.remainderX(i), i / t.w).type == STONE;
		if (L.wall[i] && !stone)
			return "The city has lost part of its rubble masonry at " + atTile(t, i) + ".";
		if (L.homeOf[i] >= 0 && stone) ++homeStone[L.homeOf[i]];
		if (L.floor[i] && map.isResource(t.remainderX(i), i / t.w))
			return "A rift floor is obstructed at " + atTile(t, i) + ".";
	}
	// Every home keeps its whole quarry face, the same for every home.
	for (int h = 0; h < int(homeStone.size()); ++h)
		if (homeStone[h] != homeStone[0] || homeStone[h] < kHomeQuarry / 2)
			return "A home has lost part of its quarry face.";
	if (const auto access = startingAccessFailure(map, teams,
		{{MaterialId::Food, 12, "food"}, {MaterialId::Wood, 24, "wood"}, {MaterialId::Stone, 32, "stone"}}, 48); !access.empty()) return access;
	if (const auto routes = colonyRoutes(map, L, teams); !routes.empty()) return routes;
	if (const auto food = colonyFood(map, L, teams); !food.empty()) return food;
	if (const auto room = colonyRoom(map, L, teams); !room.empty()) return room;
	const auto walk = walkFromFirstColony(map, teams, "the city", "over its ramps");
	if (!walk.error.empty()) return walk.error;
	for (int gate : L.gates)
		if (walk.steps[gate] < 0) return "The city has an unreachable junction at " + atTile(t, gate) + ".";
	return {};
}
} // namespace

FaultedCityOptions::FaultedCityOptions(const GenerationRequest &r)
	: displacement(r.option("fault-displacement")), faultWidth(r.option("fault-width")),
	  ruins(r.option("ruin-density")), junctions(r.option("surviving-junctions")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount")) {}
GeneratorDefinition faultedCityDefinition()
{
	return {"faulted-city",
			64,
			"The Faulted City",
			6, // 6: Faulted City 2.0, rifts, ramps, paved streets, Ridges facades, home blocks, plaza prize
			false,
			{GeneratorControl{"fault-displacement", "Fault displacement", 4, 14, 1, 10,
							  ControlGroup::Terrain}
				 .withSearchRange(6, 12),
			 GeneratorControl{"fault-width", "Fault width", 8, 16, 2, 12, ControlGroup::Terrain}
				 .withSearchRange(10, 14),
			 GeneratorControl{"ruin-density", "Ruin density", 20, 60, 5, 35, ControlGroup::Layout}
				 .withSearchRange(25, 45),
			 GeneratorControl::choice("surviving-junctions", "Surviving junctions",
									  {"Few", "Normal", "Many"}, 1, ControlGroup::Layout)
				 .withSearchValues({0, 1, 2}),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validateRequest,
			validateWorld,
			{"terrain:urban", "feature:stone-walls", "feature:lakes", "style:expansion"}};
}
