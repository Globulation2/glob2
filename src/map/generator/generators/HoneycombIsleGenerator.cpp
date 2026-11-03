// SPDX-License-Identifier: GPL-3.0-or-later
#include "HoneycombIsleGenerator.h"
#include "Drawing.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Growth.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Room.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Tessellation.h"
#include "TerrainTypeTable.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>
using namespace MapGeneration;

// Honeycomb isle: a board of terrain hexes on an island in the sea, after the board game. Every
// hex is one landscape (a wheat field, a forest, a pasture, clay hills, a mountain with its quarry,
// a lake), roads run along every hex edge, a river cuts the island in two under a few bridges and
// a desert oasis with an orchard sits at the middle. Every colony's home is two hexes with a
// cistern garden, and every home borders two food hexes and a forest of its own.
//
// HOW IT PLAYS. The island is only as big as the colonies need (blocks per colony), and the sea and
// river cannot be crossed until units swim, so colonies start a hex or two apart. Each kind of hex
// is one resource or one use: food in the fields (and the water meadows along the river), wood in
// the forests, stone at the foot of the mountains (one per colony, between the homes) and on the
// hills, building room on pastures and hills. A colony grows by taking the hexes round it, and
// the mountains, lakes and the oasis between the homes are where neighbours meet. Roads are fast
// and seal every hex: crops, trees and their growth stay inside the hex they were planted in.
//
// WHY IT WORKS (docs/map-generators/game-rules-for-map-design.md). Each road is cobbled road down
// its middle with gravel shoulders. Neither carries a deposit, so a hex is a sealed plot of its own,
// and the gravel inhibits the growth probe as the sand streets of earlier revisions did, which
// keeps a field's or a forest's growth near the old calibration even though the road itself
// doesn't. The validator proves the seal on the finished map (containedPlotsMismatch over every
// hex's label).
//
// THE TERRAIN. The layout is drawn with one terrain per mechanic (grass, sand, water, deep water,
// road, gravel, scree, loam, boulders, ridge rock), so every measurement of the design (pure tiles,
// crop growth) sees exactly the rules the game will; a last pass paints the looks that share each
// mechanic (moss, spring meadow, clay, flower meadow, mud, deep snow, thicket, hedge,
// outcrop, boardwalk, dirt track, dark water, lush and dry grass, pale and dune sand). One look per
// kind of ground: one sand on every shore, solid fields, solid clay hills.
//
// HOW IT GOT HERE. Revisions 1-4 (2026-09-16, built with the maintainer over one session) began as
// "Rubble city", an urban-combat map of bombed square blocks, and became a honeycomb city of
// rubble-filled hexes on a lagoon: the hexagons read as "a Settlers of Catan board", and the
// lagoon, river, edge wheat fields and central orchard followed. Revision 5 (2026-11-01) rebuilt it
// on the terrain catalogue as the maintainer saw it, closer to the board game than to a ruined
// city: the rubble and statues are gone and every hex is a landscape of its own.
//
// TRADEOFFS made to make it work:
// - The island never fills the map. An island from edge to edge had almost no water, so it takes
//   at most three fifths of the map's hexes, hexes shrink to 16 before that, and a crowded map
//   squeezes colonies to three hexes each before refusing them. Homes keep off the river's banks,
//   so a small map that runs out of home hexes is laid out without the river.
// - Fairness is statistical: homes are spread as far apart as the island allows and dealt at
//   random; every colony gets the same home design (one per map), the same cistern, the same
//   starter crops, two food hexes and a forest beside its home (the river can add a water meadow)
//   and a share of the mountains between the homes. Hexes further out are dealt at random.
// - The deep sea beyond the shallows is a barrier even to swimmers (a tile mixing shallow and deep
//   corners is neither walkable nor swimmable), so swimming opens the river and the coast, not a
//   way across the sea to the island's far side.
namespace
{

constexpr int kHexPitchPercent = 150;
// Square hexes the same area as hexagons of a block size: 150% of it times the square root of a
// regular hexagon's area over its pitch squared (0.866).
constexpr int kSquarePitchPercent = 140;
// Hexes shrink, never below this, until the map has this many for every so many the island wants
// (5 to 3), which leaves the sea round it; colonies may be squeezed down to this many hexes each,
// their two home hexes and one more, before the map refuses them. Below 16 some home gardens could
// not hold their starter crops.
constexpr int kSmallestBlock = 16, kCityShareNumerator = 5, kCityShareDenominator = 3;
constexpr int kFewestBlocksPerColony = 3;
// A home's cistern garden: its own crops at every amount, then the scaled part on top.
constexpr int kHomeWheat = 22, kHomeWood = 12, kHomeWheatExtra = 10, kHomeWoodExtra = 6;
// A lake's garden crops at an amount of 100.
constexpr int kLakeWheat = 26, kLakeWood = 20;
// A cistern's pond and the ring of garden round it, in tiles.
constexpr double kCisternRadius = 2.2, kCisternGarden = 6.0;
// How far behind the midpoint between a home's two hexes its well stands, at most.
constexpr double kWellBehindMidway = 11.0;
// The weights of the hexes dealt at random: forests, pastures and hills; fields by the control.
constexpr int kForestWeight = 30, kPastureWeight = 24, kHillsWeight = 14;
// The food hexes beside every home.
constexpr int kHomeFood = 2;
// The wheat-fields control: the chance, in percent, that a riverside hex is a field, and the weight
// of a field among the dealt hexes, for Few, Normal and Many.
struct FieldShare
{
	int riverside, weight;
};
constexpr FieldShare kFieldShares[] = {{25, 14}, {50, 28}, {100, 44}};
// Mountains per colony for Few, Normal and Many, in halves.
constexpr int kMountainHalves[] = {1, 2, 3};
// Stone at the foot of a mountain and on a hill's knoll at an amount of 100.
constexpr int kMountainStone = 8, kHillStone = 2;
// The landmark orchard: groves at a fruit amount of 100, and the most any amount plants.
constexpr int kOrchardGroves = 2, kMostOrchardGroves = 6;
// How far the river wanders from its line, as a share of a hex's pitch; how far home hexes keep
// from its water, in tiles.
constexpr double kRiverWander = 0.35, kHomeRiverMargin = 3.0;
// The sea deepens this many vertices from land, and darkens (the same deep water, another look)
// further out.
constexpr int kDeepFromShore = 8, kDarkFromShore = 18;

enum class Block : unsigned char
{
	Outside,
	Home,
	Annex,
	Field,    // wheat on loam
	Bank,     // the river takes a fifth of it or more: a water meadow of wheat on its banks
	Forest,   // trees on moss round a thicket
	Pasture,  // open grass and flower meadow, a hedgerow across it
	Hills,    // clay with outcrop knolls
	Mountain, // a ridge-rock massif with a snow cap, scree and boulders, stone at its foot
	Lake,     // a pond with a dark middle and a garden round it, mud on its rim
	Landmark  // the desert oasis and its orchard
};
const char *const kBlockNames[] = {"outside", "home",     "annex", "field", "bank",    "forest",
								   "pasture", "hills", "mountain", "lake",  "landmark"};

// The per-map garden design every colony shares.
enum HomeDesign
{
	WellYard,    // the cistern in front of the home hex's middle
	GardenCourt, // the well on the road between the two hexes
	kHomeDesigns
};
const char *const kHomeDesignNames[] = {"well-yard", "garden-court"};

struct Plot
{
	std::vector<int> tiles;
	int wheat, wood, minimumWheat, minimumWood;
	// A field's plot is planted full of wheat, scaled by the amount; 0 for a garden planted to its
	// counts.
	int fillWheatShare = 0;
	bool garden = false;
	ShapePoint centre{0, 0};
	// The side of the pond the wheat takes, the wood the other: a home's faces its swarm. The wheat
	// is planted nearest `wheatToward` (a home's swarm), the wood nearest the pond.
	double heading = 0;
	ShapePoint wheatToward{0, 0};
};

struct Layout
{
	Torus t{1, 1};
	Tessellation g;
	// The terrain by mechanic, which every measurement reads, and the terrain the map is given:
	// the same rules, painted with the looks.
	TerrainSketch terrain, paint;
	std::vector<int> cell, homeOf, plotOf;
	// The validator's growth labels: each home's whole ground, every other hex's.
	std::vector<int> sealOf;
	std::vector<Block> kind; // per cell
	std::vector<unsigned char> street, stone, trees, orchard, river;
	std::vector<int> homeCell, annexCell, landmarkCell;
	std::vector<ShapePoint> swarms, wells, landmarks;
	std::vector<Plot> plots;
	int homeDesign = 0, bridges = 0;
	std::string failure;
	bool homesDidNotFit = false;
	// What the design observed, replayed into the generation's telemetry by design(): the design is
	// cached and a cached design must report the same records.
	GenerationTelemetry telemetry;
	// The crop growth chance of the finished terrain, which generate and validateWorld need too.
	Fertility::Field fertility;
};

// The offsets of the tiles within `radius` of a tile, as dilateRound counts them.
std::vector<std::pair<int, int>> discOffsets(double radius)
{
	std::vector<std::pair<int, int>> offsets;
	const int reach = int(std::floor(radius));
	for (int dy = -reach; dy <= reach; ++dy)
		for (int dx = -reach; dx <= reach; ++dx)
			if (double(dx * dx + dy * dy) <= radius * radius)
				offsets.push_back({dx, dy});
	return offsets;
}

// Every tile within `radius` of a tile of `mask`: dilateRound, by marking a disc round each mask
// tile, which is much cheaper than a distance transform when the mask is a small part of the map.
std::vector<unsigned char> scatterDisc(const Torus &t, const std::vector<unsigned char> &mask,
									   double radius)
{
	const std::vector<std::pair<int, int>> offsets = discOffsets(radius);
	std::vector<unsigned char> result(mask.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		if (mask[i])
			for (const auto &[dx, dy] : offsets)
				result[t.at(t.remainderX(i) + dx, i / t.w + dy)] = 1;
	return result;
}

ShapePoint cellCentre(const Layout &L, int cell)
{
	return tilePoint(L.g.cells[cell].centre);
}

// A vertex's terrain: its mechanic in `terrain`, and the look it is painted with when that look
// shares the mechanic (0 leaves the look to the last pass).
void setVertex(Layout &L, std::vector<unsigned char> &look, int i, TerrainType mechanic,
			   TerrainType painted)
{
	L.terrain[i] = mechanic;
	look[i] = painted;
}

// A garden: a round plot of grass with a pond at its middle and no sand ring, the roads round its
// hex holding its crops. Only tiles `within` accepts are taken.
template <typename Within>
void garden(Layout &L, GenerationContext &context, ShapePoint at, double pond, double grass,
			const Plot &crops, const char *stream, const Within &within)
{
	const Torus &t = L.t;
	std::vector<int> corners;
	const RadialShape outline(pond + grass, 0.12, context, stream);
	forEachTileInShape(t, at.x, at.y, outline, 0,
					   [&](int i, double, double)
					   {
						   if (within(i))
							   corners.push_back(i);
					   });
	Plot p = crops;
	p.tiles = corners;
	p.garden = true;
	p.centre = at;
	p.wheatToward = at;
	const RadialShape water(pond, 0.3, context, stream);
	fillShape(L.terrain, t, at.x, at.y, water, 0, WATER);
	for (int i : p.tiles)
		L.plotOf[i] = int(L.plots.size());
	L.plots.push_back(std::move(p));
}

// Farthest-point spreading over the free cells of the island, from every cell already `taken`.
int farthestFree(const Layout &L, const std::vector<unsigned char> &free,
				 const std::vector<long long> &nearest)
{
	int best = -1;
	for (int c = 0; c < L.g.cellCount(); ++c)
		if (free[c] && (best < 0 || nearest[c] > nearest[best]))
			best = c;
	return best;
}

Layout layout(const GenerationRequest &request, GenerationContext &context, int riverWidth)
{
	const HoneycombIsleOptions o(request);
	Layout L;
	L.telemetry.choice("honeycomb-isle.block-shape", o.blockShape == 0 ? "Squares" : "Hexagons");
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	const double cx = t.w / 2.0, cy = t.h / 2.0;

	// The hexes: a warped tiling, every edge a road. A map too small for the island the colonies
	// ask for gets smaller hexes, down to kSmallestBlock, so the island keeps its sea; a map too
	// small even then gets a smaller island (below).
	const int landmarks = teams > 4 ? 2 : 1;
	const int wanted = teams * o.blocksPerColony + landmarks;
	int blockSize = o.blockSize;
	const auto tiling = [&](int size)
	{
		return o.blockShape == 1 ? hexTessellation(t.w, t.h, size * kHexPitchPercent / 100)
								 : squareTessellation(t.w, t.h, size * kSquarePitchPercent / 100);
	};
	L.g = tiling(blockSize);
	while (blockSize > kSmallestBlock && L.g.cellCount() * kCityShareDenominator <
											 wanted * kCityShareNumerator)
		L.g = tiling(--blockSize);
	L.telemetry.measure("honeycomb-isle.block-size", blockSize);
	if (blockSize < o.blockSize)
		L.telemetry.fallback("honeycomb-isle.blocks.shrunk",
							 "Hexes shrunk to fit the island on the map.");
	const std::vector<unsigned char> edges(L.g.edges.size(), 1);
	warpCorners(L.g, warpLimit(L.g) * o.warp / 100, edges, o.streetWidth + 6, blockSize / 4, context,
				"honeycomb-isle-warp");
	L.cell = labelTiles(L.g);
	if (L.cell.empty())
	{
		L.failure = "The blocks could not be labelled.";
		return L;
	}
	const int cells = L.g.cellCount();
	const int mostCity = cells * kCityShareDenominator / kCityShareNumerator;
	if (mostCity - landmarks < teams * kFewestBlocksPerColony)
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}

	// The river: a wandering band along one of the map's axes, all the way round the torus, a
	// little off the middle so the oasis keeps its ground.
	const double pitch = std::sqrt(double(n) / cells);
	L.river.assign(n, 0);
	std::vector<StrokePoint> riverPath;
	const bool alongX = context.bounded("honeycomb-isle-river", 2) == 0;
	if (riverWidth > 0)
	{
		const double side = context.bounded("honeycomb-isle-river", 2) ? 1.0 : -1.0;
		const double offset = side * pitch * 0.6;
		// Three legs round the torus: a path's two ends a whole map apart are the same point, and
		// a wandering path takes the short way from one to the other.
		const int span = alongX ? t.w : t.h;
		for (int leg = 0; leg < 3; ++leg)
		{
			const double a = span * leg / 3.0, b = span * (leg + 1) / 3.0;
			const ShapePoint from = alongX ? ShapePoint{a, cy + offset} : ShapePoint{cx + offset, a};
			const ShapePoint to = alongX ? ShapePoint{b, cy + offset} : ShapePoint{cx + offset, b};
			const std::vector<StrokePoint> part =
				wanderingPath(t, from, to, riverWidth / 2.0, pitch * kRiverWander, 0.15,
							  context.stream("honeycomb-isle-river"));
			riverPath.insert(riverPath.end(), part.begin() + (leg ? 1 : 0), part.end());
		}
		strokePath(L.river, t, riverPath);
	}
	std::vector<int> riverTiles(cells, 0), cellTiles(cells, 0);
	for (int i = 0; i < n; ++i)
	{
		++cellTiles[L.cell[i]];
		riverTiles[L.cell[i]] += L.river[i];
	}
	std::vector<unsigned char> nearRiver(cells, 0);
	{
		const std::vector<unsigned char> margin =
			scatterDisc(t, L.river, kHomeRiverMargin + o.streetWidth / 2.0);
		for (int i = 0; i < n; ++i)
			if (margin[i])
				nearRiver[L.cell[i]] = 1;
	}
	// A hex the river takes a fifth of is a bank hex; one it only clips keeps its kind.
	const auto isBank = [&](int c) { return riverTiles[c] * 5 >= cellTiles[c]; };

	// The island: the hexes nearest the middle, as many as the colonies need, bank hexes not
	// counted. Everything else is the sea.
	std::vector<int> byDistance(cells);
	std::iota(byDistance.begin(), byDistance.end(), 0);
	const auto fromMiddle = [&](int c)
	{
		const ShapePoint p = cellCentre(L, c);
		const double dx = t.offsetX(int(cx), int(p.x)), dy = t.offsetY(int(cy), int(p.y));
		return dx * dx + dy * dy;
	};
	std::stable_sort(byDistance.begin(), byDistance.end(),
					 [&](int a, int b) { return fromMiddle(a) < fromMiddle(b); });
	int cityCells = 0, counted = 0;
	for (; cityCells < mostCity && counted < wanted; ++cityCells)
		counted += !isBank(byDistance[cityCells]);
	L.telemetry.measure("honeycomb-isle.blocks.city-actual", cityCells);
	if (counted < wanted)
		L.telemetry.fallback("honeycomb-isle.city.reduced",
							 "The island was made smaller to keep the sea round it.");
	L.kind.assign(cells, Block::Outside);
	std::vector<unsigned char> free(cells, 0);
	int bankCells = 0;
	for (int k = 0; k < cityCells; ++k)
	{
		const int c = byDistance[k];
		const bool bank = isBank(c);
		free[c] = !bank;
		L.kind[c] = bank ? Block::Bank : Block::Pasture; // until dealt a kind
		bankCells += bank;
	}
	L.telemetry.measure("honeycomb-isle.blocks.banks", bankCells);
	std::vector<long long> nearest(cells, 0);
	const auto take = [&](int c)
	{
		free[c] = 0;
		for (int other = 0; other < cells; ++other)
			nearest[other] = std::min(nearest[other], L.g.distance2(other, c));
	};

	// The oasis at the middle; the homes as far from it and each other as the island allows, each
	// a home hex and the neighbour nearest the oasis, where the swarm stands.
	int middle = 0;
	while (!free[byDistance[middle]])
		++middle;
	L.landmarkCell.push_back(byDistance[middle]);
	for (int c = 0; c < cells; ++c)
		nearest[c] = L.g.distance2(c, byDistance[middle]);
	free[byDistance[middle]] = 0;
	for (int k = 0; k < teams; ++k)
	{
		int home = -1, annex = -1;
		// Homes keep off the river's banks, so no garden or swarm is crowded by its water; a small
		// map that runs out of such hexes may put a home on any hex the river does not reach into.
		for (const bool clearOfBanks : {true, false})
		{
			const auto homeGround = [&](int c)
			{ return free[c] && (clearOfBanks ? !nearRiver[c] : riverTiles[c] == 0); };
			std::vector<unsigned char> candidates(cells, 0);
			for (int c = 0; c < cells; ++c)
				candidates[c] = homeGround(c);
			home = annex = -1;
			while (annex < 0)
			{
				home = farthestFree(L, candidates, nearest);
				if (home < 0)
					break;
				candidates[home] = 0;
				for (int edge : L.g.cells[home].edges)
				{
					const int next = L.g.other(edge, home);
					if (next != home && homeGround(next) &&
						(annex < 0 || L.g.distance2(next, L.landmarkCell[0]) <
										  L.g.distance2(annex, L.landmarkCell[0])))
						annex = next;
				}
			}
			if (home >= 0)
			{
				if (!clearOfBanks)
					L.telemetry.fallback("honeycomb-isle.home.on-bank",
										 "A home stands on a hex beside the river.", k);
				break;
			}
		}
		if (home < 0)
		{
			L.homesDidNotFit = true;
			L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
			return L;
		}
		take(home);
		take(annex);
		L.homeCell.push_back(home);
		L.annexCell.push_back(annex);
		L.kind[home] = Block::Home;
		L.kind[annex] = Block::Annex;
	}
	{
		std::vector<int> order(teams);
		std::iota(order.begin(), order.end(), 0);
		dealStarts(context, order);
		std::vector<int> homes, annexes;
		for (int k : order)
		{
			homes.push_back(L.homeCell[k]);
			annexes.push_back(L.annexCell[k]);
		}
		L.homeCell = homes;
		L.annexCell = annexes;
	}
	// Every home borders exactly kHomeFood food hexes (fields, or water meadows the river already
	// made) and a forest of its own, like a settlement on the board: the free hexes beside its two
	// hexes nearest its swarm, fields first. No other field is dealt beside a home (below): a
	// rotation of four-AI games on independently dealt neighbours ran from one field beside a home
	// to four, and from 98 to 245 births per seat.
	std::vector<unsigned char> besideHome(cells, 0);
	int homeFields = 0, homeForests = 0;
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint swarm = cellCentre(L, L.annexCell[k]);
		std::vector<std::pair<double, int>> beside;
		std::vector<int> seen;
		int foodBeside = 0; // water meadows, and fields an earlier home was given
		for (int own : {L.homeCell[k], L.annexCell[k]})
			for (int edge : L.g.cells[own].edges)
			{
				const int next = L.g.other(edge, own);
				if (L.kind[next] == Block::Home || L.kind[next] == Block::Annex ||
					std::find(seen.begin(), seen.end(), next) != seen.end())
					continue;
				seen.push_back(next);
				besideHome[next] = 1;
				foodBeside += L.kind[next] == Block::Bank || L.kind[next] == Block::Field;
				if (free[next])
				{
					// Hexes shared with another home come last, so one home's field is not a
					// neighbour's extra food.
					bool shared = false;
					for (int e2 : L.g.cells[next].edges)
					{
						const int across = L.g.other(e2, next);
						shared |= (L.kind[across] == Block::Home || L.kind[across] == Block::Annex) &&
								  across != L.homeCell[k] && across != L.annexCell[k];
					}
					const ShapePoint p = cellCentre(L, next);
					const double dx = t.offsetX(int(swarm.x), int(p.x)), dy = t.offsetY(int(swarm.y), int(p.y));
					beside.push_back({dx * dx + dy * dy + (shared ? 1e9 : 0.0), next});
				}
			}
		std::sort(beside.begin(), beside.end());
		beside.erase(std::unique(beside.begin(), beside.end()), beside.end());
		std::vector<Block> owed(std::max(0, kHomeFood - foodBeside), Block::Field);
		owed.push_back(Block::Forest);
		if (foodBeside > kHomeFood)
			L.telemetry.fallback("honeycomb-isle.home.extra-food",
								 "A home borders more food hexes than its share.", k);
		for (size_t b = 0; b < owed.size() && b < beside.size(); ++b)
		{
			free[beside[b].second] = 0;
			L.kind[beside[b].second] = owed[b];
			(owed[b] == Block::Field ? homeFields : homeForests) += 1;
		}
		if (beside.size() < owed.size())
			L.telemetry.fallback("honeycomb-isle.home.short-of-neighbours",
								 "A home has too few free hexes beside it for its field and forest.", k);
	}
	L.telemetry.measure("honeycomb-isle.home.fields", homeFields);
	L.telemetry.measure("honeycomb-isle.home.forests", homeForests);
	// A second oasis on a crowded island, then the lakes and the mountains, each as far as it can be
	// from what was placed before: between the homes, where neighbours meet.
	for (int k = 1; k < landmarks; ++k)
		if (const int c = farthestFree(L, free, nearest); c >= 0)
		{
			take(c);
			L.landmarkCell.push_back(c);
		}
	for (int c : L.landmarkCell)
		L.kind[c] = Block::Landmark;
	const auto spread = [&](int count, Block kind, const char *key)
	{
		int placed = 0;
		for (; placed < count; ++placed)
		{
			const int c = farthestFree(L, free, nearest);
			if (c < 0)
				break;
			take(c);
			L.kind[c] = kind;
		}
		L.telemetry.measure(std::string("honeycomb-isle.") + key + ".target", count);
		L.telemetry.measure(std::string("honeycomb-isle.") + key + ".actual", placed);
		if (placed < count)
			L.telemetry.fallback(std::string("honeycomb-isle.") + key + ".reduced",
								 "The island ran out of hexes.");
	};
	spread(o.lakes * teams, Block::Lake, "lakes");
	spread(std::max(1, kMountainHalves[std::clamp(o.mountains, 0, 2)] * teams / 2), Block::Mountain,
		   "mountains");
	// Every other hex is dealt from a shuffled deck of fields, forests, pastures and hills, as the
	// board game deals its tiles: independent draws left one seed with a single pasture and twelve
	// forests. Half the hexes by the river (all of them with Many fields) are fields first. The deck
	// tops up the island's totals, counting the fields and forests already beside the homes and by
	// the river, to the proportions of the weights.
	const FieldShare &share = kFieldShares[std::clamp(o.wheatFields, 0, 2)];
	std::vector<int> dealt(int(Block::Landmark) + 1, 0), remaining;
	for (int c = 0; c < cells; ++c)
	{
		if (!free[c])
			continue;
		free[c] = 0;
		if (nearRiver[c] && !besideHome[c] &&
			int(context.bounded("honeycomb-isle-riverside", 100)) < share.riverside)
			L.kind[c] = Block::Field;
		else
			remaining.push_back(c);
	}
	for (int c = 0; c < cells; ++c)
		++dealt[int(L.kind[c])];
	dealt[int(Block::Pasture)] -= int(remaining.size()); // their placeholder until dealt
	{
		const Block suits[] = {Block::Field, Block::Forest, Block::Pasture, Block::Hills};
		const int weights[] = {share.weight, kForestWeight, kPastureWeight, kHillsWeight};
		const int weightSum = weights[0] + weights[1] + weights[2] + weights[3];
		int already = 0;
		for (Block b : suits)
			already += dealt[int(b)];
		const int total = already + int(remaining.size());
		int cards[4], sum = 0;
		for (int s = 0; s < 4; ++s)
		{
			cards[s] = std::max(0, total * weights[s] / weightSum - dealt[int(suits[s])]);
			sum += cards[s];
		}
		// Rounding and the hexes already dealt leave the deck short or long; settle it on the suits
		// furthest below or above their share.
		const auto gap = [&](int s)
		{ return (dealt[int(suits[s])] + cards[s]) * weightSum - total * weights[s]; };
		while (sum != int(remaining.size()))
		{
			int pick = -1;
			for (int s = 0; s < 4; ++s)
				if ((sum > int(remaining.size()) ? cards[s] > 0 : true) &&
					(pick < 0 || (sum > int(remaining.size()) ? gap(s) > gap(pick) : gap(s) < gap(pick))))
					pick = s;
			cards[pick] += sum > int(remaining.size()) ? -1 : 1;
			sum += sum > int(remaining.size()) ? -1 : 1;
		}
		std::vector<Block> deck;
		for (int s = 0; s < 4; ++s)
			deck.insert(deck.end(), cards[s], suits[s]);
		for (int k = int(deck.size()) - 1; k > 0; --k)
			std::swap(deck[k], deck[context.bounded("honeycomb-isle-deck", unsigned(k + 1))]);
		// A field or forest card dealt beside a home changes places with the next card that is
		// neither, so a home's other neighbours are building ground (pastures, hills) and its share of
		// food and wood is the same as every other home's.
		int moved = 0;
		const auto owedKind = [](Block b) { return b == Block::Field || b == Block::Forest; };
		for (size_t k = 0; k < remaining.size(); ++k)
			if (besideHome[remaining[k]] && owedKind(deck[k]))
				for (size_t j = k + 1; j < deck.size(); ++j)
					if (!owedKind(deck[j]))
					{
						std::swap(deck[k], deck[j]);
						++moved;
						break;
					}
		L.telemetry.measure("honeycomb-isle.deck.moved", moved);
		for (size_t k = 0; k < remaining.size(); ++k)
		{
			L.kind[remaining[k]] = deck[k];
			++dealt[int(deck[k])];
		}
	}
	for (int k = int(Block::Field); k <= int(Block::Landmark); ++k)
		L.telemetry.measure(std::string("honeycomb-isle.blocks.") + kBlockNames[k], dealt[k]);
	// What each home ended up beside, after the deck: its food and wood hexes.
	for (int k = 0; k < teams; ++k)
	{
		std::vector<int> seen;
		int food = 0, forest = 0;
		for (int own : {L.homeCell[k], L.annexCell[k]})
			for (int edge : L.g.cells[own].edges)
			{
				const int next = L.g.other(edge, own);
				if (std::find(seen.begin(), seen.end(), next) != seen.end())
					continue;
				seen.push_back(next);
				food += L.kind[next] == Block::Field || L.kind[next] == Block::Bank;
				forest += L.kind[next] == Block::Forest;
			}
		L.telemetry.measure("honeycomb-isle.home.food-beside", food, k);
		L.telemetry.measure("honeycomb-isle.home.forests-beside", forest, k);
	}
	L.homeDesign = int(context.bounded("honeycomb-isle-home-design", kHomeDesigns));
	L.telemetry.choice("honeycomb-isle.home-design", kHomeDesignNames[L.homeDesign]);
	L.telemetry.measure("honeycomb-isle.blocks.city", wanted);

	// Roads: a band along every border between two hexes, on the island.
	std::vector<unsigned char> border(n, 0), inCity(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		inCity[i] = L.kind[L.cell[i]] != Block::Outside;
		for (const auto &s : kCardinalSteps)
			if (L.cell[t.at(x + s[0], y + s[1])] != L.cell[i])
				border[i] = 1;
	}
	L.street = scatterDisc(t, border, o.streetWidth / 2.0);
	std::vector<unsigned char> interior(n, 0);
	for (int i = 0; i < n; ++i)
	{
		L.street[i] = L.street[i] && inCity[i];
		interior[i] = inCity[i] && !L.street[i];
	}
	const std::vector<int> depth = clearance(t, interior);

	L.terrain.assign(n, GRASS);
	std::vector<unsigned char> look(n, 0);
	L.homeOf.assign(n, -1);
	L.plotOf.assign(n, -1);
	L.stone.assign(n, 0);
	L.trees.assign(n, 0);
	L.orchard.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const Block b = L.kind[L.cell[i]];
		if (b == Block::Home || b == Block::Annex)
			for (int k = 0; k < teams; ++k)
				if (L.cell[i] == L.homeCell[k] || L.cell[i] == L.annexCell[k])
					L.homeOf[i] = k;
	}
	// A home's own roads stay grass, except where its hexes meet another colony's: two homes'
	// grounds are kept apart by road like every other hex.
	const auto paved = [&](int i)
	{
		if (!L.street[i])
			return false;
		if (L.homeOf[i] < 0)
			return true;
		for (int dy = -2; dy <= 2; ++dy)
			for (int dx = -2; dx <= 2; ++dx)
			{
				const int other = L.homeOf[t.at(t.remainderX(i) + dx, i / t.w + dy)];
				if (other >= 0 && other != L.homeOf[i])
					return true;
			}
		return false;
	};
	std::vector<unsigned char> pavedMask(n, 0);
	for (int i = 0; i < n; ++i)
	{
		pavedMask[i] = paved(i);
		L.terrain[i] = !inCity[i] || L.river[i] ? WATER : pavedMask[i] ? TRAIL : GRASS;
	}
	// The road's shoulders: its vertices beside the ground of a hex are gravel, which carries no
	// deposit either and holds back the growth probe as the old sand streets did.
	for (int i = 0; i < n; ++i)
	{
		if (L.terrain[i] != TRAIL)
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				if (inCity[j] && !pavedMask[j] && !L.river[j])
					L.terrain[i] = GRAVEL;
			}
	}

	const auto nearPoint = [&](int i, ShapePoint p, double r)
	{ return t.dist2(t.remainderX(i), i / t.w, int(std::lround(p.x)), int(std::lround(p.y))) <= r * r; };
	// The tiles of a cell, row-major; collected once.
	std::vector<std::vector<int>> tilesOf(cells);
	for (int i = 0; i < n; ++i)
		if (inCity[i])
			tilesOf[L.cell[i]].push_back(i);
	const int inner = std::max(4, centreClearance(L.g) - o.streetWidth / 2);
	L.telemetry.measure("honeycomb-isle.hex.inner-radius", inner);

	// Homes: open gardens round the cistern.
	const std::vector<std::pair<int, int>> sharedMargin = discOffsets(o.streetWidth / 2.0 + 2.5);
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint home = cellCentre(L, L.homeCell[k]);
		const ShapePoint annex = tilePoint(L.g.centreAcross(
			[&]
			{
				for (int edge : L.g.cells[L.homeCell[k]].edges)
					if (L.g.other(edge, L.homeCell[k]) == L.annexCell[k])
						return edge;
				return L.g.cells[L.homeCell[k]].edges[0];
			}(),
			L.homeCell[k]));
		const double axis = std::atan2(annex.y - home.y, annex.x - home.x);
		// The home's own ground, kept clear of the roads round it: the two hexes and the road
		// between them, less a margin from every other road.
		const auto own = [&, k](int i)
		{
			if (L.homeOf[i] != k)
				return false;
			for (const auto &[dx, dy] : sharedMargin)
			{
				const int j = t.at(t.remainderX(i) + dx, i / t.w + dy);
				if (L.street[j] && L.homeOf[j] != k)
					return false;
			}
			return true;
		};
		// The court design puts its well on the road between the hexes, the other in front of the
		// home hex's middle: 2.5 tiles at the default size, and further forward on bigger hexes,
		// so the garden's wheat stays in a worker's reach of the swarm.
		const double apart = std::hypot(annex.x - home.x, annex.y - home.y);
		const double lead =
			L.homeDesign == GardenCourt ? apart / 2 : std::max(2.5, apart / 2 - kWellBehindMidway);
		const ShapePoint well = polarPoint(home.x, home.y, lead, axis);
		L.wells.push_back(well);
		L.swarms.push_back(annex);
		garden(L, context, well, kCisternRadius, kCisternGarden,
			   {{}, kHomeWheatExtra, kHomeWoodExtra, kHomeWheat, kHomeWood}, "honeycomb-isle-cistern",
			   own);
		L.plots.back().heading = std::atan2(annex.y - well.y, annex.x - well.x);
		L.plots.back().wheatToward = annex;
	}

	// The landscape of every other hex. Each draws its own shapes from its own stream.
	const double pondRadius = std::clamp(inner * 0.28, 2.5, 5.0);
	const std::vector<int> drift = periodicNoise(t.w, t.h, 5, context.stream("honeycomb-isle-drifts"));
	const std::vector<int> grain = periodicNoise(t.w, t.h, 3, context.stream("honeycomb-isle-grain"));
	int lakes = 0, mountains = 0, hills = 0, pastures = 0, forests = 0, fields = 0, hedges = 0;
	std::vector<unsigned char> quarry(n, 0), forestGround(n, 0), lakeDeep(n, 0);
	for (int c = 0; c < cells; ++c)
	{
		const Block kind = L.kind[c];
		const ShapePoint centre = cellCentre(L, c);
		const auto inside = [&, c](int i) { return L.cell[i] == c && interior[i]; };
		// The block's inside eroded by `margin` tiles, so nothing drawn here touches a road.
		const auto deep = [&, c](int i, int margin) { return L.cell[i] == c && interior[i] && depth[i] > margin; };
		const auto offset = [&](int i, ShapePoint p, double &dx, double &dy)
		{
			dx = t.offsetX(int(std::lround(p.x)), t.remainderX(i));
			dy = t.offsetY(int(std::lround(p.y)), i / t.w);
		};
		switch (kind)
		{
		case Block::Field:
		case Block::Bank:
		{
			// The whole field is loam, which waters it; a bank hex is a water meadow, the same crop
			// land in spring meadow. Earlier furrows of loam and grass read as too busy (maintainer
			// playtest, 2026-11-01).
			std::vector<int> corners;
			for (int i : tilesOf[c])
			{
				if (!interior[i] || L.river[i])
					continue;
				corners.push_back(i);
				setVertex(L, look, i, LOAM, kind == Block::Bank ? SPRING_MEADOW : LOAM);
			}
			Plot p{corners, 0, 0, 0, 0, 100};
			for (int i : p.tiles)
				L.plotOf[i] = int(L.plots.size());
			L.plots.push_back(std::move(p));
			++fields;
			break;
		}
		case Block::Forest:
		{
			// Trees on a floor of moss, which waters them, round a thicket that never clears.
			const RadialShape core(std::clamp(inner * 0.2, 1.6, 3.0), 0.35, context, "honeycomb-isle-forests");
			const double heart = core.maximumRadius() + 2.5;
			for (int i : tilesOf[c])
			{
				if (!inside(i))
					continue;
				forestGround[i] = 1;
				if (nearPoint(i, centre, heart) && depth[i] > 2)
					setVertex(L, look, i, LOAM, MOSS);
			}
			fillShape(L.terrain, t, centre.x, centre.y, core, 0, BOULDERS);
			forEachTileInShape(t, centre.x, centre.y, core, 0, [&](int i, double, double) { look[i] = THICKET; });
			++forests;
			break;
		}
		case Block::Pasture:
		{
			// Open grass with drifts of flower meadow, and a hedgerow across it with a gap in its
			// middle: cover, never a wall round anything.
			for (int i : tilesOf[c])
				if (deep(i, 1) && drift[i] > 65536 * 62 / 100)
					setVertex(L, look, i, GRAVEL, FLOWER_MEADOW);
			const double heading = context.bounded("honeycomb-isle-hedges", 3600) * kPi / 3600;
			const double shift = (int(context.bounded("honeycomb-isle-hedges", 7)) - 3) * inner / 8.0;
			const double ux = std::cos(heading), uy = std::sin(heading);
			const double gap = 2.5, reach = inner * 0.75;
			bool any = false;
			for (int i : tilesOf[c])
			{
				if (!deep(i, 2))
					continue;
				double dx, dy;
				offset(i, centre, dx, dy);
				const double along = dx * ux + dy * uy, across = -dx * uy + dy * ux - shift;
				if (std::abs(across) <= 0.75 && std::abs(along) >= gap && std::abs(along) <= reach)
				{
					setVertex(L, look, i, BOULDERS, HEDGE);
					any = true;
				}
			}
			hedges += any;
			++pastures;
			break;
		}
		case Block::Hills:
		{
			// Clay, with two or three outcrop knolls; one knoll keeps a pocket of grass at its foot
			// for its stone.
			for (int i : tilesOf[c])
				if (inside(i))
					setVertex(L, look, i, GRAVEL, CLAY);
			const int knolls = 2 + int(context.bounded("honeycomb-isle-hills", 2));
			for (int k = 0; k < knolls; ++k)
			{
				const double angle = context.bounded("honeycomb-isle-hills", 3600) * 2 * kPi / 3600;
				const double distance = inner * (0.15 + context.bounded("honeycomb-isle-hills", 40) / 100.0);
				const ShapePoint at = polarPoint(centre.x, centre.y, distance, angle);
				const RadialShape knoll(1.6 + context.bounded("honeycomb-isle-hills", 9) / 10.0, 0.3, context,
										"honeycomb-isle-hills");
				if (k == 0)
				{
					// The pocket beside the first knoll, on the side towards the hex's middle.
					const ShapePoint foot = polarPoint(at.x, at.y, knoll.maximumRadius() + 2.0, angle + kPi);
					for (int i : tilesOf[c])
						if (deep(i, 1) && nearPoint(i, foot, 2.2))
						{
							setVertex(L, look, i, GRASS, GRASS);
							quarry[i] = 2;
						}
				}
				forEachTileInShape(t, at.x, at.y, knoll, 0,
								   [&](int i, double, double)
								   {
									   if (deep(i, 1))
										   setVertex(L, look, i, RIDGE_ROCK, OUTCROP);
								   });
			}
			++hills;
			break;
		}
		case Block::Mountain:
		{
			// A ridge-rock massif with a cap of snow, a skirt of scree and boulders, and grass at
			// its foot where the quarry is.
			const RadialShape massif(std::clamp(inner * 0.45, 3.0, 7.0), 0.3, context, "honeycomb-isle-mountains");
			const double peak = massif.maximumRadius();
			const double skirt = peak + 2.5, foot = peak + 4.5;
			for (int i : tilesOf[c])
			{
				if (!inside(i))
					continue;
				double dx, dy;
				offset(i, centre, dx, dy);
				const double r = std::hypot(dx, dy);
				if (r <= skirt && deep(i, 1))
				{
					const TerrainType rubble = grain[i] > 65536 * 78 / 100 ? BOULDERS : SCREE;
					setVertex(L, look, i, rubble, rubble);
				}
				else if (r <= foot && deep(i, 1))
					quarry[i] = 1;
			}
			forEachTileInShape(t, centre.x, centre.y, massif, 0,
							   [&](int i, double dx, double dy)
							   {
								   if (!deep(i, 1))
									   return;
								   const double r = std::hypot(dx, dy);
								   const bool cap = r <= peak * 0.4;
								   setVertex(L, look, i, cap ? SCREE : RIDGE_ROCK,
											 cap ? DEEP_SNOW : grain[i] > 65536 * 70 / 100 ? OUTCROP : RIDGE_ROCK);
							   });
			++mountains;
			break;
		}
		case Block::Lake:
		{
			// A pond with a dark deep middle, a garden round it, and a rim of mud.
			const double shift = context.bounded("honeycomb-isle-lakes", 16) / 10.0;
			const double heading = context.bounded("honeycomb-isle-lakes", 3600) * 2 * kPi / 3600;
			const ShapePoint at = polarPoint(centre.x, centre.y, shift, heading);
			const auto eroded = [&](int i)
			{
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
						if (!inside(t.at(t.remainderX(i) + dx, i / t.w + dy)))
							return false;
				return true;
			};
			const double lake = pondRadius * 1.15, ring = 3.5;
			garden(L, context, at, lake, ring, {{}, kLakeWheat, kLakeWood, 0, 0}, "honeycomb-isle-lakes",
				   eroded);
			L.plots.back().heading = heading;
			for (int i : tilesOf[c])
			{
				if (!deep(i, 1))
					continue;
				double dx, dy;
				offset(i, at, dx, dy);
				const double r = std::hypot(dx, dy);
				if (r <= lake - 2.0 && L.terrain[i] == WATER)
					lakeDeep[i] = 1; // drawn after the beaches, which take any other terrain for land
				else if (r >= lake + ring + 1.0 && r <= lake + ring + 2.2 && L.plotOf[i] < 0)
					setVertex(L, look, i, SCREE, MUD);
			}
			++lakes;
			break;
		}
		case Block::Landmark:
		{
			// The desert: dune sand round an oasis pond, the orchard on the grass about it.
			const int groves = std::clamp(int(scaledCount(kOrchardGroves, o.fruit)), 0, kMostOrchardGroves);
			const double orchardRadius = groves ? 3.5 + std::max(0, groves - kOrchardGroves) * 0.75 : 0.0;
			const double meadow = std::max(4.0, orchardRadius + 3.5);
			L.landmarks.push_back(centre);
			for (int i : tilesOf[c])
			{
				if (!inside(i))
					continue;
				double dx, dy;
				offset(i, centre, dx, dy);
				const double r = std::hypot(dx, dy);
				if (r > meadow && depth[i] > 1)
					setVertex(L, look, i, SAND, DUNE_SAND);
				else
					look[i] = r > meadow - 1.5 ? DRY_GRASS : LUSH_GRASS;
				if (r <= orchardRadius && r >= 2.0)
					L.orchard[i] = 1;
			}
			const RadialShape oasis(1.6, 0.25, context, "honeycomb-isle-oasis");
			fillShape(L.terrain, t, centre.x, centre.y, oasis, 0, WATER);
			break;
		}
		default:
			break;
		}
	}
	L.telemetry.measure("honeycomb-isle.orchard.groves",
						std::clamp(int(scaledCount(kOrchardGroves, o.fruit)), 0, kMostOrchardGroves));
	L.telemetry.measure("honeycomb-isle.fields", fields);
	L.telemetry.measure("honeycomb-isle.forests", forests);
	L.telemetry.measure("honeycomb-isle.pastures", pastures);
	L.telemetry.measure("honeycomb-isle.pastures.hedged", hedges);
	L.telemetry.measure("honeycomb-isle.hills", hills);
	L.telemetry.measure("honeycomb-isle.mountains", mountains);
	L.telemetry.measure("honeycomb-isle.lakes", lakes);

	// Bridges: boardwalk across the river where it runs through the island, evenly spaced along
	// it, with clear ground at both ends and a dirt track on through any field to the first road.
	std::vector<unsigned char> landing(n, 0), deck(n, 0);
	if (!riverPath.empty())
	{
		std::vector<int> spans;
		for (size_t k = 0; k < riverPath.size(); ++k)
		{
			const int i = t.at(int(std::lround(riverPath[k].x)), int(std::lround(riverPath[k].y)));
			if (inCity[i])
				spans.push_back(int(k));
		}
		const int count = spans.empty() ? 0 : o.bridges + teams / 4;
		for (int b = 0; b < count; ++b)
		{
			const StrokePoint &at = riverPath[spans[(2 * b + 1) * spans.size() / (2 * count)]];
			const double reach = riverWidth / 2.0 + 6.0;
			const ShapePoint a = alongX ? ShapePoint{at.x, at.y - reach} : ShapePoint{at.x - reach, at.y};
			const ShapePoint e = alongX ? ShapePoint{at.x, at.y + reach} : ShapePoint{at.x + reach, at.y};
			std::vector<unsigned char> span(n, 0), approach(n, 0);
			strokePath(span, t, {{a.x, a.y, 1.5}, {e.x, e.y, 1.5}});
			strokePath(approach, t, {{a.x, a.y, 3.5}, {e.x, e.y, 3.5}});
			for (int i = 0; i < n; ++i)
			{
				// The deck is laid after the beaches; until then it holds the beach back as sand.
				if (span[i] && inCity[i] && L.terrain[i] == WATER)
				{
					L.terrain[i] = SAND;
					deck[i] = 1;
				}
				landing[i] = landing[i] || approach[i];
			}
			const double nx = alongX ? 0.0 : 1.0, ny = alongX ? 1.0 : 0.0;
			for (const double side : {-1.0, 1.0})
			{
				for (int d = 0; d <= int(pitch); ++d)
				{
					const double px = at.x + side * nx * d, py = at.y + side * ny * d;
					const int middle = t.at(int(std::lround(px)), int(std::lround(py)));
					if (L.river[middle])
						continue;
					if (L.street[middle] || !inCity[middle])
						break;
					for (int w = -1; w <= 1; ++w)
					{
						const int i = t.at(int(std::lround(px + ny * w)), int(std::lround(py + nx * w)));
						if (!L.street[i] && L.homeOf[i] < 0 && inCity[i] && !L.river[i])
						{
							setVertex(L, look, i, TRAIL, DIRT_TRACK);
							landing[i] = 1;
						}
					}
				}
			}
			++L.bridges;
		}
	}
	L.telemetry.measure("honeycomb-isle.bridges", L.bridges);

	layBeaches(L.terrain, t);
	for (int i = 0; i < n; ++i)
		if (lakeDeep[i] && L.terrain[i] == WATER)
			setVertex(L, look, i, DEEP_WATER, DARK_WATER);
	for (int i = 0; i < n; ++i)
		if (deck[i])
			setVertex(L, look, i, TRAIL, BOARDWALK);
	// The sea deepens away from the shore. A tile mixing shallow and deep corners is neither
	// walkable nor swimmable, so the deep sea is a barrier; it is drawn only outside the island,
	// never in the river.
	{
		std::vector<unsigned char> water(n, 0);
		for (int i = 0; i < n; ++i)
			water[i] = L.terrain[i] == WATER || L.terrain[i] == DEEP_WATER;
		const std::vector<int> offshore = clearance(t, water);
		// The clearance is counted in king's moves, so its contours are squares; noise breaks them up.
		const std::vector<int> swell = periodicNoise(t.w, t.h, 9, context.stream("honeycomb-isle-swell"));
		int deepTiles = 0;
		for (int i = 0; i < n; ++i)
		{
			const int lift = swell[i] * 7 / 65536 - 3;
			if (L.terrain[i] == WATER && !inCity[i] && offshore[i] + lift >= kDeepFromShore)
			{
				setVertex(L, look, i, DEEP_WATER,
						  offshore[i] - lift >= kDarkFromShore ? DARK_WATER : DEEP_WATER);
				++deepTiles;
			}
		}
		L.telemetry.measure("honeycomb-isle.sea.deep", deepTiles);
	}

	// Crops grow on tiles whose four corners are grass or loam; only such tiles carry plots.
	const auto cropCorner = [&](int i) { return L.terrain[i] == GRASS || L.terrain[i] == LOAM; };
	std::vector<unsigned char> crop(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		crop[i] = cropCorner(i) && cropCorner(t.at(x + 1, y)) && cropCorner(t.at(x, y + 1)) &&
				  cropCorner(t.at(x + 1, y + 1));
	}
	const std::vector<unsigned char> grass = pureTiles(L.terrain, t, GRASS);
	for (Plot &p : L.plots)
	{
		for (int i : p.tiles)
			if (!crop[i])
				L.plotOf[i] = -1;
		p.tiles.erase(std::remove_if(p.tiles.begin(), p.tiles.end(), [&](int i) { return !crop[i]; }),
					  p.tiles.end());
	}
	// The growth field on the sketch with deep water as water: the two irrigate alike, and a tile
	// mixing them irrigates as either, which the sketch's pure-tile field reads only this way.
	{
		TerrainSketch shallow = L.terrain;
		for (unsigned char &v : shallow)
			if (v == DEEP_WATER)
				v = WATER;
		L.fertility = cropGrowthField(shallow, t);
	}
	const Fertility::Field &fertility = L.fertility;

	// Forests: trees on the forest's crop ground in noisy stands, as full as the forest control
	// and the wood amount make them. Stone: at a mountain's foot and in a hill's pocket.
	const std::vector<int> stands = periodicNoise(t.w, t.h, 4, context.stream("honeycomb-isle-stands"));
	const int density = std::clamp(int(scaledCount(o.forest + 10, o.wood)), 0, 100);
	L.telemetry.measure("honeycomb-isle.forest.share", density);
	std::vector<unsigned char> treeGround(n, 0);
	for (int i = 0; i < n; ++i)
		treeGround[i] = forestGround[i] && crop[i] && !landing[i] && depth[i] >= 1;
	const std::vector<unsigned char> standShare = noisyShare(treeGround, stands, density);
	int treeTiles = 0;
	for (int i = 0; i < n; ++i)
		if (standShare[i])
		{
			L.trees[i] = 1;
			++treeTiles;
		}
	L.telemetry.measure("honeycomb-isle.forest.trees", treeTiles);
	{
		std::vector<std::pair<int, int>> mountainFoot, hillPocket;
		for (int i = 0; i < n; ++i)
			if (quarry[i] && grass[i] && !landing[i] && !L.street[i])
				(quarry[i] == 1 ? mountainFoot : hillPocket).push_back({-grain[i], i});
		std::sort(mountainFoot.begin(), mountainFoot.end());
		std::sort(hillPocket.begin(), hillPocket.end());
		// Each mountain and each hill takes its own share of its best-ranked foot tiles.
		const auto quarryStone = [&](const std::vector<std::pair<int, int>> &ranked, int perHex)
		{
			std::vector<int> taken(cells, 0);
			int placed = 0;
			for (const auto &[rank, i] : ranked)
				if (taken[L.cell[i]] < perHex)
				{
					L.stone[i] = 1;
					++taken[L.cell[i]];
					++placed;
				}
			return placed;
		};
		L.telemetry.measure("honeycomb-isle.stone.mountain",
							quarryStone(mountainFoot, int(scaledCount(kMountainStone, o.stone))));
		L.telemetry.measure("honeycomb-isle.stone.hills",
							quarryStone(hillPocket, int(scaledCount(kHillStone, o.stone))));
	}

	// Every crop tile is labelled for the validator: a home's whole ground is one label (its garden
	// crops may grow through it), every other hex's crop ground another, sealed by its roads.
	L.sealOf.assign(n, -1);
	const int firstHexLabel = int(L.plots.size());
	for (int i = 0; i < n; ++i)
	{
		if (!crop[i])
			continue;
		L.sealOf[i] = L.homeOf[i] >= 0 ? firstHexLabel + cells + L.homeOf[i] : firstHexLabel + L.cell[i];
	}
	int regrowing = 0;
	for (int i = 0; i < n; ++i)
		regrowing += L.trees[i] && fertility.at(t.remainderX(i), i / t.w) > 0;
	L.telemetry.measure("honeycomb-isle.forest.regrowing", regrowing);
	L.telemetry.measure("honeycomb-isle.plots", L.plots.size());

	// The looks. Grass is lush where it is tended (homes, forests, water meadows, the oasis) and
	// plain elsewhere: shading it by its growth chance blotched pastures and hillsides. Every beach
	// is pale sand and the desert dune sand; everything else keeps the look it was drawn with.
	L.paint = L.terrain;
	for (int i = 0; i < n; ++i)
	{
		const TerrainType mechanic = TerrainType(L.terrain[i]);
		const TerrainType chosen = TerrainType(look[i]);
		if (look[i] && terrainGroup(chosen) == terrainGroup(mechanic))
		{
			L.paint[i] = chosen;
			continue;
		}
		const Block b = L.kind[L.cell[i]];
		if (mechanic == GRASS && (L.homeOf[i] >= 0 || b == Block::Forest || b == Block::Bank))
			L.paint[i] = LUSH_GRASS;
		else if (mechanic == SAND)
			// One beach sand everywhere (three sands on one shore looked wrong; maintainer playtest,
			// 2026-11-01); the desert keeps its dunes.
			L.paint[i] = b == Block::Landmark ? DUNE_SAND : PALE_SAND;
	}
	return L;
}

bool sameDesign(const GenerationRequest &a, const GenerationRequest &b)
{
	return a.method == b.method && a.wDec == b.wDec && a.hDec == b.hDec && a.nbTeams == b.nbTeams &&
		   a.nbWorkers == b.nbWorkers && a.seed == b.seed && a.options == b.options;
}

// The whole layout as a pure function of the request and the context's named streams. Homes keep
// off the river, so on a small map the river can leave too few hexes for them; then the island is
// laid out again without it, and only a map too small even then refuses the colonies.
//
// A generation asks for the same design three times: the request check (designFailure), generate
// and validateWorld. The design depends only on the request, so the last design built on this
// thread is kept and handed out again for the same request, its telemetry replayed so each context
// records what building it would have.
Layout design(const GenerationRequest &request, GenerationContext &context)
{
	struct Cache
	{
		bool valid = false;
		GenerationRequest request;
		Layout layout;
	};
	thread_local Cache cache;
	if (!cache.valid || !sameDesign(cache.request, request))
	{
		const HoneycombIsleOptions o(request);
		cache.valid = false;
		cache.layout = layout(request, context, o.riverWidth);
		if (cache.layout.homesDidNotFit && o.riverWidth > 0)
		{
			GenerationTelemetry both = cache.layout.telemetry;
			both.fallback("honeycomb-isle.river.dropped",
						  "The river left too few hexes for the homes; laid out without it.");
			cache.layout = layout(request, context, 0);
			both.replay(cache.layout.telemetry);
			cache.layout.telemetry = std::move(both);
		}
		cache.request = request;
		cache.valid = true;
	}
	context.telemetry.replay(cache.layout.telemetry);
	return cache.layout;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "honeycomb isle layout";
	const HoneycombIsleOptions o(context.request);
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

	context.stage = "honeycomb isle terrain";
	writeVertices(map, L.paint);
	// Quarry stone and forest trees go down before the colonies: neither is ever on a home.
	int stoneTiles = 0;
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if (L.stone[i] && map.isResourceAllowed(x, y, STONE))
		{
			map.setResourceByIndex(x, y, STONE, 1);
			++stoneTiles;
		}
		else if (L.trees[i] && map.isResourceAllowed(x, y, WOOD))
			map.setResourceByIndex(x, y, WOOD, 1);
	}
	context.telemetry.measure("honeycomb-isle.stone.tiles", stoneTiles);

	context.stage = "honeycomb isle colonies";
	if (!settleColonies(
			game, context, "honeycomb-isle-starts",
			[&](int k)
			{
				auto mask = homeGrassMask(map, t, L.homeOf, k);
				for (int i = 0; i < n; ++i)
					if (L.plotOf[i] >= 0)
						mask[i] = 0;
				return mask;
			},
			[&](int k)
			{
				return MapGeneratorPoint(int(std::lround(L.swarms[k].x)) - 2,
										 int(std::lround(L.swarms[k].y)) - 2);
			}))
		return false;

	context.stage = "honeycomb isle resources";
	const Fertility::Field &fertility = L.fertility;
	for (size_t k = 0; k < L.plots.size(); ++k)
	{
		const Plot &p = L.plots[k];
		const int size = int(p.tiles.size()), wheatShare = size * p.fillWheatShare / 100;
		// A field is planted to its corners, dry corners included: its roads hold them.
		const int wheat = p.fillWheatShare
							  ? int(std::min<std::int64_t>(wheatShare, scaledCount(wheatShare, o.wheat)))
							  : p.minimumWheat + int(scaledCount(p.wheat, o.wheat));
		const int wood =
			p.fillWheatShare
				? int(std::min<std::int64_t>(size - wheatShare, scaledCount(size - wheatShare, o.wood)))
				: p.minimumWood + int(scaledCount(p.wood, o.wood));
		const bool renewable = !p.fillWheatShare;
		int plantedWheat = 0, plantedWood = 0;
		if (p.garden)
		{
			// A garden's crops go round its pond, wheat in one broad half facing `heading` and wood
			// in the other: one solid patch beside the swarm reads well and is what the AIs look for.
			const ShapePoint well = p.centre;
			const auto byDistanceTo = [&](ShapePoint to)
			{
				std::vector<std::pair<int, int>> order;
				for (int i : p.tiles)
					order.push_back(
						{t.dist2(t.remainderX(i), i / t.w, int(std::lround(to.x)), int(std::lround(to.y))), i});
				std::sort(order.begin(), order.end());
				return order;
			};
			const std::vector<std::pair<int, int>> order = byDistanceTo(well);
			const double ux = std::cos(p.heading), uy = std::sin(p.heading);
			const auto wheatSide = [&](int i)
			{
				return t.offsetX(int(std::lround(well.x)), t.remainderX(i)) * ux +
						   t.offsetY(int(std::lround(well.y)), i / t.w) * uy >=
					   0;
			};
			// The wheat nearest the swarm on its own side of the pond: on big hexes the swarm stands
			// far from the cistern, and wheat planted nearest the pond was 25 steps from it.
			for (const auto &[distance, i] : byDistanceTo(p.wheatToward))
			{
				const int x = t.remainderX(i), y = i / t.w;
				if (plantedWheat < wheat && wheatSide(i) && clearGround(map, x, y) &&
					map.isResourceAllowed(x, y, WHEAT))
				{
					map.setResourceByIndex(x, y, WHEAT, 1);
					++plantedWheat;
				}
			}
			for (const auto &[distance, i] : order)
			{
				const int x = t.remainderX(i), y = i / t.w;
				if (plantedWood < wood && !wheatSide(i) && clearGround(map, x, y) &&
					map.isResourceAllowed(x, y, WOOD))
				{
					map.setResourceByIndex(x, y, WOOD, 1);
					++plantedWood;
				}
			}
			// A garden clipped by its home's edge can lose most of a half: whatever its halves
			// could not hold goes on the nearest clear tiles left.
			for (const auto &[distance, i] : order)
			{
				const int x = t.remainderX(i), y = i / t.w;
				if (!clearGround(map, x, y))
					continue;
				if (plantedWood < wood && map.isResourceAllowed(x, y, WOOD))
				{
					map.setResourceByIndex(x, y, WOOD, 1);
					++plantedWood;
				}
				else if (plantedWheat < wheat && map.isResourceAllowed(x, y, WHEAT))
				{
					map.setResourceByIndex(x, y, WHEAT, 1);
					++plantedWheat;
				}
			}
		}
		else
		{
			plantedWheat = plantContainedPlot(map, t, p.tiles, fertility, WHEAT, wheat, renewable);
			plantedWood = plantContainedPlot(map, t, p.tiles, fertility, WOOD, wood, renewable);
		}
		if (plantedWheat < wheat || plantedWood < wood)
			context.telemetry.fallback("honeycomb-isle.plot.saturated", "Eligible plot tiles exhausted",
									   int(k));
		context.telemetry.measure("honeycomb-isle.plot.tiles", p.tiles.size(), int(k));
		context.telemetry.measure("honeycomb-isle.plot.wheat", plantedWheat, int(k));
		context.telemetry.measure("honeycomb-isle.plot.wood", plantedWood, int(k));
		if (plantedWheat < p.minimumWheat || plantedWood < p.minimumWood)
		{
			context.detail = "A cistern garden cannot hold its colony's starting crops (" +
							 std::to_string(plantedWheat) + " wheat, " + std::to_string(plantedWood) +
							 " wood of " + std::to_string(p.tiles.size()) + " tiles).";
			return false;
		}
	}
	const int groves = std::clamp(int(scaledCount(kOrchardGroves, o.fruit)), 0, kMostOrchardGroves);
	if (groves > 0)
		for (const ShapePoint &c : L.landmarks)
		{
			const double spin = context.bounded("honeycomb-isle-fruit", 3600) * 2 * kPi / 3600;
			std::vector<double> angles;
			for (int g = 0; g < groves; ++g)
				angles.push_back(spin + 2 * kPi * g / groves);
			plantOrchard(map, t, context, c.x, c.y, 2.0 + std::max(0, groves - kOrchardGroves) * 0.5,
						 angles, 2.5, 3, 1,
						 [&](int i)
						 { return L.orchard[i] && clearGround(map, t.remainderX(i), i / t.w); });
		}
	seedAlgae(map, context, t, "honeycomb-isle-algae", o.algae, AlgaeBand::shallows(0, 4, 60));
	// No generic crop or route repair: an unrestricted top-up would break the hexes' seal. The
	// validator refuses a colony short of crops or room instead.
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "honeycomb isle"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int teams = context.request.nbTeams;
	for (int i = 0; i < t.size(); ++i)
		if (map.vertexTerrainAt(t.remainderX(i), i / t.w) != TerrainType(L.paint[i]))
			return "The terrain at " + std::to_string(t.remainderX(i)) + "," + std::to_string(i / t.w) +
				   " is not the design's.";
	// Every hex is sealed by its roads: no crop or tree may grow out of the hex it is in.
	const Fertility::Field &fertility = L.fertility;
	if (const std::string error = containedPlotsMismatch(map, t, L.sealOf, &fertility); !error.empty())
		return error;
	if (const std::string error = homePondMissing(map, t, L.wells, teams, "home", "cistern");
		!error.empty())
		return error;
	for (int i = 0; i < t.size(); ++i)
		if (L.street[i] && L.homeOf[i] < 0 && map.terrainPropertiesAt(t.remainderX(i), i / t.w).walkable &&
			map.getResource(t.remainderX(i), i / t.w).type != NO_RES_TYPE)
			return "A road is blocked by a deposit at " + std::to_string(t.remainderX(i)) + "," +
				   std::to_string(i / t.w) + ".";
	if (const std::string error =
			walkFromFirstColony(map, teams, "the island", "along the roads").error;
		!error.empty())
		return error;
	return startingAccessFailure(map, teams, {{MaterialId::Food, 24, "food"}, {MaterialId::Wood, 32, "wood"}}, 16, 24);
}
} // namespace

HoneycombIsleOptions::HoneycombIsleOptions(const GenerationRequest &r)
	: blockShape(r.option("block-shape")), blockSize(r.option("block-size")),
	  streetWidth(r.option("street-width")), warp(r.option("warp")),
	  blocksPerColony(r.option("blocks-per-colony")), forest(r.option("forest-density")),
	  lakes(r.option("lakes")), mountains(r.option("mountains")),
	  riverWidth(r.option("river-width")), wheatFields(r.option("wheat-fields")),
	  bridges(r.option("bridges")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
	blockShape = resolveDesignChoice<design>(r, "block-shape", 2, "honeycomb-isle/block-shape");
}

GeneratorDefinition honeycombIsleDefinition()
{
	return {
		"honeycomb-isle",
		53,
		"Honeycomb isle",
		// Revision 5: a board of terrain hexes on the terrain catalogue (fields, forests, pastures,
		// hills, mountains, lakes, a desert oasis), cobbled roads and a deep sea.
		5,
		false,
		// Hexes of 18 with roads of 3 leave a hex's inside about 15 tiles across, room for a
		// cistern garden and a swarm's neighbourhood, or a mountain and its quarry.
		{GeneratorControl::choice("block-shape", "Block shape", {"Squares", "Hexagons", "Random"},
								  1, ControlGroup::Terrain)
			 .withSearchValues({0, 1}),
		 GeneratorControl{"block-size", "Block size", 16, 22, 1, 18, ControlGroup::Layout}
			 .withSearchRange(18, 22),
		 GeneratorControl{"street-width", "Street width", 2, 5, 1, 3, ControlGroup::Terrain}
			 .withSearchRange(3, 5),
		 GeneratorControl{"warp", "Warp", 0, 100, 10, 50, ControlGroup::Terrain}.withSearchRange(
			 20, 80),
		 GeneratorControl{"blocks-per-colony", "Blocks per colony", 8, 16, 1, 11,
						  ControlGroup::Layout}
			 .withSearchRange(10, 14),
		 GeneratorControl{"forest-density", "Forest density", 20, 90, 5, 70, ControlGroup::Terrain}
			 .withSearchRange(45, 80),
		 GeneratorControl{"lakes", "Lakes", 0, 3, 1, 1, ControlGroup::Layout}.withSearchRange(1, 2),
		 GeneratorControl::choice("mountains", "Mountain ranges", {"Few", "Normal", "Many"}, 1,
								  ControlGroup::Layout)
			 .withSearchValues({0, 1, 2}),
		 GeneratorControl::choice("wheat-fields", "Wheat fields", {"Few", "Normal", "Many"}, 1,
								  ControlGroup::Layout)
			 .withSearchValues({0, 1, 2}),
		 GeneratorControl{"river-width", "River width", 0, 20, 1, 12, ControlGroup::Terrain}
			 .withSearchRange(8, 16),
		 GeneratorControl{"bridges", "Bridges", 1, 6, 1, 3, ControlGroup::Layout}.withSearchRange(
			 2, 5),
		 // Fields are planted full at 100 and forests are full by 150-200, so higher amounts would
		 // change nothing. The stone amount scales the quarries at the mountains and hills.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount", 100),
		 GeneratorControl::percentage("wood-amount", "Wood amount", 200),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		designFailure<design>,
		validateWorld,
		{"terrain:novelty", "feature:hexagons", "feature:islands", "feature:river", "feature:farmland",
		 "feature:forest", "feature:mountains", "feature:lakes", "feature:orchard",
		 "style:contested-center"}};
}
