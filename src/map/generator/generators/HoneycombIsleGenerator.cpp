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
#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>
using namespace MapGeneration;

// Honeycomb isle: a city of hexagon blocks on an island in a lagoon, cut in two by a wide river
// that a few bridges cross. Paved streets draw the honeycomb. Pale wheat fields ring the island's
// edge and line the river; the blocks inside are ruins, stone outlines broken open onto the street
// and filled with rubble that has grown over, with a few open squares, crater gardens round flooded
// bomb craters, and the shell of a landmark at the middle (a stadium, a cathedral or a station)
// with an orchard growing in it. Every colony's home is two blocks with a cistern garden.
//
// HOW IT PLAYS. The city is only as big as the colonies need (blocks per colony), and the lagoon
// and river cannot be crossed until units swim, so colonies start a block or two apart and nobody
// expands away from the fight. Food is on the island's edge and along the river; the ruins between
// the homes are where they meet. Rubble is wood: it blocks walking and building, so it is cover and
// chokepoint, and it is the city's lumber, so a colony builds its town by clearing its own cover,
// and cutting through a ruin opens a way into a neighbour's flank. Stone outlines cannot be
// cleared, so the ruins keep their shape and only their gaps and fill change hands. The bridges are
// where the two halves of the city meet, and the landmark's orchard is the prize at the middle.
//
// WHY IT WORKS (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Every street is paved, and a
// line of sand corners seals the ground either side of it, so every block is a sealed plot of its
// own: its crops and its rubble can grow only inside it, and no pond or field needs a sand ring.
// The validator proves it on the finished map (containedPlotsMismatch over every block's label).
//
// HOW IT GOT HERE (2026-09-16, built with the maintainer over one session; the lessons are in
// .agents/skills/glob2-map-design). It began as "Rubble city", an urban-combat map meant to force
// colonies to build close together and fight in tight quarters: a bombed-out city of square blocks
// on a scorched sand plain, whose rubble was wood planted only where water could never make it
// grow, so every tile cleared stayed cleared, with water only in sand-ringed cisterns and craters.
// Looking at it changed it, step by step: the blocks became hexagons (they read as a city, and "a
// Settlers of Catan board"); a map with "no water" got the lagoon round the city and the river
// through it; the blocks along the edge and the river became wheat fields; the sand rings came off
// every pond once the paved streets were known to seal each block; the river was made twice as
// wide. By the end it was, in the maintainer's words, no longer urban combat but original and
// unlike any other map, and it was renamed for how it looks.
//
// TRADEOFFS made to make it work:
// - Rubble near water regrows inside its block. Keeping rubble only where it could never grow lost
//   a third of the ruins once the lagoon and river were added; sealed blocks keep the look instead
//   of the "cleared for good" rule.
// - The city never fills the map. A city from edge to edge had almost no water, so the city takes
//   at most three fifths of the map's blocks, blocks shrink to 16 before that, and a crowded map
//   squeezes colonies to three blocks each (cramped is the point) before refusing them. Homes keep
//   off the river's banks, so a small map that runs out of home blocks is laid out without the
//   river.
// - Fairness is statistical, like Old Town's: homes are spread as far apart as the city allows and
//   dealt at random, and every colony gets the same home design (one per map), the same cistern and
//   the same starter crops; the ruins, craters and fields between them differ.
// - The older AIs (Numbi, Castor, Warrush) stall here while Nicowar, Cortex, Cabino and Maxima
//   grow; by the maintainer's decision the map is not tuned for them.
namespace
{

constexpr int kHexPitchPercent = 150;
// Square blocks the same area as hexagons of a block size: 150% of it times the square root of a
// regular hexagon's area over its pitch squared (0.866). Squares at the raw block size gave a city
// a fifth of the building sites (bulk study, 2026-09-16).
constexpr int kSquarePitchPercent = 140;
// Blocks shrink, never below this, until the map has this many blocks for every so many the city
// wants (5 to 3), which leaves a ring of lagoon round it. A map still too small at the smallest
// block gets a smaller city, never more than that share of its blocks: a city from edge to edge
// left a map with hardly any water (FEEDBACK 2026-09-16: "way too little water, without the lagoon
// surrounding the city"). A crowded city is the map's point, so colonies may be squeezed down to
// this many blocks each, their two home blocks and one more, before the map refuses them. Below 16,
// some home gardens could not hold their starter crops.
constexpr int kSmallestBlock = 16, kCityShareNumerator = 5, kCityShareDenominator = 3;
constexpr int kFewestBlocksPerColony = 3;
// A home's cistern garden: its own crops at every amount, then the scaled part on top. The starter
// wheat is one broad patch beside the swarm (see generate), not a ring round the pond.
constexpr int kHomeWheat = 22, kHomeWood = 12, kHomeWheatExtra = 10, kHomeWoodExtra = 6;
// A crater garden's crops at an amount of 100.
constexpr int kCraterWheat = 26, kCraterWood = 20;
// A cistern's pond and the ring of garden round it, in tiles.
constexpr double kCisternRadius = 2.2, kCisternGarden = 6.0;
// How far behind the midpoint between a home's two blocks its well stands, at most.
constexpr double kWellBehindMidway = 11.0;
// Weights of the ordinary blocks: ruins with standing outlines, collapsed blocks, open squares and
// wheat fields.
constexpr int kRuinWeight = 50, kCollapsedWeight = 22, kSquareWeight = 16;
// The wheat-fields control: the chance, in percent, that an edge block and a riverside block is a
// field, and the weight of a field among the inner blocks, for Few, Normal and Many.
struct FieldShare
{
	int edge, riverside, innerWeight;
};
// Many used to weigh inner fields 25 and measured only 9% more wheat than Normal, the edge being
// all fields already (bulk study, 2026-09-16).
constexpr FieldShare kFieldShares[] = {{50, 25, 0}, {100, 50, 12}, {100, 100, 60}};
// The landmark orchard: groves (each a cherry, an orange and a prune clump) at a fruit amount of
// 100, and the most any amount plants.
constexpr int kOrchardGroves = 2, kMostOrchardGroves = 6;
// How far the river wanders from its line, as a share of a block's pitch; how far home blocks
// keep from its water, in tiles.
constexpr double kRiverWander = 0.35, kHomeRiverMargin = 3.0;

enum class Block : unsigned char
{
	Outside,
	Home,
	Annex,
	Ruin,
	Collapsed,
	Crater,
	Field, // a block of wheat round a pond, sealed by its streets
	Bank, // the river takes a fifth of it or more: a wheat field on its banks, no pond
	Square,
	Landmark
};

// The per-map designs every colony shares, and the landmark's.
enum HomeDesign
{
	WellYard,     // the cistern in front of the home block's middle, nothing else standing
	WalledCellar, // the same well, with the back half of the block's outline still standing
	GardenCourt,  // the well on the street between the two blocks, stone posts round its court
	kHomeDesigns
};
enum LandmarkDesign
{
	Stadium,   // a thick ring of stone with four gates
	Cathedral, // a ring and rows of pillars
	Station,   // long platforms side by side
	kLandmarkDesigns
};
const char *const kHomeDesignNames[] = {"well-yard", "walled-cellar", "garden-court"};
const char *const kLandmarkDesignNames[] = {"stadium", "cathedral", "station"};

struct Plot
{
	std::vector<int> tiles;
	int wheat, wood, minimumWheat, minimumWood;
	// A wheat field's plot is planted full, wheat on this share and wood on the rest, scaled by the
	// amounts; 0 for a garden planted to its counts.
	int fillWheatShare = 0;
	// A garden's crops are planted round its pond at `centre`; a field's fill its block.
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
	TerrainSketch terrain;
	std::vector<int> cell, homeOf, plotOf;
	// The validator's growth labels: every plot, each home's whole ground and each crater's block.
	std::vector<int> sealOf;
	std::vector<Block> kind; // per cell
	std::vector<unsigned char> street, stone, rubble, orchard, river;
	std::vector<int> homeCell, annexCell, craterCell, landmarkCell;
	std::vector<ShapePoint> swarms, wells, landmarks;
	std::vector<Plot> plots;
	int homeDesign = 0, landmarkDesign = 0, bridges = 0;
	std::string failure;
	bool homesDidNotFit = false;
	// What the design observed, replayed into the generation's telemetry by design(): the design is
	// cached and a cached design must report the same records.
	GenerationTelemetry telemetry;
	// The crop growth chance of the finished terrain, which generate and validateWorld need too.
	Fertility::Field fertility;
};

// The offsets of the tiles within `radius` of a tile, as dilateRound counts them (squared distance
// at most the radius squared): a local disc scan gives the same answer as dilating a whole map.
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
				result[t.at(i % t.w + dx, i / t.w + dy)] = 1;
	return result;
}

ShapePoint cellCentre(const Layout &L, int cell)
{
	return tilePoint(L.g.cells[cell].centre);
}

// A garden: a round plot of grass with a pond at its middle and no sand ring, the paved streets
// round its block holding its crops. Only tiles `within` accepts are taken, so a garden never
// reaches into a street other colonies use.
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

// Farthest-point spreading over the free cells of the city, from every cell already `taken`.
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
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	const double cx = t.w / 2.0, cy = t.h / 2.0;

	// The blocks: a warped tiling, every edge a street. A map too small for the city the colonies
	// ask for gets smaller blocks, down to kSmallestBlock (FEEDBACK 2026-09-16: "it can also shrink
	// the size of the blocks when needed"), so the city keeps its lagoon; a map too small even then
	// gets a smaller city (below).
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
							 "Blocks shrunk to fit the city on the map.");
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
	// little off the middle so the landmark keeps its ground. The city is cut in two and the lagoon
	// joins its ends.
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
	// A block the river takes a fifth of is a bank block; one it only clips keeps its kind.
	const auto isBank = [&](int c) { return riverTiles[c] * 5 >= cellTiles[c]; };

	// The city: the blocks nearest the middle, as many as the colonies need, bank blocks not
	// counted. Everything else is the lagoon.
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
	// Bank blocks count towards the lagoon's share, since the city's outline is what keeps it.
	int cityCells = 0, counted = 0;
	for (; cityCells < mostCity && counted < wanted; ++cityCells)
		counted += !isBank(byDistance[cityCells]);
	L.telemetry.measure("honeycomb-isle.blocks.city-actual", cityCells);
	if (counted < wanted)
		L.telemetry.fallback("honeycomb-isle.city.reduced",
							 "The city was made smaller to keep the lagoon round it.");
	L.kind.assign(cells, Block::Outside);
	std::vector<unsigned char> free(cells, 0);
	int bankCells = 0;
	for (int k = 0; k < cityCells; ++k)
	{
		const int c = byDistance[k];
		const bool bank = isBank(c);
		free[c] = !bank;
		L.kind[c] = bank ? Block::Bank : Block::Square; // until dealt a kind
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

	// The landmark at the middle; the homes as far from it and each other as the city allows, each
	// a home block and the neighbour nearest the landmark, where the swarm stands.
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
		// map that runs out of such blocks may put a home on any block the river does not reach
		// into.
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
										 "A home stands on a block beside the river.", k);
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
	// A second landmark on a crowded city, then the craters, each as far as it can be from the
	// water already placed: between the homes, where neighbours meet.
	for (int k = 1; k < landmarks; ++k)
		if (const int c = farthestFree(L, free, nearest); c >= 0)
		{
			take(c);
			L.landmarkCell.push_back(c);
		}
	for (int c : L.landmarkCell)
		L.kind[c] = Block::Landmark;
	const int craters = o.craters * teams;
	for (int k = 0; k < craters; ++k)
	{
		const int c = farthestFree(L, free, nearest);
		if (c < 0)
			break;
		take(c);
		L.craterCell.push_back(c);
		L.kind[c] = Block::Crater;
	}
	L.telemetry.measure("honeycomb-isle.craters.target", craters);
	L.telemetry.measure("honeycomb-isle.craters.actual", L.craterCell.size());
	if (int(L.craterCell.size()) < craters)
		L.telemetry.fallback("honeycomb-isle.craters.reduced",
							 "The city ran out of blocks for craters.");
	// Every block on the city's edge, where it meets the lagoon, or beside the river, is a wheat
	// field (FEEDBACK 2026-09-16: "basically all of the ones around the outer edge"); every other
	// city block is dealt a ruin, a collapsed block, a square or a field.
	int ruins = 0, collapsed = 0, squares = 0, edgeFields = 0;
	for (int c = 0; c < cells; ++c)
	{
		if (!free[c])
			continue;
		bool edge = false;
		for (int e : L.g.cells[c].edges)
			edge |= L.kind[L.g.other(e, c)] == Block::Outside;
		// So is about half of the blocks along the river (FEEDBACK 2026-09-16: "more of the cells
		// along the river wheat filled", then a few more ruins back); the blocks the river runs
		// through are all fields.
		const FieldShare &share = kFieldShares[std::clamp(o.wheatFields, 0, 2)];
		if ((edge && int(context.bounded("honeycomb-isle-edge", 100)) < share.edge) ||
			(nearRiver[c] && int(context.bounded("honeycomb-isle-riverside", 100)) < share.riverside))
		{
			L.kind[c] = Block::Field;
			++edgeFields;
			continue;
		}
		const int draw = int(context.bounded(
			"honeycomb-isle-blocks", kRuinWeight + kCollapsedWeight + kSquareWeight + share.innerWeight));
		L.kind[c] = draw < kRuinWeight										? Block::Ruin
					: draw < kRuinWeight + kCollapsedWeight					? Block::Collapsed
					: draw < kRuinWeight + kCollapsedWeight + kSquareWeight ? Block::Square
																			: Block::Field;
		ruins += L.kind[c] == Block::Ruin;
		collapsed += L.kind[c] == Block::Collapsed;
		squares += L.kind[c] == Block::Square;
	}
	L.homeDesign = int(context.bounded("honeycomb-isle-home-design", kHomeDesigns));
	L.landmarkDesign = int(context.bounded("honeycomb-isle-landmark-design", kLandmarkDesigns));
	L.telemetry.choice("honeycomb-isle.home-design", kHomeDesignNames[L.homeDesign]);
	L.telemetry.choice("honeycomb-isle.landmark-design", kLandmarkDesignNames[L.landmarkDesign]);
	L.telemetry.measure("honeycomb-isle.blocks.city", wanted);
	L.telemetry.measure("honeycomb-isle.blocks.ruins", ruins);
	L.telemetry.measure("honeycomb-isle.blocks.collapsed", collapsed);
	L.telemetry.measure("honeycomb-isle.blocks.squares", squares);
	L.telemetry.measure("honeycomb-isle.blocks.edge-fields", edgeFields);

	// Streets: a band along every border between two cells, inside the city.
	std::vector<unsigned char> border(n, 0), inCity(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
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
	L.homeOf.assign(n, -1);
	L.plotOf.assign(n, -1);
	L.stone.assign(n, 0);
	L.rubble.assign(n, 0);
	L.orchard.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const Block b = L.kind[L.cell[i]];
		if (b == Block::Home || b == Block::Annex)
			for (int k = 0; k < teams; ++k)
				if (L.cell[i] == L.homeCell[k] || L.cell[i] == L.annexCell[k])
					L.homeOf[i] = k;
	}
	// A home's own streets stay grass, except where its blocks meet another colony's: two homes'
	// grounds are kept apart by sand like every other block.
	const auto paved = [&](int i)
	{
		if (!L.street[i])
			return false;
		if (L.homeOf[i] < 0)
			return true;
		for (int dy = -2; dy <= 2; ++dy)
			for (int dx = -2; dx <= 2; ++dx)
			{
				const int other = L.homeOf[t.at(i % t.w + dx, i / t.w + dy)];
				if (other >= 0 && other != L.homeOf[i])
					return true;
			}
		return false;
	};
	for (int i = 0; i < n; ++i)
		L.terrain[i] = !inCity[i] || L.river[i] ? WATER : paved(i) ? SAND : GRASS;

	// An edge's midpoint on a cell's outline: where a gate or a gap in the outline goes.
	const auto edgeMiddle = [&](int edge, int c)
	{
		const auto ends = L.g.edgeEnds(edge, c);
		const ShapePoint a = tilePoint(ends.first), b = tilePoint(ends.second);
		return ShapePoint{(a.x + b.x) / 2, (a.y + b.y) / 2};
	};
	const auto nearPoint = [&](int i, ShapePoint p, double r)
	{ return t.dist2(i % t.w, i / t.w, int(std::lround(p.x)), int(std::lround(p.y))) <= r * r; };
	// The tiles of a cell, row-major; collected once.
	std::vector<std::vector<int>> tilesOf(cells);
	for (int i = 0; i < n; ++i)
		if (inCity[i])
			tilesOf[L.cell[i]].push_back(i);

	// Homes: a garden round the cistern and the design's standing stone.
	const std::vector<std::pair<int, int>> sharedMargin = discOffsets(o.streetWidth / 2.0 + 2.5);
	const double gap = o.streetWidth / 2.0 + 2.2;
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
		// The home's own ground, kept clear of the streets round it: the two blocks and the street
		// between them, less a margin from every other street.
		const auto own = [&, k](int i)
		{
			if (L.homeOf[i] != k)
				return false;
			for (const auto &[dx, dy] : sharedMargin)
			{
				const int j = t.at(i % t.w + dx, i / t.w + dy);
				if (L.street[j] && L.homeOf[j] != k)
					return false;
			}
			return true;
		};
		// The court design puts its well on the street between the blocks, the others in front of
		// the home block's middle: 2.5 tiles at the default block size, and further forward on
		// bigger blocks, so the garden's wheat stays in a worker's reach of the swarm (final
		// reliability pass, 2026-09-16: at blocks of 22 the swarm stood 34 tiles from a well 2.5
		// tiles forward, and its nearest wheat was 25 to 27 steps away).
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
		if (L.homeDesign == WalledCellar)
			// The back half of the home block's outline still stands; the front, towards the annex,
			// fell.
			for (int i : tilesOf[L.homeCell[k]])
			{
				const double dx = t.offsetX(int(home.x), i % t.w), dy = t.offsetY(int(home.y), i / t.w);
				if (depth[i] == 1 && dx * std::cos(axis) + dy * std::sin(axis) < -2.0)
					L.stone[i] = 1;
			}
		if (L.homeDesign == GardenCourt)
			// Stone posts at the corners of the court round the well.
			for (int corner = 0; corner < 4; ++corner)
			{
				const ShapePoint p =
					polarPoint(well.x, well.y, kCisternRadius + kCisternGarden + 2.5,
							   axis + kPi / 4 + corner * kPi / 2);
				L.stone[t.at(int(std::lround(p.x)), int(std::lround(p.y)))] = 1;
			}
	}

	// Craters: a pond blown into the block, its garden round it.
	const double craterPond = std::clamp(centreClearance(L.g) * 0.28, 2.5, 5.0);
	for (int c : L.craterCell)
	{
		const ShapePoint centre = cellCentre(L, c);
		const double drift = context.bounded("honeycomb-isle-craters", 16) / 10.0;
		const double heading = context.bounded("honeycomb-isle-craters", 3600) * 2 * kPi / 3600;
		// The block's inside eroded by a tile: an inside tile whose eight neighbours are inside
		// too.
		const auto inside = [&, c](int i) { return L.cell[i] == c && interior[i]; };
		const auto eroded = [&](int i)
		{
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					if (!inside(t.at(i % t.w + dx, i / t.w + dy)))
						return false;
			return true;
		};
		garden(L, context, polarPoint(centre.x, centre.y, drift, heading), craterPond, 3.5,
			   {{}, kCraterWheat, kCraterWood, 0, 0}, "honeycomb-isle-craters", eroded);
		L.plots.back().heading = heading;
	}
	// Wheat fields: a whole block's ground inside its streets one sealed plot, planted full of
	// wheat round a small pond that keeps it growing back.
	int fields = 0;
	for (int c = 0; c < cells; ++c)
	{
		// A bank block, the river through it, is a field too, watered by the river instead of a
		// pond.
		const bool bank = L.kind[c] == Block::Bank;
		if (L.kind[c] != Block::Field && !bank)
			continue;
		std::vector<int> corners;
		for (int i : tilesOf[c])
			if (interior[i] && !L.river[i])
				corners.push_back(i);
		// The block's paved streets bound it on every side, so the field needs no sand of its own.
		Plot p{corners, 0, 0, 0, 0, 100};
		const ShapePoint centre = cellCentre(L, c);
		const RadialShape pond(craterPond * 0.7, 0.3, context, "honeycomb-isle-fields");
		if (!riverTiles[c])
			fillShape(L.terrain, t, centre.x, centre.y, pond, 0, WATER);
		for (int i : p.tiles)
			L.plotOf[i] = int(L.plots.size());
		L.plots.push_back(std::move(p));
		++fields;
	}
	L.telemetry.measure("honeycomb-isle.blocks.fields", fields);

	// The landmark's stone and the orchard at its heart, which grows with the fruit amount.
	const int groves = std::clamp(int(scaledCount(kOrchardGroves, o.fruit)), 0, kMostOrchardGroves);
	const double orchardRadius = groves ? 3.5 + std::max(0, groves - kOrchardGroves) * 0.75 : 0.0;
	L.telemetry.measure("honeycomb-isle.orchard.groves", groves);
	for (int c : L.landmarkCell)
	{
		const ShapePoint centre = cellCentre(L, c);
		L.landmarks.push_back(centre);
		std::vector<ShapePoint> gates;
		for (size_t e = 0; e < L.g.cells[c].edges.size(); e += 2)
			gates.push_back(edgeMiddle(L.g.cells[c].edges[e], c));
		for (int i : tilesOf[c])
		{
			if (!interior[i])
				continue;
			const int dx = t.offsetX(int(centre.x), i % t.w), dy = t.offsetY(int(centre.y), i / t.w);
			bool wall = false;
			switch (L.landmarkDesign)
			{
			case Stadium:
				wall = depth[i] == 2 || depth[i] == 3;
				break;
			case Cathedral:
				wall = depth[i] == 2 || (depth[i] >= 5 && dx % 3 == 0 && dy % 3 == 0 &&
										 dx * dx + dy * dy > 9);
				break;
			case Station:
				wall = depth[i] >= 2 && (dy + 64) % 4 == 0 && std::abs(dy) > 1;
				break;
			}
			for (const ShapePoint &g : gates)
				if (nearPoint(i, g, gap + 1))
					wall = false;
			if (wall)
				L.stone[i] = 1;
			if (nearPoint(i, centre, orchardRadius))
			{
				L.stone[i] = 0;
				L.orchard[i] = 1;
			}
		}
	}

	// Ruins and collapsed blocks: outlines with gaps, and rubble.
	const std::vector<int> fill = periodicNoise(t.w, t.h, 4, context.stream("honeycomb-isle-fill"));
	const std::vector<int> chunks = periodicNoise(t.w, t.h, 2, context.stream("honeycomb-isle-chunks"));
	std::vector<unsigned char> rubbleGround(n, 0);
	for (int c = 0; c < cells; ++c)
	{
		if (L.kind[c] != Block::Ruin && L.kind[c] != Block::Collapsed)
			continue;
		const bool ruin = L.kind[c] == Block::Ruin;
		std::vector<ShapePoint> gaps;
		if (ruin)
		{
			for (int edge : L.g.cells[c].edges)
				if (int(context.bounded("honeycomb-isle-gaps", 100)) < o.outlineGaps)
					gaps.push_back(edgeMiddle(edge, c));
			if (gaps.empty())
			{
				const auto &own = L.g.cells[c].edges;
				gaps.push_back(
					edgeMiddle(own[context.bounded("honeycomb-isle-gaps", std::uint32_t(own.size()))], c));
			}
		}
		for (int i : tilesOf[c])
		{
			if (!interior[i])
				continue;
			bool opening = false;
			for (const ShapePoint &g : gaps)
				opening |= nearPoint(i, g, gap);
			if (ruin && depth[i] == 1 && !opening)
				L.stone[i] = 1;
			else if (depth[i] >= (ruin ? 2 : 1))
				rubbleGround[i] = 1;
		}
	}
	// The rubble control sets how full a ruin is; the wood amount scales it like any other wood, so
	// at a wood amount of 0 the ruins stand empty.
	const int share = std::clamp(int(scaledCount(o.rubble + 10, o.wood)), 0, 100);
	L.telemetry.measure("honeycomb-isle.rubble.share", share);
	std::vector<unsigned char> rubbleShare = noisyShare(rubbleGround, fill, share);
	// Fallen masonry in the rubble: about 3% of it at a stone amount of 100, scaled by the amount.
	const int chunkShare = std::clamp(int(scaledCount(3, o.stone)), 0, 100);
	const int chunkCut = chunkShare ? 65536 - 65536 * chunkShare / 100 : 65536;
	for (int i = 0; i < n; ++i)
		if (rubbleShare[i] && chunks[i] >= chunkCut)
			L.stone[i] = 1;

	// Squares keep their ground open: a single statue at the middle.
	for (int c = 0; c < cells; ++c)
		if (L.kind[c] == Block::Square && o.stone > 0)
		{
			const ShapePoint p = cellCentre(L, c);
			const int i = t.at(int(std::lround(p.x)), int(std::lround(p.y)));
			if (interior[i] && depth[i] >= 3)
				L.stone[i] = 1;
		}

	// Bridges: sand across the river where it runs through the city, evenly spaced along it, with
	// clear ground at both ends.
	std::vector<unsigned char> landing(n, 0);
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
			// The deck is sand over the water; beyond each end the ground stays clear of rubble and
			// stone for a few tiles, so a bridge always lands on open ground.
			const double reach = riverWidth / 2.0 + 6.0;
			const ShapePoint a = alongX ? ShapePoint{at.x, at.y - reach} : ShapePoint{at.x - reach, at.y};
			const ShapePoint e = alongX ? ShapePoint{at.x, at.y + reach} : ShapePoint{at.x + reach, at.y};
			std::vector<unsigned char> deck(n, 0), approach(n, 0);
			strokePath(deck, t, {{a.x, a.y, 1.5}, {e.x, e.y, 1.5}});
			strokePath(approach, t, {{a.x, a.y, 3.5}, {e.x, e.y, 3.5}});
			for (int i = 0; i < n; ++i)
			{
				if (deck[i] && inCity[i] && L.terrain[i] == WATER)
					L.terrain[i] = SAND;
				landing[i] = landing[i] || approach[i];
			}
			// Beyond each end a sand causeway runs on through any field to the first street, so
			// wheat growing on a bank never closes a bridge.
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
						const Block b = L.kind[L.cell[i]];
						if ((b == Block::Field || b == Block::Bank) && !L.street[i] && L.homeOf[i] < 0)
						{
							L.terrain[i] = SAND;
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
	// Beaches and street sand spoil tiles; only pure grass carries anything.
	const std::vector<unsigned char> grass = pureTiles(L.terrain, t, GRASS);
	for (Plot &p : L.plots)
	{
		for (int i : p.tiles)
			if (!grass[i])
				L.plotOf[i] = -1;
		p.tiles.erase(std::remove_if(p.tiles.begin(), p.tiles.end(), [&](int i) { return !grass[i]; }),
					  p.tiles.end());
	}
	L.fertility = cropGrowthField(L.terrain, t);
	const Fertility::Field &fertility = L.fertility;
	// Every block is sealed by its paved streets, so rubble near water may regrow, but only inside
	// its own block: each rubble block is labelled as a plot of its own for the validator.
	int rubbleTiles = 0, regrowing = 0;
	const int firstBlockPlot = int(L.plots.size());
	for (int i = 0; i < n; ++i)
	{
		const bool usable = grass[i] && L.plotOf[i] < 0 && !L.river[i] && !landing[i];
		L.stone[i] = L.stone[i] && usable && !L.street[i];
		const bool dry = fertility.at(i % t.w, i / t.w) == 0;
		if (rubbleShare[i] && usable && L.homeOf[i] < 0 && !L.stone[i])
		{
			L.rubble[i] = 1;
			++rubbleTiles;
			regrowing += !dry;
		}
	}
	for (int i = 0; i < n; ++i)
	{
		const Block b = L.kind[L.cell[i]];
		if (grass[i] && L.plotOf[i] < 0 && (b == Block::Ruin || b == Block::Collapsed))
			L.plotOf[i] = firstBlockPlot + L.cell[i];
	}
	// No pond needs a ring of sand round its garden (FEEDBACK 2026-09-16: "the extra sand ring
	// around lakes"): the sand of the streets round a home's two blocks, or round a crater's block,
	// holds the crops, so that whole ground is one label.
	L.sealOf = L.plotOf;
	for (int i = 0; i < n; ++i)
	{
		if (!grass[i])
			continue;
		if (L.homeOf[i] >= 0)
			L.sealOf[i] = firstBlockPlot + cells + L.homeOf[i];
		else if (L.kind[L.cell[i]] == Block::Crater)
			L.sealOf[i] = firstBlockPlot + L.cell[i];
	}
	L.telemetry.measure("honeycomb-isle.rubble.regrowing", regrowing);
	L.telemetry.measure("honeycomb-isle.rubble.tiles", rubbleTiles);
	L.telemetry.measure("honeycomb-isle.plots", L.plots.size());
	return L;
}

bool sameDesign(const GenerationRequest &a, const GenerationRequest &b)
{
	return a.method == b.method && a.wDec == b.wDec && a.hDec == b.hDec && a.nbTeams == b.nbTeams &&
		   a.nbWorkers == b.nbWorkers && a.seed == b.seed && a.options == b.options;
}

// The whole layout as a pure function of the request and the context's named streams. Homes keep
// off the river, so on a small map the river can leave too few blocks for them; then the city is
// laid out again without it (the lagoon still gives it its water), and only a map too small even
// then refuses the colonies.
//
// A generation asks for the same design three times: the request check (designFailure), generate
// and validateWorld. The design depends only on the request (every draw comes from a named stream
// seeded by the request's seed), so the last design built on this thread is kept and handed out
// again for the same request, its telemetry replayed so each context records what building it would
// have.
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
						  "The river left too few blocks for the homes; laid out without it.");
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
	writeUndermap(map, L.terrain);
	// Standing stone and rubble go down before the colonies: neither is ever on a home.
	int stoneTiles = 0;
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (L.stone[i] && map.isResourceAllowed(x, y, STONE))
		{
			map.setResource(x, y, STONE, 1);
			++stoneTiles;
		}
		else if (L.rubble[i] && map.isResourceAllowed(x, y, WOOD))
			map.setResource(x, y, WOOD, 1);
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
		// A field is planted to its corners, dry corners included: its streets hold them.
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
			// in the other. The first version alternated them by quarter, which left only thin arcs
			// of wheat; one solid patch beside the swarm reads better and is what the AIs look for.
			const ShapePoint well = p.centre;
			const auto byDistanceTo = [&](ShapePoint to)
			{
				std::vector<std::pair<int, int>> order;
				for (int i : p.tiles)
					order.push_back(
						{t.dist2(i % t.w, i / t.w, int(std::lround(to.x)), int(std::lround(to.y))), i});
				std::sort(order.begin(), order.end());
				return order;
			};
			const std::vector<std::pair<int, int>> order = byDistanceTo(well);
			const double ux = std::cos(p.heading), uy = std::sin(p.heading);
			const auto wheatSide = [&](int i)
			{
				return t.offsetX(int(std::lround(well.x)), i % t.w) * ux +
						   t.offsetY(int(std::lround(well.y)), i / t.w) * uy >=
					   0;
			};
			// The wheat nearest the swarm on its own side of the pond: on big blocks the swarm
			// stands far from the cistern, and wheat planted nearest the pond was 25 steps from it
			// (final reliability pass, 2026-09-16: 6 of 1,620 maps, all blocks of 22 with little
			// wheat).
			for (const auto &[distance, i] : byDistanceTo(p.wheatToward))
			{
				const int x = i % t.w, y = i / t.w;
				if (plantedWheat < wheat && wheatSide(i) && clearGround(map, x, y) &&
					map.isResourceAllowed(x, y, WHEAT))
				{
					map.setResource(x, y, WHEAT, 1);
					++plantedWheat;
				}
			}
			for (const auto &[distance, i] : order)
			{
				const int x = i % t.w, y = i / t.w;
				if (plantedWood < wood && !wheatSide(i) && clearGround(map, x, y) &&
					map.isResourceAllowed(x, y, WOOD))
				{
					map.setResource(x, y, WOOD, 1);
					++plantedWood;
				}
			}
			// A garden clipped by its home's edge can lose most of a half: whatever its halves
			// could not hold goes on the nearest clear tiles left.
			for (const auto &[distance, i] : order)
			{
				const int x = i % t.w, y = i / t.w;
				if (!clearGround(map, x, y))
					continue;
				if (plantedWood < wood && map.isResourceAllowed(x, y, WOOD))
				{
					map.setResource(x, y, WOOD, 1);
					++plantedWood;
				}
				else if (plantedWheat < wheat && map.isResourceAllowed(x, y, WHEAT))
				{
					map.setResource(x, y, WHEAT, 1);
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
						 { return L.orchard[i] && clearGround(map, i % t.w, i / t.w); });
		}
	seedAlgae(map, context, t, "honeycomb-isle-algae", o.algae, AlgaeBand::shallows(0, 4, 60));
	// No generic crop or route repair: an unrestricted top-up would break the gardens' seal and
	// could plant living wood in a street. The validator refuses a colony short of crops or room
	// instead.
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
	// Every block is sealed by its streets: no crop or rubble may grow out of the plot it is in.
	const Fertility::Field &fertility = L.fertility;
	if (const std::string error = containedPlotsMismatch(map, t, L.sealOf, &fertility); !error.empty())
		return error;
	if (const std::string error = homePondMissing(map, t, L.wells, teams, "home", "cistern");
		!error.empty())
		return error;
	for (int i = 0; i < t.size(); ++i)
		if (L.street[i] && L.homeOf[i] < 0 && !map.isWater(i % t.w, i / t.w) &&
			map.getResource(i % t.w, i / t.w).type != NO_RES_TYPE)
			return "A street is blocked by a deposit at " + std::to_string(i % t.w) + "," +
				   std::to_string(i / t.w) + ".";
	if (const std::string error =
			walkFromFirstColony(map, teams, "the city", "along the streets").error;
		!error.empty())
		return error;
	return startingAccessFailure(map, teams, {{WHEAT, 24, "wheat"}, {WOOD, 32, "wood"}}, 16, 24);
}
} // namespace

HoneycombIsleOptions::HoneycombIsleOptions(const GenerationRequest &r)
	: blockShape(r.option("block-shape")), blockSize(r.option("block-size")),
	  streetWidth(r.option("street-width")), warp(r.option("warp")),
	  blocksPerColony(r.option("blocks-per-colony")), rubble(r.option("rubble")),
	  outlineGaps(r.option("outline-gaps")), craters(r.option("crater-gardens")),
	  riverWidth(r.option("river-width")), wheatFields(r.option("wheat-fields")),
	  bridges(r.option("bridges")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition honeycombIsleDefinition()
{
	return {
			"honeycomb-isle",
			53,
			"Honeycomb isle",
			1,
			false,
			// Blocks of 18 with streets of 3 leave a block's inside about 15 tiles across, room for
			// a cistern garden and a swarm's neighbourhood; eleven blocks a colony leave room for
			// ruins inside the ring of edge and riverside fields.
			{GeneratorControl::choice("block-shape", "Block shape", {"Squares", "Hexagons"}, 1,
									  ControlGroup::Terrain),
			 {"block-size", "Block size", 16, 22, 1, 18, ControlGroup::Layout},
			 {"street-width", "Street width", 2, 5, 1, 3, ControlGroup::Terrain},
			 {"warp", "Warp", 0, 100, 10, 50, ControlGroup::Terrain},
			 {"blocks-per-colony", "Blocks per colony", 8, 16, 1, 11, ControlGroup::Layout},
			 {"rubble", "Rubble", 20, 90, 5, 70, ControlGroup::Terrain},
			 {"outline-gaps", "Outline gaps", 0, 100, 10, 40, ControlGroup::Terrain},
			 {"crater-gardens", "Craters per colony", 0, 3, 1, 1, ControlGroup::Layout},
			 GeneratorControl::choice("wheat-fields", "Wheat fields", {"Few", "Normal", "Many"}, 1,
									  ControlGroup::Layout),
			 // FEEDBACK 2026-09-16: the river "a bit thicker, maybe 2x" (was 6 wide, at most 12).
			 {"river-width", "River width", 0, 20, 1, 12, ControlGroup::Terrain},
			 {"bridges", "Bridges", 1, 6, 1, 3, ControlGroup::Layout},
			 // Wheat fields are planted full at 100 and rubble is full at 200, so higher amounts would
			 // change nothing (bulk study, 2026-09-16). The stone amount scales only the masonry
			 // chunks in the rubble: ruin and landmark outlines are structural and stand at 0.
			 GeneratorControl::percentage("wheat-amount", "Wheat amount", 100),
			 GeneratorControl::percentage("wood-amount", "Wood amount", 200),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			// The hex tiling itself reads as stylised/artificial on top of the urban ruins.
			{"terrain:urban", "terrain:novelty", "feature:river", "feature:hexagons", "feature:ruins",
			 "style:tight-building"}};
}
