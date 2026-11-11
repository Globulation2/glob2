// SPDX-License-Identifier: GPL-3.0-or-later
#include "GenerationWork.h"
#include "GenerationNumeric.h"
#include "FortsGenerator.h"
#include "Climates.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "Growth.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Roads.h"
#include "Room.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Walls.h"
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
using namespace MapGeneration;

// Forts: granite enclosures overlooking a central-European bocage of hedged fields, river valleys,
// forests and rocky uplands. Two opposite gates give each home a defensible front and a second way
// out. Lanes run from the gates through the hedgerows to market towns and ford the rivers. The
// brick-paved bailey stays clear while two loam beds, edged with barren ground and watered, supply
// the opening economy. Expansion means leaving the enclosure for the fields and orchards beyond.
//
// Revision 9 draws the map from the terrain catalogue (TerrainGroup.h) where earlier revisions stood
// stone on grass. The ramparts are permanent walls: granite (Obstacles) along the curtains, and rock
// that towers shoot over (Ridges) at the bastions, towers and gate fronts, where a defender's towers
// should cover the approach. Their stone comes back as quarry faces in the bailey, and the uplands'
// rocky crowns are granite tors with faces at their feet. Hedgerows (Obstacles) enclose the fields,
// each with a field gate, so the countryside is a network of routes rather than an open plain.
// Each map has one fixed look (kFortsLook), not a climate: this is the granite-and-brick country.
namespace
{
// Every fort on a map is built to one design, so the colonies start with the same courtyard, plots
// and walls, but the design changes from map to map: a map that always drew its yard on the left
// and its plots on the right read as one stamp (maintainer review 2026-09-16). A design is an
// interior layout, a wall style, and a quarter turn and mirror of the whole fort, plus the plan
// of the market towns.
enum class FortLayout
{
	Bailey,      // yard on one side, wheat and wood plots side by side on the other
	Diagonal,    // wheat and wood in opposite corners, the yard in the other two
	LongGardens, // a long wheat garden down most of one wall, wood along half the other
	Chapter,     // wheat in the two corners of one wall, wood in the middle of the other
	Count
};
enum class FortWalls
{
	Bastions,  // square corner bastions
	Gatehouse, // corner bastions, towers flanking both gates and one mid-wall tower each side
	Round,     // round towers at the corners and flanking the gates
	Count
};
enum class TownPlan
{
	Crossroads, // two streets crossing: four plots
	Green,      // a ring street round a small central green, cut by one through street
	HighStreet, // one main street with a back lane crossing it either side
	Count
};
const char *const kLayoutNames[] = {"bailey", "diagonal", "long-gardens", "chapter"};
const char *const kWallNames[] = {"bastions", "gatehouse", "round-towers"};
const char *const kTownNames[] = {"crossroads", "green", "high-street"};
struct FortDesign
{
	FortLayout layout = FortLayout::Bailey;
	FortWalls walls = FortWalls::Bastions;
	TownPlan town = TownPlan::Crossroads;
	// The fort's own frame, (u, v): u runs gate to gate. A quarter turn puts the gates north and
	// south instead of east and west; the flips mirror the layout across either axis.
	bool turn = false, flipU = false, flipV = false, townTurn = false;
	int x(int u, int v) const { return turn ? (flipV ? -v : v) : (flipU ? -u : u); }
	int y(int u, int v) const { return turn ? (flipU ? -u : u) : (flipV ? -v : v); }
};
/// A sand-rimmed crop plot in a fort's frame: the plot spans a in [a0, a1] and b in [b0, b1], and
/// sits at u = su * a, v = sv * b. Its water lies along the far a edge (waterOnA) or the near b
/// edge, the side toward the gate road: crops regrow from water only where no sand lies directly
/// opposite it, so a plot's crops never sit between its water and the road's broad sand.
struct FortPlot
{
	int a0, a1, b0, b1, su, sv;
	bool waterOnA, timber;
};
struct FortPlan
{
	std::vector<FortPlot> plots;
	int settleU, settleV;     // the colony's starting point, in the yard
	int orchardU, orchardV;   // the first household orchard seed
	int orchardDu, orchardDv; // the step to the next
};
FortPlan fortPlan(FortLayout layout, int r)
{
	// A plot watered along its long side needs ten corners of depth: its rim, four corners of
	// water, the beach the shoreline pass takes from the grass, and four rows of crops.
	// Plots stop at r - 5, a tile short of the bastions and towers: a tile of stone needs all four
	// of its corners grass, and mirroring a plot onto a fort's other side moves its sand rim one
	// corner nearer the stone than it sits on the side it was drawn for.
	const int e = r - 5;
	switch (layout)
	{
	case FortLayout::Diagonal:
		return {{{2, e, 4, e, 1, -1, true, false}, {2, e, 4, e, -1, 1, true, true}},
				-10, -9, 6, e, 4, 0};
	case FortLayout::LongGardens:
		return {{{6 - e, e, e - 10, e, 1, -1, false, false}, {2, e, e - 10, e, 1, 1, false, true}},
				-10, 9, -r + 6, r - 3, 4, 0};
	case FortLayout::Chapter:
		return {{{e - 10, e, 4, e, 1, -1, false, false},
				 {e - 10, e, 4, e, -1, -1, false, false},
				 {-7, 7, e - 10, e, 1, 1, false, true}},
				0, -10, -e, 6, 0, 4};
	default:
		// Separate irrigated food and wood plots, each enclosed by sand. The other half of the
		// fort remains a large uninterrupted construction yard, with room for upgraded buildings.
		return {{{2, e, 4, e, 1, -1, true, false}, {2, e, 4, e, 1, 1, true, true}},
				-10, -9, -r + 6, e, 4, 0};
	}
}
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<int> homeOf, plot, uplands;
	// fort: the fort (0 to teams - 1) whose ramparts enclose a tile, or -1.
	std::vector<int> fort;
	std::vector<unsigned char> wall, roads, gates, buffer, towns;
	// ridge: the rampart tiles towers shoot over (bastions, towers, gate fronts); rim: the plots'
	// barren edging; ford: road corners laid over water; hedge, hedgeGap and crown: the bocage's
	// hedgerows, its field gates and the uplands' granite crowns, all drawn in sketchTerrain.
	std::vector<unsigned char> ridge, rim, ford, hedge, hedgeGap, crown;
	std::vector<int> crownSeeds; // one tile at the heart of each crown, for its quarry face
	std::vector<ShapePoint> homes, villages;
	FortDesign design;
	int r = 0; // the forts' half-width (home-size)
	std::string failure;
};

// The one look of the forts' country (see the header): granite curtains and tors, ridge rock on the
// bastions, brick in the baileys and market towns, overgrown lanes through the fields, boardwalk
// fords, loam garden beds edged with bare earth. The hedgerows are drawn as HEDGE directly.
constexpr ClimateLook kFortsLook = {"forts",      GRANITE_TORS, RIDGE_ROCK,     LUSH_GRASS, GRASS,
									PALE_SAND,    BRICK_PATH,   BRICK_PATH,     OVERGROWN_ROAD,
									BRICK_PATH,   BOARDWALK,    DIRT,           DEEP_WATER,
									MUD,          LOAM,         OUTCROP,        LUSH_GRASS};
constexpr int kVerge = 4;          // grass kept at a rampart's foot for towers and quarry faces
constexpr double kBailey = 4.5;    // the paved square round each swarm
constexpr int kQuarryFace = 12;    // stone tiles in one quarry face
constexpr int kFortFaces = 2;      // quarry faces in each bailey, at the foot of its side walls
constexpr int kFieldSpacing = 20;  // the bocage's field size, in tiles
constexpr int kCrownShare = 45;    // one country tile in this many is granite crown
constexpr int kFieldGates = 35;    // percent of hedged borders with a field gate of their own

// splitmix64: a fixed integer hash, so a field border's hedge is the same on every platform.
std::uint64_t mix(std::uint64_t z)
{
	z += 0x9e3779b97f4a7c15ULL;
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

// The connected pieces of `mask`, eight neighbours, in index order of their first tile.
std::vector<std::vector<int>> pieces(const Torus &t, const std::vector<unsigned char> &mask)
{
	std::vector<std::vector<int>> out;
	std::vector<unsigned char> seen(t.size(), 0);
	for (int start = 0; start < t.size(); ++start)
	{
		::MapGeneration::generationCheckpoint();
		if (!mask.at(start) || seen.at(start))
			continue;
		std::vector<int> piece{start};
		seen.at(start) = 1;
		for (size_t q = 0; q < piece.size(); ++q)
		{
			const int x = t.remainderX(piece[q]), y = piece[q] / t.w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(x + dx, y + dy);
					if (mask.at(j) && !seen.at(j))
					{
						seen.at(j) = 1;
						piece.push_back(j);
					}
				}
		}
		out.push_back(std::move(piece));
	}
	return out;
}

bool riverCountry(Layout &L, const FortsOptions &o, const GenerationRequest &request,
				  GenerationContext &context)
{
	const Torus &t = L.t;
	const int r = o.homeSize;
	// The Rhine-like spine bends round the forts instead of being erased beneath them.
	// Clearance limits the width locally in crowded passes; even those retain a water core.
	const auto outside = [&]
	{
		std::vector<unsigned char> m(t.size());
		for (int i = 0; i < t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			m.at(i) = !L.buffer.at(i) && !L.towns.at(i);
		}
		return m;
	}();
	const auto space = clearance(t, outside);
	const double phase = context.bounded("forts-river", 65536) * 2 * kPi / 65536;
	const bool sideways = t.w > t.h || (t.w == t.h && context.bounded("forts-river", 2));
	const int span = sideways ? t.h : t.w, length = sideways ? t.w : t.h;
	const auto centreAt = [&](double along)
	{
		return span *
			   (0.5 + 0.18 * ::MapGeneration::Numeric::sin(2 * kPi * along / length + phase) +
				0.06 * ::MapGeneration::Numeric::sin(4 * kPi * along / length - phase));
	};
	std::vector<int> bends;
	for (int k = 0; k < 8; ++k)
	{
		::MapGeneration::generationCheckpoint();
		const int along = k * length / 8, across = int(centreAt(along));
		const int seed = seedNear(t, sideways ? along : across, sideways ? across : along, 2 * r,
								  [&](int i) { return space.at(i) >= 2; });
		if (seed < 0)
		{
			L.failure = "There is no room for the river between these forts.";
			return false;
		}
		bends.push_back(seed);
	}
	std::vector<unsigned char> river(t.size(), 0);
	int narrowest = o.riverWidth;
	const auto waterStroke = [&](const std::vector<int> &path, int wanted)
	{
		for (int i : path)
		{
			::MapGeneration::generationCheckpoint();
			const int radius = std::min(wanted / 2, space.at(i) - 1);
			narrowest = std::min(narrowest, 2 * radius + 1);
			for (int dy = -radius; dy <= radius; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -radius; dx <= radius; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					river.at(t.at(t.remainderX(i) + dx, i / t.w + dy)) = 1;
				}
			}
		}
	};
	for (size_t k = 0; k < bends.size(); ++k)
	{
		::MapGeneration::generationCheckpoint();
		const auto goal = tileMask(t, {bends.at((k + 1) % bends.size())});
		const auto path = cheapestWalk(t, GridNeighbors::Cardinal, {bends.at(k)}, goal,
									   [&](int, int to, int, int)
									   {
										   if (space.at(to) < 2)
											   return -1;
										   const int along = sideways ? t.remainderX(to) : to / t.w,
													 across = sideways ? to / t.w : t.remainderX(to);
										   return 10 + int(std::abs(across - centreAt(along)) / 3) +
												  L.uplands.at(to) / 8192;
									   });
		if (path.empty())
		{
			L.failure = "The river cannot pass between these forts.";
			return false;
		}
		waterStroke(path, o.riverWidth);
	}
	const int mainNarrowest = narrowest;
	std::vector<unsigned char> water(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		water.at(i) = river.at(i) || L.terrain.at(i) == WATER;
	}
	std::vector<int> queued(t.size(), 0);
	const auto basin = fractalNoise(t.w, t.h, 32, 3, context.stream("forts-lake-basins"));
	const int lakeTiles = std::max(48, t.size() / (16 * request.nbTeams));
	for (int k = 0; k < request.nbTeams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		for (int lake = 0; lake < o.lakes; ++lake)
		{
			::MapGeneration::generationCheckpoint();
			const double angle = context.bounded("forts-lake-sites", 65536) * 2 * kPi / 65536;
			const int ax = int(L.homes.at(k).x + (r + 24) * ::MapGeneration::Numeric::cos(angle));
			const int ay = int(L.homes.at(k).y + (r + 24) * ::MapGeneration::Numeric::sin(angle));
			int seed =
				seedNear(t, ax, ay, 2 * r, [&](int i) { return space.at(i) >= 5 && !water.at(i); });
			if (seed < 0)
				seed = seedNear(t, ax, ay, 2 * r,
								[&](int i) { return space.at(i) >= 2 && !water.at(i); });
			const bool longX = context.bounded("forts-lake-sites", 2);
			const int subject = k * o.lakes + lake;
			int placed = 0;
			if (seed >= 0)
			{
				placed = growWater(
					t, water, seed, lakeTiles,
					[&](int i) { return !L.buffer.at(i) && !L.towns.at(i) && !water.at(i); },
					[&](int i)
					{
						const long long dx = t.offsetX(t.remainderX(seed), t.remainderX(i)),
										dy = t.offsetY(seed / t.w, i / t.w);
						return (longX ? dx * dx + 3 * dy * dy : 3 * dx * dx + dy * dy) +
							   static_cast<long long>(lakeTiles) * basin.at(i) / 32768;
					},
					queued, subject + 1);
				// Tributaries connect the lake country to the main river through low ground.
				const auto stream = cheapestWalk(
					t, GridNeighbors::Cardinal, {seed}, river, [&](int, int to, int, int)
					{ return space.at(to) < 2 ? -1 : 10 + L.uplands.at(to) / 4096; });
				waterStroke(stream, 3);
			}
			context.telemetry.measure("forts.lake.target-water-corners", lakeTiles, subject);
			context.telemetry.measure("forts.lake.placed-water-corners", placed, subject);
			if (placed < lakeTiles)
				context.telemetry.fallback("forts.lake.capacity",
										   "The countryside limits this lake.", subject);
		}
	}
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (water.at(i) || river.at(i))
			L.terrain.at(i) = WATER;
	}

	context.telemetry.measure("forts.river.minimum-width-corners", mainNarrowest);
	context.telemetry.choice("forts.river.direction", sideways ? "east-west" : "north-south");
	return true;
}

void marketTowns(Layout &L, const FortsOptions &o, const GenerationRequest &request,
				 GenerationContext &context)
{
	const Torus &t = L.t;
	// Nearby market-town sites: four roomy building plots, a crossroads and a
	// sand boundary against crop spread. These are expansion sites, not an extra AI faction.
	std::vector<unsigned char> blocked(t.size(), 0), allWater(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		allWater.at(i) = L.terrain.at(i) == WATER;
		blocked.at(i) = L.buffer.at(i) || allWater.at(i);
	}
	const auto townRoom = stepsFrom(t, blocked), waterDistance = stepsFrom(t, allWater);
	const int townRadius = o.villageSize;
	for (int k = 0; k < request.nbTeams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		int site = -1;
		long long best = LLONG_MAX;
		for (int i = 0; i < t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (townRoom.at(i) < townRadius + 3)
				continue;
			bool separated = true;
			for (const auto &v : L.villages)
			{
				::MapGeneration::generationCheckpoint();
				if (t.chebyshev(t.remainderX(i), i / t.w, int(v.x), int(v.y)) < 2 * townRadius + 12)
					separated = false;
			}
			if (!separated)
				continue;
			const long long homeDistance =
				t.dist2(t.remainderX(i), i / t.w, int(L.homes.at(k).x), int(L.homes.at(k).y));
			const long long reach = o.homeSize + townRadius + 10;
			const long long desired = reach * reach;
			int rivalDistance = INT_MAX;
			for (int other = 0; other < request.nbTeams; ++other)
			{
				::MapGeneration::generationCheckpoint();
				if (other != k)
					rivalDistance =
						std::min(rivalDistance, t.dist2(t.remainderX(i), i / t.w, int(L.homes.at(other).x),
														int(L.homes.at(other).y)));
			}
			const long long balance =
				request.nbTeams > 1 ? 4 * std::max(0LL, homeDistance - rivalDistance) : 0;
			const long long score = std::abs(homeDistance - desired) + balance +
									40 * std::abs(waterDistance.at(i) - (townRadius + 4)) +
									L.uplands.at(i) / 64;
			if (score < best)
			{
				best = score;
				site = i;
			}
		}
		if (site < 0)
		{
			context.telemetry.fallback("forts.town.omitted", "No bank has enough room for a town.",
									   k);
			continue;
		}
		L.villages.push_back({double(t.remainderX(site)), double(site / t.w)});
		for (int dy = -townRadius; dy <= townRadius; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -townRadius; dx <= townRadius; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				const int i = t.at(t.remainderX(site) + dx, site / t.w + dy);
				L.towns.at(i) = 1;
				// Streets in the town's own frame; every plan joins the centre, where the roads
				// arrive, to the boundary street.
				const int across = std::abs(L.design.townTurn ? dy : dx),
						  along = std::abs(L.design.townTurn ? dx : dy),
						  ring = std::max(across, along), half = townRadius / 2;
				bool street = false;
				switch (L.design.town)
				{
				case TownPlan::Green:
					// A small green inside a ring street, a wide band of plots round it.
					street = across <= 1 || ring == 3 || ring == 4;
					break;
				case TownPlan::HighStreet:
					street = along <= 1 || across == half || across == half + 1;
					break;
				default:
					street = across <= 1 || along <= 1;
					break;
				}
				if (street || ring == townRadius)
				{
					L.roads.at(i) = 1;
					L.terrain.at(i) = SAND;
				}
			}
		}
	}
}

bool countryRoads(Layout &L, const std::vector<int> &exits)
{
	const Torus &t = L.t;
	// Roads follow valleys and reuse junctions. Two fort gates lead to different nearby towns
	// where possible; the towns join a connected network, with an extra first-to-last market road.
	const auto road = [&](int from, int to)
	{
		const auto goal = tileMask(t, {to});
		const auto path = cheapestWalk(t, GridNeighbors::Eight, {from}, goal,
									   [&](int, int next, int dx, int dy)
									   {
										   if (L.buffer.at(next) && !goal.at(next))
											   return -1;
										   if (L.towns.at(next) && !L.roads.at(next))
											   return -1;
										   if (L.roads.at(next))
											   return dx && dy ? 4 : 3;
										   return (dx && dy ? 14 : 10) + L.uplands.at(next) / 2048 +
												  (L.terrain.at(next) == WATER ? 70 : 0);
									   });
		if (path.empty())
			return false;
		for (int i : path)
		{
			::MapGeneration::generationCheckpoint();
			for (int dy = -1; dy <= 1; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -1; dx <= 1; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					const int j = t.at(t.remainderX(i) + dx, i / t.w + dy);
					if ((!L.buffer.at(j) && !L.towns.at(j)) || L.roads.at(j))
					{
						if (L.terrain.at(j) == WATER)
							L.ford.at(j) = 1;
						L.roads.at(j) = 1;
						L.terrain.at(j) = SAND;
					}
				}
			}
		}
		return true;
	};
	std::vector<int> hubs;
	for (auto v : L.villages)
	{
		::MapGeneration::generationCheckpoint();
		hubs.push_back(t.at(int(v.x), int(v.y)));
	}
	if (hubs.empty())
		hubs = exits;
	for (size_t k = 1; k < hubs.size(); ++k)
	{
		::MapGeneration::generationCheckpoint();
		int closest = 0;
		for (size_t j = 1; j < k; ++j)
		{
			::MapGeneration::generationCheckpoint();
			if (t.dist2(t.remainderX(hubs.at(k)), hubs.at(k) / t.w, t.remainderX(hubs.at(j)), hubs.at(j) / t.w) <
				t.dist2(t.remainderX(hubs.at(k)), hubs.at(k) / t.w, t.remainderX(hubs.at(closest)),
						hubs.at(closest) / t.w))
				closest = int(j);
		}
		if (!road(hubs.at(k), hubs.at(closest)))
		{
			L.failure = "The towns could not be connected.";
			return false;
		}
	}
	for (size_t k = 0; k < exits.size(); k += 2)
	{
		::MapGeneration::generationCheckpoint();
		// Choose both gate destinations together. A fixed second-nearest rule can skip
		// the nearby market on the very side where that fort has its useful expansion.
		int west = hubs.front(), east = hubs.front();
		long long best = LLONG_MAX;
		for (int a : hubs)
		{
			::MapGeneration::generationCheckpoint();
			for (int b : hubs)
			{
				::MapGeneration::generationCheckpoint();
				if (a == b && hubs.size() > 1)
					continue;
				const long long distance =
					t.dist2(t.remainderX(exits.at(k)), exits.at(k) / t.w, t.remainderX(a), a / t.w) +
					t.dist2(t.remainderX(exits.at(k + 1)), exits.at(k + 1) / t.w, t.remainderX(b), b / t.w);
				if (distance < best)
				{
					best = distance;
					west = a;
					east = b;
				}
			}
		}
		if (!road(exits.at(k), west) || !road(exits.at(k + 1), east))
		{
			L.failure = "A fort cannot reach the road network.";
			return false;
		}
	}
	if (hubs.size() > 2 && !road(hubs.front(), hubs.back()))
	{
		L.failure = "The market road could not be connected.";
		return false;
	}
	return true;
}

// The ground the bocage and the crowns may take: open country at least `margin` tiles from any fort,
// town, road, water or crown.
std::vector<int> countryReach(const Layout &L)
{
	const Torus &t = L.t;
	std::vector<unsigned char> busy(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		busy.at(i) = L.buffer.at(i) || L.towns.at(i) || L.roads.at(i) || L.terrain.at(i) == WATER ||
					 L.crown.at(i);
	}
	return stepsFrom(t, busy);
}

// The uplands' rocky crowns: the highest few percent of the upland noise in open country, as granite
// tors. Specks too small to read as a crag are dropped; each crown keeps its highest tile as the seed
// of its quarry face.
void uplandCrowns(Layout &L, GenerationContext &context)
{
	const Torus &t = L.t;
	const auto reach = countryReach(L);
	// Crags break the high ground into several tors rather than one bald summit.
	const auto crags = fractalNoise(t.w, t.h, 16, 2, context.stream("forts-crowns"));
	std::vector<int> height(t.size(), 0), heights;
	for (int i = 0; i < t.size(); ++i)
	{
		height.at(i) = L.uplands.at(i) / 2 + crags.at(i);
		if (reach.at(i) >= 4)
			heights.push_back(height.at(i));
	}
	const size_t keep = heights.size() / kCrownShare;
	if (keep < 9)
		return;
	std::nth_element(heights.begin(), heights.end() - keep, heights.end());
	const int threshold = *(heights.end() - keep);
	std::vector<unsigned char> high(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		high.at(i) = reach.at(i) >= 4 && height.at(i) >= threshold;
	for (const auto &crown : pieces(t, high))
	{
		if (crown.size() < 9)
			continue;
		int top = crown.front();
		for (int i : crown)
		{
			L.crown.at(i) = 1;
			if (height.at(i) > height.at(top))
				top = i;
		}
		L.crownSeeds.push_back(top);
	}
}

// The bocage: the open country divided into fields about kFieldSpacing across (each tile belongs to
// its nearest jittered field centre), with a hedgerow on the border between two fields for
// `hedgerows` percent of the borders, each hedged border keeping a field gate near its middle. Roads,
// water, forts, towns and crowns are never hedged. Any ground the hedges and crowns would close off
// from the roads gets its hedges, then its crowns, taken down until every field can be walked to.
void bocage(Layout &L, const FortsOptions &o, GenerationContext &context)
{
	const Torus &t = L.t;
	const int n = t.size();
	const std::uint64_t salt = context.bounded("forts-hedges", 1u << 30);
	const int cols = std::max(1, t.w / kFieldSpacing), rows = std::max(1, t.h / kFieldSpacing);
	std::vector<int> seedX(cols * rows), seedY(cols * rows);
	for (int cy = 0; cy < rows; ++cy)
		for (int cx = 0; cx < cols; ++cx)
		{
			::MapGeneration::generationCheckpoint();
			const int x0 = cx * t.w / cols, x1 = (cx + 1) * t.w / cols;
			const int y0 = cy * t.h / rows, y1 = (cy + 1) * t.h / rows;
			const int jx = context.bounded("forts-hedges", unsigned(x1 - x0));
			const int jy = context.bounded("forts-hedges", unsigned(y1 - y0));
			seedX.at(cy * cols + cx) = x0 + jx;
			seedY.at(cy * cols + cx) = y0 + jy;
		}
	context.telemetry.measure("forts.hedges.fields", cols * rows);
	if (o.hedgerows > 0)
	{
		const auto reach = countryReach(L);
		std::vector<int> field(n, -1);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (reach.at(i) < 2)
				continue;
			const int x = t.remainderX(i), y = i / t.w;
			const int cx = std::min(cols - 1, x * cols / t.w), cy = std::min(rows - 1, y * rows / t.h);
			long long best = LLONG_MAX;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int c = ((cy + dy + rows) % rows) * cols + (cx + dx + cols) % cols;
					const long long d = t.dist2(x, y, seedX.at(c), seedY.at(c));
					if (d < best || (d == best && c < field.at(i)))
					{
						best = d;
						field.at(i) = c;
					}
				}
		}
		// Each border tile belongs to the pair of fields it parts: its own and its lowest neighbour.
		std::map<std::pair<int, int>, std::vector<int>> borders;
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (field.at(i) < 0)
				continue;
			int lower = INT_MAX;
			const int x = t.remainderX(i), y = i / t.w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int f = field.at(t.at(x + dx, y + dy));
					if (f >= 0 && f < field.at(i))
						lower = std::min(lower, f);
				}
			if (lower != INT_MAX)
				borders[{lower, field.at(i)}].push_back(i);
		}
		int hedged = 0;
		for (const auto &[pair, tiles] : borders)
		{
			::MapGeneration::generationCheckpoint();
			const std::uint64_t roll = mix(salt * 1000003ULL + std::uint64_t(pair.first) * 65537ULL +
										  std::uint64_t(pair.second));
			if (tiles.size() < 4 || int(roll % 100) >= o.hedgerows)
				continue;
			++hedged;
			for (int i : tiles)
				L.hedge.at(i) = 1;
			if (int((roll >> 32) % 100) >= kFieldGates)
				continue;
			// The field gate: the border tile nearest the middle of the two fields' centres.
			const int ax = seedX.at(pair.first), ay = seedY.at(pair.first);
			const double mx = ax + t.offsetX(ax, seedX.at(pair.second)) / 2.0,
						 my = ay + t.offsetY(ay, seedY.at(pair.second)) / 2.0;
			int gate = tiles.front();
			long long nearest = LLONG_MAX;
			for (int i : tiles)
			{
				const long long d = t.dist2(t.remainderX(i), i / t.w, t.x(int(mx)), t.y(int(my)));
				if (d < nearest)
				{
					nearest = d;
					gate = i;
				}
			}
			for (int dy = -2; dy <= 2; ++dy)
				for (int dx = -2; dx <= 2; ++dx)
				{
					const int j = t.at(t.remainderX(gate) + dx, gate / t.w + dy);
					L.hedgeGap.at(j) = 1;
					L.hedge.at(j) = 0;
				}
		}
		context.telemetry.measure("forts.hedges.borders", int(borders.size()));
		context.telemetry.measure("forts.hedges.hedged-borders", hedged);
	}
	// A field the hedges and crowns close off from the roads gets a field gate in the hedge nearest the
	// open country, one per enclosure per round, until every field can be walked to; ground only
	// crowns enclose loses the crown tiles round it.
	const auto cut = [&](int at)
	{
		for (int dy = -2; dy <= 2; ++dy)
			for (int dx = -2; dx <= 2; ++dx)
			{
				const int j = t.at(t.remainderX(at) + dx, at / t.w + dy);
				L.hedgeGap.at(j) = 1;
				L.hedge.at(j) = 0;
			}
	};
	int opened = 0;
	const std::vector<unsigned char> everywhere(n, 1);
	for (int round = 0; round < 64; ++round)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<unsigned char> barrier(n, 0), open(n, 0), roads(n, 0);
		for (int i = 0; i < n; ++i)
			barrier.at(i) = L.hedge.at(i) || L.crown.at(i);
		const auto fromBarrier = stepsFrom(t, barrier);
		for (int i = 0; i < n; ++i)
		{
			open.at(i) = L.terrain.at(i) != WATER && !L.wall.at(i) &&
						 (fromBarrier.at(i) < 0 || fromBarrier.at(i) > 1);
			roads.at(i) = L.roads.at(i);
		}
		const auto walk = stepsFrom(t, roads, open);
		std::vector<unsigned char> stranded(n, 0), reached(n, 0);
		for (int i = 0; i < n; ++i)
		{
			stranded.at(i) = open.at(i) && walk.at(i) < 0;
			reached.at(i) = walk.at(i) >= 0;
		}
		const auto fromReached = stepsFrom(t, reached);
		int cuts = 0;
		for (const auto &piece : pieces(t, stranded))
		{
			const auto fromPiece = reachFrom(t, piece, everywhere, 3);
			int best = -1;
			for (int i : fromPiece.tiles)
				if (L.hedge.at(i) &&
					(best < 0 || fromReached.at(i) < fromReached.at(best) ||
					 (fromReached.at(i) == fromReached.at(best) && i < best)))
					best = i;
			if (best >= 0)
			{
				cut(best);
				++cuts;
				continue;
			}
			for (int i : fromPiece.tiles)
				if (L.crown.at(i))
				{
					L.crown.at(i) = 0;
					++cuts;
				}
		}
		opened += cuts;
		if (!cuts)
			break;
	}
	context.telemetry.measure("forts.hedges.opened-tiles", opened);
	int hedgeTiles = 0;
	for (int i = 0; i < n; ++i)
		hedgeTiles += L.hedge.at(i);
	context.telemetry.measure("forts.hedges.tiles", hedgeTiles);
}

// THE GROUND, as the finished world must hold it: a pure function of the design, so validateWorld
// rebuilds it and compares every vertex.
//  - the plots' sand rims are barren ground and their beds loam, watered by their pools as before;
//  - every road, gate and town street is paved, the fords boardwalk;
//  - the ramparts are walls on vertices (wallVertices), leaning away from the gate roads: ridge rock
//    on the bastions, towers and gate fronts, granite on the curtains; the crowns are granite and the
//    hedgerows hedge, every wall at least three vertices thick so its look draws whole;
//  - a brick square round every swarm, kept off the verge at the ramparts' foot where the bailey's
//    quarry faces stand.
TerrainSketch sketchTerrain(const Layout &L)
{
	const Torus &t = L.t;
	const int n = t.size();
	TerrainSketch s = L.terrain;
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (L.rim.at(i) && s.at(i) == SAND)
			s.at(i) = DIRT;
		else if (L.plot.at(i) >= 0 && s.at(i) == GRASS)
			s.at(i) = LOAM;
		else if (L.roads.at(i) && s.at(i) == SAND)
			s.at(i) = TRAIL;
	}
	std::vector<unsigned char> open(n, 0), ridgeTiles(n, 0), cliffTiles(n, 0);
	for (int i = 0; i < n; ++i)
	{
		open.at(i) = L.roads.at(i) || L.gates.at(i) || L.hedgeGap.at(i);
		ridgeTiles.at(i) = L.wall.at(i) && L.ridge.at(i);
		cliffTiles.at(i) = L.wall.at(i) && !L.ridge.at(i);
	}
	const auto ridges = wallVertices(t, ridgeTiles, &open, &L.gates);
	const auto cliffs = wallVertices(t, cliffTiles, &open, &L.gates);
	const auto crowns = wallVertices(t, L.crown, &open);
	const auto hedges = wallVertices(t, L.hedge, &open);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (s.at(i) == WATER)
			continue;
		if (ridges.at(i))
			s.at(i) = RIDGE_ROCK;
		else if (cliffs.at(i) || crowns.at(i))
			s.at(i) = CLIFFS;
		else if (hedges.at(i))
			s.at(i) = HEDGE;
	}
	const auto rampartGrow = [&](int i)
	{
		return s.at(i) == GRASS && L.plot.at(i) < 0 && !L.rim.at(i) && !L.towns.at(i) &&
			   !cornerOf(t, open, i);
	};
	thickenRidges(s, t, rampartGrow);
	thickenRidges(s, t, rampartGrow, nullptr, CLIFFS);
	thickenRidges(
		s, t, [&](int i) { return rampartGrow(i) && !L.buffer.at(i); }, nullptr, HEDGE);
	std::vector<unsigned char> rock(n, 0);
	for (int i = 0; i < n; ++i)
		rock.at(i) = s.at(i) == CLIFFS || s.at(i) == RIDGE_ROCK;
	const auto fromRock = stepsFrom(t, rock);
	const FortPlan plan = fortPlan(L.design.layout, L.r);
	for (int k = 0; k < int(L.homes.size()); ++k)
	{
		::MapGeneration::generationCheckpoint();
		const double cx = L.homes.at(k).x + L.design.x(plan.settleU, plan.settleV),
					 cy = L.homes.at(k).y + L.design.y(plan.settleU, plan.settleV);
		paveCore(s, t, cx, cy, kBailey,
				 [&](int i)
				 {
					 return L.homeOf.at(i) == k && L.plot.at(i) < 0 && !L.rim.at(i) &&
							(fromRock.at(i) < 0 || fromRock.at(i) > kVerge);
				 });
	}
	LookRoles roles;
	roles.tended = [&](int i) { return L.homeOf.at(i) >= 0; };
	roles.path = [&](int i)
	{
		if (L.ford.at(i))
			return PathLook::Bridge;
		if (L.buffer.at(i))
			return PathLook::Town;
		return L.towns.at(i) ? PathLook::Street : PathLook::Road;
	};
	paintClimate(s, t, kFortsLook, roles);
	return s;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const FortsOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int r = o.homeSize;
	L.r = r;
	L.terrain.assign(t.size(), GRASS);
	L.homeOf.assign(t.size(), -1);
	L.plot.assign(t.size(), -1);
	L.wall.assign(t.size(), 0);
	L.roads.assign(t.size(), 0);
	L.gates.assign(t.size(), 0);
	L.buffer.assign(t.size(), 0);
	L.towns.assign(t.size(), 0);
	L.fort.assign(t.size(), -1);
	for (auto *mask : {&L.ridge, &L.rim, &L.ford, &L.hedge, &L.hedgeGap, &L.crown})
		mask->assign(t.size(), 0);
	L.uplands = fractalNoise(t.w, t.h, 64, 3, context.stream("forts-uplands"));
	// Sequence named-stream draws explicitly: function argument evaluation order differs
	// between compilers. Keep the same lattice for a seed on every platform.
	const int offsetX = context.bounded("forts-layout", t.w);
	const int offsetY = context.bounded("forts-layout", t.h);
	L.homes = latticeSites(t.w, t.h, request.nbTeams, offsetX, offsetY).sites;
	dealStarts(context, L.homes);
	for (size_t a = 0; a < L.homes.size(); ++a)
	{
		::MapGeneration::generationCheckpoint();
		for (size_t b = a + 1; b < L.homes.size(); ++b)
		{
			::MapGeneration::generationCheckpoint();
			if (std::abs(t.offsetX(int(L.homes.at(a).x), int(L.homes.at(b).x))) < 2 * r + 16 &&
				std::abs(t.offsetY(int(L.homes.at(a).y), int(L.homes.at(b).y))) < 2 * r + 16)
			{
				L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
				return L;
			}
		}
	}
	if (std::min(t.w, t.h) < 2 * r + 16)
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	// One design for every fort on the map, from its own stream so the rest of the layout draws
	// what it always drew.
	FortDesign &d = L.design;
	d.layout = FortLayout(context.bounded("forts-design", unsigned(FortLayout::Count)));
	d.walls = FortWalls(context.bounded("forts-design", unsigned(FortWalls::Count)));
	d.town = TownPlan(context.bounded("forts-design", unsigned(TownPlan::Count)));
	d.turn = context.bounded("forts-design", 2);
	d.flipU = context.bounded("forts-design", 2);
	d.flipV = context.bounded("forts-design", 2);
	d.townTurn = context.bounded("forts-design", 2);
	context.telemetry.choice("forts.design.layout", kLayoutNames[int(d.layout)]);
	context.telemetry.choice("forts.design.walls", kWallNames[int(d.walls)]);
	context.telemetry.choice("forts.design.town", kTownNames[int(d.town)]);
	context.telemetry.choice("forts.design.gates", d.turn ? "north-south" : "east-west");
	const FortPlan plan = fortPlan(d.layout, r);
	const int gate = o.gateWidth / 2;
	std::vector<int> exits;
	for (int k = 0; k < request.nbTeams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		const int cx = int(L.homes.at(k).x), cy = int(L.homes.at(k).y);
		const auto at = [&](int u, int v) { return t.at(cx + d.x(u, v), cy + d.y(u, v)); };
		const auto tower = [&](int u, int v)
		{
			const int au = std::abs(u), av = std::abs(v);
			switch (d.walls)
			{
			case FortWalls::Gatehouse:
				// A square tower either side of each gate and one in the middle of each long wall.
				return (au >= r - 3 && au <= r + 1 && av >= gate + 2 && av <= gate + 5) ||
					   (av >= r - 3 && av <= r + 1 && au <= 2);
			case FortWalls::Round:
			{
				// Round towers at the corners and beside the gates, standing out past the wall.
				const auto round = [&](int tu, int tv)
				{ return (au - tu) * (au - tu) + (av - tv) * (av - tv) <= 8; };
				return round(r, r) || round(r, gate + 4);
			}
			default:
				return false;
			}
		};
		for (int v = -r - 5; v <= r + 5; ++v)
		{
			::MapGeneration::generationCheckpoint();
			for (int u = -r - 5; u <= r + 5; ++u)
			{
				::MapGeneration::generationCheckpoint();
				const int i = at(u, v);
				L.buffer.at(i) = 1;
				L.terrain.at(i) = GRASS;
				const int edge = std::max(std::abs(u), std::abs(v));
				if (edge < r - 1)
					L.homeOf.at(i) = k;
				if (edge <= r)
					L.fort.at(i) = k;
				// Two-tile ramparts with square corner bastions, and two broad opposite gateways.
				const bool bastion = d.walls != FortWalls::Round && std::abs(u) >= r - 3 &&
									 std::abs(v) >= r - 3 && edge <= r + 1;
				L.wall.at(i) = (edge >= r - 1 && edge <= r) || bastion || tower(u, v);
				if (std::abs(v) <= gate + 1)
				{
					L.wall.at(i) = 0;
					if (std::abs(u) >= r - 1 && std::abs(u) <= r)
						L.gates.at(i) = 1;
				}
				if (std::abs(v) <= gate && std::abs(u) <= r + 5)
				{
					L.wall.at(i) = 0;
					if (std::abs(u) >= r - 1 && std::abs(u) <= r)
						L.gates.at(i) = 1;
					L.roads.at(i) = 1;
					L.terrain.at(i) = SAND;
				}
				// The walls a defender's towers should fire across: bastions and towers, and the
				// curtain either side of each gate.
				L.ridge.at(i) = L.wall.at(i) && (bastion || tower(u, v) ||
												 (std::abs(u) >= r - 1 && std::abs(v) <= gate + 7));
			}
		}
		for (const FortPlot &p : plan.plots)
		{
			::MapGeneration::generationCheckpoint();
			for (int b = p.b0; b <= p.b1; ++b)
			{
				::MapGeneration::generationCheckpoint();
				for (int a = p.a0; a <= p.a1; ++a)
				{
					::MapGeneration::generationCheckpoint();
					const int i = at(p.su * a, p.sv * b);
					const bool rim = a == p.a0 || a == p.a1 || b == p.b0 || b == p.b1;
					L.terrain.at(i) = rim ? SAND : GRASS;
					L.rim.at(i) = rim;
					if (!rim)
						L.plot.at(i) = 2 * k + p.timber;
					const bool water = p.waterOnA ? a >= p.a1 - 5 && b >= p.b0 + 2 && b < p.b1
												  : b <= p.b0 + 4 && a >= p.a0 + 2 && a <= p.a1 - 2;
					if (water && !rim)
						L.terrain.at(i) = WATER;
				}
			}
		}
		// A moat all the way round, broken only where the gate roads cross it. It keeps a corner
		// clear of every rampart and tower, because a beach may not touch a tile of stone: where a
		// tower stands out it narrows round it instead of stopping.
		const auto nearStone = [&](int x, int y)
		{
			for (int ty = y - 2; ty <= y + 1; ++ty)
			{
				::MapGeneration::generationCheckpoint();
				for (int tx = x - 2; tx <= x + 1; ++tx)
				{
					::MapGeneration::generationCheckpoint();
					if (L.wall.at(t.at(tx, ty)))
						return true;
				}
			}
			return false;
		};
		for (int v = -r - 5; v <= r + 5; ++v)
		{
			::MapGeneration::generationCheckpoint();
			for (int u = -r - 5; u <= r + 5; ++u)
			{
				::MapGeneration::generationCheckpoint();
				const int edge = std::max(std::abs(u), std::abs(v));
				if (edge < r + 3 || std::abs(v) <= gate + 3)
					continue;
				const int x = cx + d.x(u, v), y = cy + d.y(u, v);
				if (!nearStone(x, y))
					L.terrain.at(t.at(x, y)) = WATER;
			}
		}
		exits.push_back(at(-r - 5, 0));
		exits.push_back(at(r + 5, 0));
	}
	marketTowns(L, o, request, context);
	if (!riverCountry(L, o, request, context))
		return L;
	if (!countryRoads(L, exits))
		return L;
	uplandCrowns(L, context);
	bocage(L, o, context);
	context.telemetry.measure("forts.crowns.placed", int(L.crownSeeds.size()));
	context.telemetry.measure("forts.towns.requested", request.nbTeams);
	context.telemetry.measure("forts.towns.placed", L.villages.size());
	layBeaches(L.terrain, t);
	context.telemetry.measure("forts.home.half-width", r);
	context.telemetry.measure("forts.homes.placed", L.homes.size());
	context.telemetry.measure("forts.gates.per-home", 2);
	context.telemetry.measure("forts.river.width-corners", o.riverWidth);
	return L;
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	context.stage = "forts layout";
	const FortsOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	GenerationMap &map = game.map;
	context.stage = "forts terrain";
	const TerrainSketch drawn = sketchTerrain(L);
	writeVertices(map, drawn);
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		game.addTeam();
	}
	const FortDesign &d = L.design;
	const FortPlan plan = fortPlan(d.layout, o.homeSize);
	context.stage = "forts colonies";
	if (!settleColonies(
			game, context, "forts-starts",
			[&](int k)
			{
				auto mask = homeGrassMask(map, t, L.homeOf, k);
				for (int i = 0; i < t.size(); ++i)
				{
					::MapGeneration::generationCheckpoint();
					if (L.plot.at(i) >= 0 || L.wall.at(i))
						mask.at(i) = 0;
				}
				return mask;
			},
			[&](int k)
			{
				return MapGeneratorPoint(int(L.homes.at(k).x) + d.x(plan.settleU, plan.settleV),
										 int(L.homes.at(k).y) + d.y(plan.settleU, plan.settleV));
			}))
		return false;
	context.stage = "forts farms and countryside";
	const auto reserved = swarmSurroundings(t, context);
	const auto fertility = cropGrowthField(drawn, t);
	for (int p = 0; p < 2 * context.request.nbTeams; ++p)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<int> tiles;
		for (int i = 0; i < t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (L.plot.at(i) == p && !reserved.at(i) && clearGround(map, t.remainderX(i), i / t.w) &&
				fertility.at(t.remainderX(i), i / t.w) > 0)
				tiles.push_back(i);
		}
		std::stable_sort(
			tiles.begin(), tiles.end(), [&](int a, int b)
			{ return fertility.at(t.remainderX(a), a / t.w) > fertility.at(t.remainderX(b), b / t.w); });
		const int wanted = 32 + int(scaledCount(p % 2 ? 16 : 32, p % 2 ? o.wood : o.wheat));
		const int placed = std::min(wanted, int(tiles.size()));
		std::uint64_t yield = 0;
		for (int j = 0; j < placed; ++j)
		{
			::MapGeneration::generationCheckpoint();
			map.setResourceByIndex(t.remainderX(tiles.at(j)), tiles.at(j) / t.w, p % 2 ? WOOD : WHEAT, 1);
			yield += fertility.at(t.remainderX(tiles.at(j)), tiles.at(j) / t.w);
		}
		context.telemetry.measure("forts.plot.target-tiles", wanted, p);
		context.telemetry.measure("forts.plot.planted-tiles", placed, p);
		if (placed < wanted)
			context.telemetry.fallback("forts.plot.capacity",
									   "The contained plot limits surplus crops.", p);
		context.telemetry.measure("forts.plot.planted-growth-sum", yield, p);
		if (placed < 32)
		{
			context.detail = "A fort has insufficient fertile farm space.";
			return false;
		}
	}
	// Small household orchards put every fruit within reach of every fort. The larger
	// market orchards still reward expansion; the fruit control scales both layers.
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		for (int fruit = 0; fruit < 3; ++fruit)
		{
			::MapGeneration::generationCheckpoint();
			const int cx = int(L.homes.at(k).x), cy = int(L.homes.at(k).y);
			const int u = plan.orchardU + fruit * plan.orchardDu,
					  v = plan.orchardV + fruit * plan.orchardDv;
			const int seed = t.at(cx + d.x(u, v), cy + d.y(u, v));
			const auto eligible = [&](int i)
			{
				return L.homeOf.at(i) == k && L.plot.at(i) < 0 && !L.roads.at(i) &&
					   !reserved.at(i) && clearGround(map, t.remainderX(i), i / t.w);
			};
			const int wanted = int(scaledCount(3, o.fruit));
			const int placed =
				eligible(seed) ? growPatch(map, t, seed, CHERRY + fruit, wanted, eligible) : 0;
			context.telemetry.measure("forts.orchard.target-tiles", wanted, 3 * k + fruit);
			context.telemetry.measure("forts.orchard.planted-tiles", placed, 3 * k + fruit);
			if (placed < wanted)
			{
				context.detail = "A fort has insufficient household orchard space.";
				return false;
			}
		}
	}
	// The ramparts were the forts' quarry when they were stone: each bailey gets its stone back as
	// quarry faces at the foot of its side walls, away from the gates, at every stone setting.
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		const int cx = int(L.homes.at(k).x), cy = int(L.homes.at(k).y);
		std::vector<KitSeed> seeds;
		for (int side = -1; side <= 1; side += 2)
			seeds.push_back({cx + d.x(0, side * (o.homeSize - 3)), cy + d.y(0, side * (o.homeSize - 3)),
							 o.homeSize / 2});
		const int placed = plantQuarryFaces(
			map, t, seeds, kQuarryFace,
			[&](int i)
			{
				return L.homeOf.at(i) == k && L.plot.at(i) < 0 && !L.rim.at(i) && !L.roads.at(i) &&
					   !reserved.at(i) && clearGround(map, t.remainderX(i), i / t.w);
			});
		context.telemetry.measure("forts.bailey.quarry-tiles", placed, k);
		if (placed < kFortFaces * kQuarryFace / 2)
		{
			context.detail = "A fort has no room for its quarry.";
			return false;
		}
	}
	const auto forestAt = [&](int i) { return L.uplands.at(i) / 65536.0; };
	const PeriodicNoise fields(t.w, t.h, 16, context.stream("forts-fields"));
	std::vector<int> woods;
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (!L.buffer.at(i) && !L.towns.at(i) && !L.roads.at(i) &&
			clearGround(map, t.remainderX(i), i / t.w))
			woods.push_back(i);
	}
	std::stable_sort(woods.begin(), woods.end(),
					 [&](int a, int b) { return forestAt(a) > forestAt(b); });
	// Quarry faces at the foot of the granite crowns, as much stone as the uplands' exposed rock
	// held when it was stone, shared out round the crowns.
	const int rockTarget = int(scaledCount(woods.size() / 100, o.stone));
	int rocks = 0;
	for (int round = 0; rockTarget > 0 && round < 8 && rocks < rockTarget; ++round)
	{
		::MapGeneration::generationCheckpoint();
		const int before = rocks;
		for (int seed : L.crownSeeds)
		{
			if (rocks >= rockTarget)
				break;
			const KitSeed at{t.remainderX(seed), seed / t.w, 8 + 4 * round};
			rocks += plantQuarryFaces(
				map, t, {at}, std::min(kQuarryFace, rockTarget - rocks),
				[&](int i)
				{
					return !L.buffer.at(i) && !L.towns.at(i) && !L.roads.at(i) &&
						   clearGround(map, t.remainderX(i), i / t.w);
				});
		}
		if (rocks == before)
			break;
	}
	context.telemetry.measure("forts.uplands.stone-target", rockTarget);
	context.telemetry.measure("forts.uplands.stone-tiles", rocks);
	woods.erase(std::remove_if(woods.begin(), woods.end(),
							   [&](int i) { return !clearGround(map, t.remainderX(i), i / t.w); }),
				woods.end());
	const int count = std::min(int(woods.size()), int(scaledCount(woods.size() / 6, o.wood)));
	for (int j = 0; j < count; ++j)
	{
		::MapGeneration::generationCheckpoint();
		map.setResourceByIndex(t.remainderX(woods.at(j)), woods.at(j) / t.w, WOOD, 1);
	}
	context.telemetry.measure("forts.forest.planted-tiles", count);
	furnishGround(
		map, t, context, fertility,
		[&](int i)
		{
			return !L.buffer.at(i) && !L.towns.at(i) && !L.roads.at(i) &&
				   clearGround(map, t.remainderX(i), i / t.w);
		},
		[&](int i) { return fields.at(t.remainderX(i), i / t.w); }, [&](int i) { return forestAt(i); },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(area / 18, o.wheat)), 0,
								 int(scaledCount(area / 500, o.stone)),
								 int(scaledCount(area / 600, o.fruit))};
		},
		"forts-rocks", "forts-orchards");
	// Try all three fruits on each market’s outskirts; crowded banks can limit these patches.
	for (const auto &v : L.villages)
	{
		::MapGeneration::generationCheckpoint();
		for (int fruit = 0; fruit < 3; ++fruit)
		{
			::MapGeneration::generationCheckpoint();
			const double angle = fruit * 2 * kPi / 3;
			const auto eligible = [&](int i)
			{
				return !L.buffer.at(i) && !L.towns.at(i) && !L.roads.at(i) &&
					   clearGround(map, t.remainderX(i), i / t.w);
			};
			const int seed = seedNear(
				t, int(v.x + (o.villageSize + 4) * ::MapGeneration::Numeric::cos(angle)),
				int(v.y + (o.villageSize + 4) * ::MapGeneration::Numeric::sin(angle)), 8, eligible);
			if (seed >= 0)
				growPatch(map, t, seed, CHERRY + fruit, int(scaledCount(5, o.fruit)), eligible);
		}
	}
	seedAlgae(map, context, t, "forts-algae", o.algae, AlgaeBand::anyWater());
	secureStartingCrops(game, context, t, 24, 32, 0, &L.wall);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&L.wall);
	return true;
}

std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (auto error = designMismatch(L, game.map, "forts"); !error.empty())
		return error;
	const Torus &t = L.t;
	const auto where = [&](int i)
	{ return "(" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) + ")"; };
	// Every rampart, crown, hedge, bed and road stands as designed, vertex for vertex.
	const TerrainSketch designed = sketchTerrain(L);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (game.map.vertexTerrainAt(t.remainderX(i), i / t.w) != designed.at(i))
			return "The ground at " + where(i) + " is not as designed.";
	}
	std::vector<int> orchard(3 * context.request.nbTeams, 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		const auto &resource = game.map.getResource(i);
		if (L.homeOf.at(i) >= 0 && resource.type >= CHERRY && resource.type < CHERRY + 3)
			++orchard.at(3 * L.homeOf.at(i) + resource.type - CHERRY);
		if (L.roads.at(i) && game.map.isResource(t.remainderX(i), i / t.w))
			return "A fort road is obstructed.";
	}
	// With its gates shut, nothing walks or swims out of a fort.
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<int> inside;
		for (int i = 0; i < t.size(); ++i)
			if (L.homeOf.at(i) == k && !L.gates.at(i))
				inside.push_back(i);
		if (const int leak = leakWithShut(game.map, t, inside, L.gates, true,
										  [&](int i) { return L.fort.at(i) == k; });
			leak >= 0)
			return "A fort has an unintended entrance at " + where(leak) + ".";
	}
	const auto buildable = buildableTiles(game.map);
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		auto home = homeGrassMask(game.map, t, L.homeOf, k);
		if (buildSites(t, buildable, home) < 64)
			return "A fort has insufficient courtyard building room.";
	}
	const FortsOptions o(context.request);
	for (int count : orchard)
	{
		::MapGeneration::generationCheckpoint();
		if (count < int(scaledCount(3, o.fruit)))
			return "A fort has lost its household orchard.";
	}
	for (const auto &v : L.villages)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<unsigned char> town(t.size(), 0);
		for (int dy = -o.villageSize; dy <= o.villageSize; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -o.villageSize; dx <= o.villageSize; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				town.at(t.at(int(v.x) + dx, int(v.y) + dy)) = 1;
			}
		}
		if (buildSites(t, buildable, town) < 16)
			return "A market town has lost its building plots.";
	}
	return walkFromFirstColony(game.map, context.request.nbTeams, "fort roads", "").error;
}
// The classic world the JavaScript example draws (FortsBlueprint.inc, content/data/generators/examples/
// forts): the design's grass, sand and water as they stand, every rampart a stone deposit. Revision 8's
// checks, kept for that example, which stays a simplified stone-wall sample of the generator.
std::string validateClassicWorld(const GenerationWorld &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (auto error = designMismatch(L, game.map, "forts"); !error.empty())
		return error;
	const Torus &t = L.t;
	std::vector<int> orchard(3 * context.request.nbTeams, 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		const auto &resource = game.map.getResource(i);
		if (L.homeOf.at(i) >= 0 && resource.type >= CHERRY && resource.type < CHERRY + 3)
			++orchard.at(3 * L.homeOf.at(i) + resource.type - CHERRY);
		if (L.wall.at(i) && !(game.map.isResource(t.remainderX(i), i / t.w) &&
							  game.map.getResource(t.remainderX(i), i / t.w).type == STONE))
			return "A fort rampart is missing.";
		if (L.roads.at(i) && game.map.isResource(t.remainderX(i), i / t.w))
			return "A fort road is obstructed.";
	}
	auto pieces = L.homeOf;
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (pieces.at(i) < 0 && !L.wall.at(i) && !L.gates.at(i))
			pieces.at(i) = context.request.nbTeams;
	}
	if (pieceLeak(game.map, t, pieces, L.gates) >= 0)
		return "A fort has an unintended entrance.";
	const auto buildable = buildableTiles(game.map);
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		auto home = homeGrassMask(game.map, t, L.homeOf, k);
		if (buildSites(t, buildable, home) < 64)
			return "A fort has insufficient courtyard building room.";
	}
	const FortsOptions o(context.request);
	for (int count : orchard)
	{
		::MapGeneration::generationCheckpoint();
		if (count < int(scaledCount(3, o.fruit)))
			return "A fort has lost its household orchard.";
	}
	for (const auto &v : L.villages)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<unsigned char> town(t.size(), 0);
		for (int dy = -o.villageSize; dy <= o.villageSize; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -o.villageSize; dx <= o.villageSize; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				town.at(t.at(int(v.x) + dx, int(v.y) + dy)) = 1;
			}
		}
		if (buildSites(t, buildable, town) < 16)
			return "A market town has lost its building plots.";
	}
	return walkFromFirstColony(game.map, context.request.nbTeams, "fort roads", "").error;
}

} // namespace
FortsOptions::FortsOptions(const GenerationRequest &r)
	: homeSize(r.option("home-size")), gateWidth(r.option("gate-width")),
	  riverWidth(r.option("river-width")), lakes(r.option("lakes")),
	  villageSize(r.option("village-size")),
	  // The JavaScript example (FortsBlueprint.inc) draws the classic open country and declares no
	  // hedgerows control.
	  hedgerows(r.options.count("hedgerows") ? r.option("hedgerows") : 0),
	  wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition fortsDefinition()
{
	return {
		"forts",
		35,
		"Forts",
		// Revision 9: the terrain catalogue - granite and ridge-rock ramparts with quarry faces in
		// the baileys, paved roads and baileys, loam beds, granite crowns and hedgerows.
		9,
		false,
		{GeneratorControl{"home-size", "Home size", 20, 26, 2, 22, ControlGroup::Layout}
			 .withSearchRange(22, 26),
		 GeneratorControl{"gate-width", "Gate width", 4, 8, 2, 6, ControlGroup::Layout}
			 .withSearchRange(6, 8),
		 GeneratorControl{"river-width", "River width", 3, 13, 2, 9, ControlGroup::Layout}
			 .withSearchRange(5, 11),
		 GeneratorControl{"lakes", "Lakes", 0, 3, 1, 1, ControlGroup::Layout}.withSearchRange(1, 3),
		 GeneratorControl{"village-size", "Village size", 8, 12, 2, 10, ControlGroup::Layout}
			 .withSearchRange(10, 12),
		 GeneratorControl{"hedgerows", "Hedgerows", 0, 100, 10, 70, ControlGroup::Layout}
			 .withSearchRange(30, 70),
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		designFailure<design>,
		validateWorld,
		// See BreachableHighlands: "arena" means one shared battleground elsewhere in this file
		// set, but each colony here gets its own walled fort, so it uses "stronghold" instead.
		{"terrain:stronghold", "feature:stone-walls", "feature:river", "style:siege",
		 "style:fortified", "fairness:stamped-lattice"}};
}

#include "FortsBlueprint.inc"
