// SPDX-License-Identifier: GPL-3.0-or-later
#include "CanalsGenerator.h"
#include "Channels.h"
#include "Climates.h"
#include "FertilityField.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "GraphMaze.h"
#include "Grid.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Rock.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Tessellation.h"
#include "Topology.h"
#include "Towers.h"
#include "Walls.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <string>
#include <vector>
using namespace MapGeneration;

// Canals: a lagoon city. The map is cut into blocks by a grid of narrow canals - squares, or hexagons
// (FEEDBACK 2026-09-14: "support a hexagon lattice ... an option similar to the maze generator") -
// warped so the blocks are irregular, and every canal is just wide enough to stop a unit and just
// narrow enough for a tower on one bank to shoot the other. Every colony starts on a block of its own
// with a kit and no pond, and a handful of bridges join the blocks: only the bridges a tree needs to
// connect the colonies' blocks, plus a few more at random, so most blocks are islands until someone
// can swim. Towers reach across the canals from the first minute and armies cannot, so where the
// first towers go is the opening; once swimming pools are built every canal is a road and the map
// turns inside out.
//
// The city has no centre: the colonies' blocks are the ones farthest apart on the block graph
// (spreadPockets), every other block is dealt its kind by a weighted draw, and fairness is
// statistical (the lobby keeps the best-scoring of several seeds).
//
// WHY IT PLAYS WELL (docs/map-generators/game-rules-for-map-design.md). Water blocks walking until
// a colony can swim, so the canals are a timing rule; a tower scans square rings with no line of
// sight, and a straight canal w corners wide puts the banks' buildable ground w + 4 tiles apart
// (Channels.h), so the default canal of 3 is reached by a level-2 tower (range 7) and not by a
// level-1 (range 5). Every block's canal waters all of it, so crops regrow everywhere; the fight is
// over bridges and banks, not fields.
//
// FEEDBACK 2026-09-13 (first play): "remove the pond from the user's starting base. increase the
// default rate of bridges across the squares 1.5x, and the default amount of warping 2x. it's also a
// little boring right now: can every grid cell have one of N types? ... each cell has its own little
// surprise." So home blocks have no pond (their canal waters them), extra bridges default to 30% and
// warp to 80, and every other block is dealt a kind (BlockKind) from a weighted draw, and a facing.
// Second play: "too many of them are empty of resources ... add some types that are more like forts?
// with some stone that guides enemies into a kill zone": every pad kind has a wheat and a wood clump
// beside its pad, and five built kinds (fort, bastion, funnel, chicane, moat) guard theirs.
//
// Revision 6 (2026-09-15): a woodlot's or wheatfield's cover goes down through plantCoverShare.
//
// REVISION 10 (CANALS 2.0, 2026-11-11). The first revisions were written when the engine had grass,
// sand and water only: every bank was a sand beach, bridges and causeways were sand, a building pad
// was grass in a ring of sand, a "dune" block was a disc of sand, and every wall was the stone
// resource. 2.0 rebuilds the city on the terrain catalogue (.agents/skills/glob2-map-design/
// references/terrain-catalogue.md) with one fixed lagoon look (no per-map climate):
//  - Quays and beaches. The banks of the homes and of the city's blocks (pads and built kinds) are
//    brick quays (Paths, one vertex wide, so they spoil the same tiles a beach did and the tower
//    arithmetic above is unchanged); the banks of the country's blocks (plain, lake, orchard,
//    woodlot, wheatfield, quarry, marsh) stay pale-sand beaches. A garden's pond is banked in loam.
//  - Bridges and the moat's causeway are boardwalks (Paths), the bridge exception to one paving per
//    map. The building pads are paved campi (Paths: buildable, nothing grows, so no sand ring), and
//    every home has a paved piazza round its swarm.
//  - Walls are terrain on vertices, thickened to read as one mass: a fort's and a bastion's are
//    parapets towers shoot over (Ridges), a funnel's and a chicane's are hedges that stop arrows too
//    (Obstacles) and so make their gap the only way and the only line of fire. A quarry block is a
//    rock outcrop with stone at its foot, and forts and bastions get faces of stone outside their
//    walls, since the walls were the stone the colonies mined.
//  - The dune is a salt marsh (barena): a rough mudflat core (walkable, slow, unbuildable) in a rim
//    of barren clay. A new garden block (orto) is fertile loam round a small pond, under wheat.
//  - THE GRAND CANAL (control grand-canal, on by default): one row of the lattice's edges, a closed
//    loop round the torus so it parts nothing, is a canal kGrandExtra corners wider than the rest
//    with a deep-water core (slower swimming). At its width (Channels.h) no tower reaches across, so
//    its few bridges - kGrandBridgeShare of its edges, at least one - are the city's fronts.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say.
constexpr int kHomeWheat = 14, kHomeWood = 12, kHomeQuarry = 2;
// Hexagonal blocks sit this share of the block size apart, as Maze's cells do: a hexagon's edge is only
// about 0.58 of its pitch, so at the square's spacing its canals would leave blocks too small for their
// kinds.
constexpr int kHexPitchPercent = 150;
// A bridge is a connected stroke kBridgeHalfWidth either side of its centre line; traversed tiles need
// at least two deck corners to favour walking over swimming. It reaches this far past the canal's edge
// onto each bank so it lands on the bank, not in the water.
constexpr double kBridgeHalfWidth = 0.75, kBridgeLanding = 2.5;
// What a block holds (first play: "each cell has its own little surprise").
enum BlockKind
{
	Home = -1,
	Plain = 0,
	Lake,       // a pond at the middle, the fields round it
	Orchard,    // a small pond with the three fruits round it
	Homestead,  // a paved 4x4 campo, a wheat and a wood clump either side of it
	Hamlet,     // two such campi side by side, the clumps before and behind them
	Quarry,     // a rock outcrop with stone at its foot
	Woodlot,    // wood over most of the block
	Wheatfield, // wheat over most of the block
	Marsh,      // a salt marsh: a mudflat core in a rim of barren clay (was the sand dune)
	Fort,       // a square parapet round a campo with one gate at the front, the kill zone
	Bastion,    // four corner parapets round a campo, an opening in the middle of every side
	Funnel,     // two hedges in a V that guide a walk onto a three-tile gap, the campo behind it
	Chicane,    // two staggered hedges with a corridor between them, the way across the block
	Moat,       // a ring of water round an islet with a campo, one boardwalk causeway onto it
	Garden      // an orto: a pond banked in fertile loam, under wheat
};
constexpr int kKinds = 15;
constexpr int kKindWeight[kKinds] = {5, 7, 8, 9, 6, 6, 7, 7, 5, 8, 7, 7, 6, 7, 5};
static_assert(
	[]
	{
		int sum = 0;
		for (int w : kKindWeight)
			sum += w;
		return sum == 100;
	}(),
	"Block kind weights are out of 100");
const char *const kKindNames[kKinds + 1] = {"home",    "plain",    "lake",     "orchard",
											"homestead", "hamlet", "quarry",   "woodlot",
											"wheatfield", "marsh", "fort",     "bastion",
											"funnel",  "chicane",  "moat",     "garden"};
// The country's blocks keep sand beaches; the homes and the city's blocks are quayed.
bool quayed(int kind)
{
	switch (kind)
	{
	case Home:
	case Homestead:
	case Hamlet:
	case Fort:
	case Bastion:
	case Funnel:
	case Chicane:
	case Moat:
		return true;
	default:
		return false;
	}
}
// The kinds' features: the lake's and the orchard pond's radii, the marsh and its mudflat core, the
// garden's pond and loam beds, the quarry's outcrop, the pad (4 tiles square, paved), how far apart a
// hamlet's two pads stand, the share of a woodlot, wheatfield or garden under its crop, and the
// piazza paved round every home's swarm: wide enough that the ring of tiles round the swarm's
// footprint, where its workers stand, is pure paving (a paving-grass seam is never buildable, and
// at 3.5 it cost the swarm its central site and half its worker room).
constexpr double kLakeRadius = 3.5, kOrchardPond = 2.5, kMarshRadius = 4.5, kMarshCore = 3.2;
constexpr double kGardenPond = 2.0, kGardenBeds = 6.5, kQuarryRock = 2.5, kPiazzaRadius = 4.6;
constexpr int kPadSize = 4, kPadsApart = 10, kCoverPercent = 60;
// Stone: a quarry block's faces round its outcrop, and the faces outside a fort's or bastion's
// parapet (its walls used to be its stone), before the stone amount.
constexpr int kQuarryFaceTiles = 30, kWallFaceTiles = 16;
// The built kinds, in tiles from the block's middle in the block's own frame (every block faces one of
// four ways, dealt with its kind; localTile). A wall's reach is the half block less kWallMargin (a
// bank, a bridge's landing and a lane round the wall), at most kWallReach. A fort's gate and the
// funnel's gap are kGateWidth tiles; a bastion's corner arms are two short of the half side; the
// funnel's walls run diagonally in from the back corners, two tiles thick; the chicane's walls stand
// kChicaneOffset either side of the middle, each reaching kChicaneOverlap past it from its own side.
// The walls are drawn on vertices (wallVertices) and thickened to three (thickenRidges), never onto a
// bank, a pad, a gate or a lane; a block whose walls would cut its land in two gets no walls and the
// pad kind instead. A moat is kMoatWidth corners of water round an islet of kMoatOuter - kMoatWidth
// in radius; it needs a block of kMoatMinimumBlock with every corner within kMoatOuter + 1.5 of the
// middle on the block, else it is dealt as a lake. Beside every pad kind stand a wheat clump of
// kClumpRadius (a homestead's; the others one smaller) and a wood clump one smaller.
constexpr int kWallReach = 6, kWallMargin = 4, kGateWidth = 3, kChicaneOffset = 3,
			  kChicaneOverlap = 2;
constexpr double kMoatOuter = 7.5, kMoatWidth = 3.0;
constexpr int kMoatMinimumBlock = 22, kClumpRadius = 3, kClumpOut = 6;
// The grand canal: kGrandExtra corners wider than the canals, at least as wide as the narrowest
// channel no level-3 tower crosses (Channels.h), its water kGrandDeep steps from the banks deep;
// kGrandBridgeShare in 100 of its edges carry a bridge, at least one.
constexpr int kGrandExtra = 4, kGrandDeep = 2, kGrandBridgeShare = 25;
// Tower pads beside each colony's starting towers, and the spacing between a colony's sites.
constexpr int kTowerPads = 2, kTowerSpacing = 6;

// The lagoon city's one look (FEEDBACK 2026-11-11: a fixed look rather than a climate): brick quays,
// streets and campi, boardwalk bridges, tended lawns at home, pale-sand beaches, ridge-rock parapets,
// garden hedges, clay and mudflat in the marshes, loam in the gardens, a deep grand canal.
constexpr ClimateLook kLagoonLook = {"lagoon",    HEDGE,      RIDGE_ROCK, LUSH_GRASS, GRASS,
									 PALE_SAND,   BRICK_PATH, BRICK_PATH, BRICK_PATH, BRICK_PATH,
									 BOARDWALK,   CLAY,       DEEP_WATER, MUDFLAT,    LOAM,
									 RIDGE_ROCK,  LUSH_GRASS};
static_assert(terrainGroup(kLagoonLook.cliff) == TerrainGroup::Obstacles &&
				  terrainGroup(kLagoonLook.rock) == TerrainGroup::Ridges &&
				  terrainGroup(kLagoonLook.rockAccent) == TerrainGroup::Ridges &&
				  terrainGroup(kLagoonLook.homeGrass) == TerrainGroup::Grass &&
				  terrainGroup(kLagoonLook.openGrass) == TerrainGroup::Grass &&
				  terrainGroup(kLagoonLook.farmGrass) == TerrainGroup::Grass &&
				  terrainGroup(kLagoonLook.beach) == TerrainGroup::Sand &&
				  terrainGroup(kLagoonLook.town) == TerrainGroup::Paths &&
				  terrainGroup(kLagoonLook.street) == TerrainGroup::Paths &&
				  terrainGroup(kLagoonLook.road) == TerrainGroup::Paths &&
				  terrainGroup(kLagoonLook.deck) == TerrainGroup::Paths &&
				  terrainGroup(kLagoonLook.bridge) == TerrainGroup::Paths &&
				  terrainGroup(kLagoonLook.scrub) == TerrainGroup::Barren &&
				  terrainGroup(kLagoonLook.deep) == TerrainGroup::DeepWater &&
				  terrainGroup(kLagoonLook.rough) == TerrainGroup::Rough &&
				  terrainGroup(kLagoonLook.fertile) == TerrainGroup::Fertile,
			  "The lagoon look may change only looks, never a terrain's rules");

struct Layout
{
	Torus t{1, 1};
	Tessellation g;
	std::vector<int> cell; // every tile's block
	std::vector<int> homeCell;
	std::vector<int> kind;   // every block's BlockKind
	std::vector<int> facing; // every block's quarter turn (0-3) for its built kind
	std::vector<unsigned char> grandEdge; // per edge: part of the grand canal
	std::vector<int> homeOf;              // the home block a tile is in, or -1
	std::vector<unsigned char> canal, grand, bridge, water, land;
	std::vector<unsigned char> deck;  // vertices of the bridges and causeways (boardwalk)
	std::vector<unsigned char> deep;  // vertices of the grand canal's deep core
	std::vector<unsigned char> pad;   // the paved building pads, as tiles
	std::vector<unsigned char> wall;  // the walls' vertices, Ridges and Obstacles
	std::vector<unsigned char> rock;  // the quarry outcrops' vertices
	std::vector<unsigned char> lane;  // tiles walls leave open: gates, gaps, corridors, pads
	std::vector<unsigned char> bank;  // tiles with a corner in the water or on its bank
	TerrainSketch sketch;             // the mechanics: one terrain per rule
	TerrainSketch finished;           // the lagoon look painted over them
	std::vector<ShapePoint> homes, kits;
	double homeRadius = 0;
	int grandWidth = 0, grandBridges = 0;
	std::string failure;
};

// A tile at (u, v) in a block's frame: from its middle, turned a quarter turn per facing.
int localTile(const Layout &L, int cell, int u, int v)
{
	const ShapePoint c = tilePoint(L.g.cells[cell].centre);
	const int cx = int(std::lround(c.x)), cy = int(std::lround(c.y));
	int dx = u, dy = v;
	switch (L.facing[cell])
	{
	case 1:
		dx = -v;
		dy = u;
		break;
	case 2:
		dx = -u;
		dy = -v;
		break;
	case 3:
		dx = v;
		dy = -u;
		break;
	default:
		break;
	}
	return L.t.at(cx + dx, cy + dy);
}

// How far a built kind's walls reach from the block's middle.
int wallReach(const CanalsOptions &o)
{
	return std::min(kWallReach, o.blockSize / 2 - kWallMargin);
}

// The grand canal's width in corners: kGrandExtra wider than the canals, and never narrower than the
// narrowest channel a level-3 tower misses, so no tower covers it.
int grandWidth(const CanalsOptions &o)
{
	return std::max(o.canalWidth + kGrandExtra, narrowestChannelTowersMiss(3));
}

// THE WALLS of a built kind, as the tiles of the block they may stand on (on the block, clear of water
// by two corners all round, and on pure grass), with the tiles they must leave open: the gate, the
// gap, the corridor, the pad and a tile round it.
struct BlockWall
{
	std::vector<int> tiles, lane;
};
BlockWall blockWallTiles(const Layout &L, int cell, int reach)
{
	const Torus &t = L.t;
	const auto wallable = [&](int i)
	{
		if (L.cell[i] != cell || L.canal[i] || L.pad[i])
			return false;
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 2; ++dy)
			for (int dx = -1; dx <= 2; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				if (L.sketch[j] == WATER)
					return false;
				if (dx >= 0 && dx <= 1 && dy >= 0 && dy <= 1 && L.sketch[j] != GRASS)
					return false;
			}
		return true;
	};
	std::set<int> tiles;
	BlockWall w;
	const auto wall = [&](int u, int v)
	{
		const int i = localTile(L, cell, u, v);
		if (wallable(i))
			tiles.insert(i);
	};
	const auto lane = [&](int u0, int v0, int u1, int v1)
	{
		for (int v = v0; v <= v1; ++v)
			for (int u = u0; u <= u1; ++u)
				w.lane.push_back(localTile(L, cell, u, v));
	};
	const int half = kGateWidth / 2, padHalf = kPadSize / 2 + 1;
	switch (L.kind[cell])
	{
	case Fort:
		for (int k = -reach; k <= reach; ++k)
		{
			wall(k, -reach);
			wall(-reach, k);
			wall(reach, k);
			if (std::abs(k) > half)
				wall(k, reach);
		}
		lane(-half, padHalf, half, reach + 2);
		lane(-padHalf, -padHalf, padHalf, padHalf);
		break;
	case Bastion:
		for (int k = 0; k < reach - 2; ++k)
			for (const int su : {-1, 1})
				for (const int sv : {-1, 1})
				{
					wall(su * reach, sv * (reach - k));
					wall(su * (reach - k), sv * reach);
				}
		lane(-half, -reach - 2, half, reach + 2);
		lane(-reach - 2, -half, reach + 2, half);
		lane(-padHalf, -padHalf, padHalf, padHalf);
		break;
	case Funnel:
		for (int k = 0; k <= reach - 3; ++k)
		{
			wall(-reach + k, -reach + k);
			wall(-reach + k + 1, -reach + k);
			wall(reach - k, -reach + k);
			wall(reach - k - 1, -reach + k);
		}
		lane(-half, -reach - 2, half, kPadSize / 2);
		lane(-padHalf, kPadSize / 2 - padHalf, padHalf, kPadSize / 2 + padHalf);
		break;
	case Chicane:
		for (int u = -reach; u <= kChicaneOverlap; ++u)
			wall(u, -kChicaneOffset);
		for (int u = -kChicaneOverlap; u <= reach; ++u)
			wall(u, kChicaneOffset);
		lane(-reach - 2, -kChicaneOffset + 1, reach + 2, kChicaneOffset - 1);
		lane(kChicaneOverlap + 1, -kChicaneOffset - 2, reach + 2, -kChicaneOffset + 1);
		lane(-reach - 2, kChicaneOffset - 1, -kChicaneOverlap - 1, kChicaneOffset + 2);
		break;
	default:
		break;
	}
	w.tiles.assign(tiles.begin(), tiles.end());
	return w;
}

// The pieces a block's walkable land falls in, eight ways as a unit walks.
int landPieces(const Layout &L, const std::vector<unsigned char> &walk, int cell)
{
	const Torus &t = L.t;
	std::vector<int> start;
	for (int i = 0; i < t.size(); ++i)
		if (L.cell[i] == cell && walk[i])
			start.push_back(i);
	std::set<int> seen;
	int pieces = 0;
	for (int s : start)
	{
		if (seen.count(s))
			continue;
		++pieces;
		std::vector<int> queue{s};
		seen.insert(s);
		for (size_t head = 0; head < queue.size(); ++head)
		{
			const int x = t.remainderX(queue[head]), y = queue[head] / t.w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(x + dx, y + dy);
					if (L.cell[j] == cell && walk[j] && seen.insert(j).second)
						queue.push_back(j);
				}
		}
	}
	return pieces;
}

// The block-graph tree joining every colony's block to the first, by the cheapest crossings: a
// crossing of the grand canal costs kGrandCost of the others, so the tree crosses it only where the
// way round is longer.
constexpr int kGrandCost = 4;
void bridgeTree(const Layout &L, int teams, std::vector<unsigned char> &open)
{
	if (teams < 2)
		return;
	const int cells = L.g.cellCount();
	std::vector<int> cost(cells, -1), parentEdge(cells, -1);
	using Entry = std::pair<int, int>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> frontier;
	cost[L.homeCell[0]] = 0;
	frontier.push({0, L.homeCell[0]});
	while (!frontier.empty())
	{
		const auto [c, cell] = frontier.top();
		frontier.pop();
		if (c != cost[cell])
			continue;
		for (int edge : L.g.cells[cell].edges)
		{
			const int next = L.g.other(edge, cell);
			const int step = c + (L.grandEdge[edge] ? kGrandCost : 1);
			if (cost[next] < 0 || step < cost[next])
			{
				cost[next] = step;
				parentEdge[next] = edge;
				frontier.push({step, next});
			}
		}
	}
	for (int k = 1; k < teams; ++k)
		for (int cell = L.homeCell[k]; parentEdge[cell] >= 0;)
		{
			open[parentEdge[cell]] = 1;
			cell = L.g.other(parentEdge[cell], cell);
		}
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const CanalsOptions o(request);
	context.telemetry.choice("canals.block-shape", o.blockShape == 0 ? "Squares" : "Hexagons");
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// The blocks: a square or hexagonal tiling, warped. Every edge is a canal, so every edge is an
	// obstacle the warp keeps apart: opposite sides of a block stay at least the widest canal plus
	// eight tiles apart, so a block always keeps a few tiles of ground across its narrowest, and a
	// block's centre keeps at least a fifth of the block from its sides (and the grand canal's extra
	// half-width more), so a pond fits.
	L.g = o.blockShape == int(Tessellation::Shape::Hexagon)
			  ? hexTessellation(t.w, t.h, o.blockSize * kHexPitchPercent / 100)
			  : squareTessellation(t.w, t.h, o.blockSize);
	if (L.g.columns < 2 || L.g.rows < 2)
	{
		L.failure = "The canals need at least two blocks across and down; use a bigger map or "
					"smaller blocks.";
		return L;
	}
	// The grand canal: the edges between one row of blocks and the next, a loop round the torus.
	// With fewer than three rows those edges are both of the rows' borders and would part the map.
	L.grandEdge.assign(L.g.edges.size(), 0);
	bool grand = o.grandCanal != 0;
	if (grand && L.g.rows < 3)
	{
		context.telemetry.fallback("canals.grand-canal.omitted",
								   "Fewer than three rows of blocks; no grand canal.", L.g.rows);
		grand = false;
	}
	L.grandWidth = grand ? grandWidth(o) : 0;
	if (grand)
	{
		const int row = int(context.bounded("canals-grand", std::uint32_t(L.g.rows)));
		const int next = (row + 1) % L.g.rows;
		for (int edge = 0; edge < int(L.g.edges.size()); ++edge)
		{
			const int a = L.g.cells[L.g.edges[edge].cells[0]].row,
					  b = L.g.cells[L.g.edges[edge].cells[1]].row;
			L.grandEdge[edge] = (a == row && b == next) || (a == next && b == row);
		}
		context.telemetry.measure("canals.grand-canal.row", row);
		context.telemetry.measure("canals.grand-canal.width", L.grandWidth);
	}
	const std::vector<unsigned char> walls(L.g.edges.size(), 1);
	warpCorners(L.g, warpLimit(L.g) * o.warp / 100, walls,
				std::max(o.canalWidth, L.grandWidth) + 8,
				o.blockSize / 5 + (grand ? (L.grandWidth - o.canalWidth + 1) / 2 : 0), context,
				"canals-warp");
	L.cell = labelTiles(L.g);
	if (L.cell.empty())
	{
		L.failure = "The blocks could not be labelled.";
		return L;
	}

	// The home blocks: as far apart on the block graph as the tiling allows.
	const CellGraph graph = cellGraph(L.g);
	L.homeCell = spreadPockets(graph, teams);
	if (int(L.homeCell.size()) != teams)
	{
		L.failure = "Too many colonies for this many blocks; use a bigger map, smaller blocks or "
					"fewer colonies.";
		return L;
	}
	// spreadPockets always starts from block 0, so the deal decides which colony gets which block.
	dealStarts(context, L.homeCell);
	// Every other block's kind, dealt from a weighted draw in block order.
	L.kind.assign(L.g.cellCount(), Plain);
	L.facing.assign(L.g.cellCount(), 0);
	for (int cell : L.homeCell)
		L.kind[cell] = Home;
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		if (L.kind[cell] == Home)
			continue;
		int roll = int(context.bounded("canals-kinds", 100)), kind = 0;
		while (kind + 1 < kKinds && roll >= kKindWeight[kind])
			roll -= kKindWeight[kind++];
		// A moat needs a block big enough for its ring; a smaller block's moat is a lake.
		if (kind == Moat && o.blockSize < kMoatMinimumBlock)
		{
			context.telemetry.fallback("canals.block.moat-to-lake",
									   "Block size is below the moat minimum.", cell);
			kind = Lake;
		}
		L.kind[cell] = kind;
		L.facing[cell] = int(context.bounded("canals-facing", 4));
	}

	// The canals: every edge stroked canalWidth corners wide (the grand canal's grandWidth). A canal's
	// water is its width less one tiles across, and a diagonal canal is only sealed against a diagonal
	// step when its water is two tiles thick, which is why the narrowest canal offered is 3.
	L.canal.assign(n, 0);
	L.grand.assign(n, 0);
	for (int edge = 0; edge < int(L.g.edges.size()); ++edge)
	{
		const auto ends = L.g.edgeEnds(edge, L.g.edges[edge].cells[0]);
		const ShapePoint a = tilePoint(ends.first), b = tilePoint(ends.second);
		const double r = (L.grandEdge[edge] ? L.grandWidth : o.canalWidth) / 2.0;
		strokePath(L.canal, t, {{a.x, a.y, r}, {b.x, b.y, r}});
		if (L.grandEdge[edge])
			strokePath(L.grand, t, {{a.x, a.y, r}, {b.x, b.y, r}});
	}

	// The bridges: the cheapest tree joining every colony's block to the first (bridgeTree), then
	// `extraBridges` percent of the blocks' count in further bridges at random (openLoops), which is
	// where the flanking routes come from. Of the grand canal's edges only kGrandBridgeShare carry a
	// bridge, the tree's first: the grand canal's few bridges are the city's fronts.
	std::vector<unsigned char> open(L.g.edges.size(), 0);
	bridgeTree(L, teams, open);
	std::vector<unsigned char> tree = open;
	openLoops(graph, context, "canals-bridges", std::vector<unsigned char>(L.g.cellCount(), 0),
			  o.extraBridges, open);
	if (grand)
	{
		std::vector<int> closed;
		int grandEdges = 0, bridged = 0;
		for (int edge = 0; edge < int(L.g.edges.size()); ++edge)
			if (L.grandEdge[edge])
			{
				++grandEdges;
				if (!tree[edge])
				{
					open[edge] = 0;
					closed.push_back(edge);
				}
				else
					++bridged;
			}
		const int wanted = std::max(1, grandEdges * kGrandBridgeShare / 100);
		context.shuffle(closed.begin(), closed.end(), "canals-grand-bridges");
		for (size_t k = 0; k < closed.size() && bridged < wanted; ++k, ++bridged)
			open[closed[k]] = 1;
		L.grandBridges = bridged;
		context.telemetry.measure("canals.grand-canal.bridges", bridged);
	}
	L.bridge.assign(n, 0);
	for (int edge = 0; edge < int(L.g.edges.size()); ++edge)
	{
		if (!open[edge])
			continue;
		const int owner = L.g.edges[edge].cells[0];
		const auto ends = L.g.edgeEnds(edge, owner);
		const ShapePoint a = tilePoint(ends.first), b = tilePoint(ends.second);
		const ShapePoint middle{(a.x + b.x) / 2, (a.y + b.y) / 2};
		// Across the canal: from the owner's centre to the neighbour's, through the edge's middle.
		const ShapePoint from = tilePoint(L.g.cells[owner].centre),
						 to = tilePoint(L.g.centreAcross(edge, owner));
		const double dx = to.x - from.x, dy = to.y - from.y, length = std::hypot(dx, dy);
		// A warped edge meets the line between the centres at a slant, so the water is wider along
		// it: the reach grows by the slant (at most doubled) so the deck still lands on both banks.
		const double ex = b.x - a.x, ey = b.y - a.y, edgeLength = std::hypot(ex, ey);
		const double square =
			edgeLength > 0 ? std::abs(ex * dy - ey * dx) / (edgeLength * length) : 1.0;
		const double reach =
			((L.grandEdge[edge] ? L.grandWidth : o.canalWidth) / 2.0 + kBridgeLanding) /
			std::max(0.5, square);
		strokePath(
			L.bridge, t,
			{{middle.x - dx / length * reach, middle.y - dy / length * reach, kBridgeHalfWidth},
			 {middle.x + dx / length * reach, middle.y + dy / length * reach, kBridgeHalfWidth}});
	}

	// Homes: the kit round the middle of every home block, with no pond (first play; the block's canal
	// is within the growth probe's reach of all of it). The block's "radius" for the kit is what is
	// left between its centre and its canal.
	L.homeRadius = std::max(6.0, o.blockSize / 2.0 - o.canalWidth / 2.0 - 3);
	L.water = L.canal;
	L.homeOf.assign(n, -1);
	L.land.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.land[i] = !L.canal[i];
	for (int k = 0; k < teams; ++k)
	{
		L.homes.push_back(tilePoint(L.g.cells[L.homeCell[k]].centre));
		L.kits.push_back(L.homes[k]);
		for (int i = 0; i < n; ++i)
			if (L.cell[i] == L.homeCell[k] && L.land[i])
				L.homeOf[i] = k;
	}
	// Every block's middle feature: a lake, an orchard's or a garden's pond, a marsh, a quarry's
	// outcrop, or a moat (a ring of water with one boardwalk causeway across it, at the block's front;
	// a moat whose ring would not lie wholly on its block is a lake).
	const RadialShape lake(kLakeRadius, 0.3, context, "canals-lakes");
	const RadialShape orchardPond(kOrchardPond, 0.3, context, "canals-lakes");
	const RadialShape marsh(kMarshRadius, 0.3, context, "canals-marshes");
	const RadialShape marshCore(kMarshCore, 0.3, context, "canals-marshes");
	const RadialShape gardenPond(kGardenPond, 0.3, context, "canals-gardens");
	const RadialShape gardenBeds(kGardenBeds, 0.25, context, "canals-gardens");
	const RadialShape outcrop(kQuarryRock, 0.3, context, "canals-quarries");
	const RadialShape moatRing(kMoatOuter, 0.0, context, "canals-moats");
	const RadialShape islet(kMoatOuter - kMoatWidth, 0.0, context, "canals-moats");
	std::vector<unsigned char> mud(n, 0), clay(n, 0), loam(n, 0);
	L.deck.assign(n, 0);
	L.rock.assign(n, 0);
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		const ShapePoint c = tilePoint(L.g.cells[cell].centre);
		const int cx = int(std::lround(c.x)), cy = int(std::lround(c.y));
		if (L.kind[cell] == Moat)
		{
			bool fits = true;
			const int span = int(std::ceil(kMoatOuter + 1.5));
			for (int dy = -span; dy <= span && fits; ++dy)
				for (int dx = -span; dx <= span && fits; ++dx)
					if (std::hypot(dx, dy) < kMoatOuter + 1.5)
					{
						const int i = t.at(cx + dx, cy + dy);
						fits = L.cell[i] == cell && !L.canal[i];
					}
			if (!fits)
			{
				context.telemetry.fallback("canals.block.moat-to-lake",
										   "Moat clearance does not fit the warped block.", cell);
				L.kind[cell] = Lake;
			}
		}
		switch (L.kind[cell])
		{
		case Lake:
			fillShape(L.water, t, c.x, c.y, lake, 0.0);
			break;
		case Orchard:
			fillShape(L.water, t, c.x, c.y, orchardPond, 0.0);
			break;
		case Marsh:
			fillShape(clay, t, c.x, c.y, marsh, 0.0);
			fillShape(mud, t, c.x, c.y, marshCore, 0.0);
			break;
		case Garden:
			fillShape(loam, t, c.x, c.y, gardenBeds, 0.0);
			fillShape(L.water, t, c.x, c.y, gardenPond, 0.0);
			break;
		case Quarry:
			fillShape(L.rock, t, c.x, c.y, outcrop, 0.0);
			break;
		case Moat:
			// Round the rounded middle, as the pad will be, so the islet holds it exactly.
			fillShape(L.water, t, cx, cy, moatRing, 0.0);
			fillShape(L.water, t, cx, cy, islet, 0.0, 0);
			for (int v = int(kMoatOuter - kMoatWidth) - 1; v <= int(kMoatOuter) + 1; ++v)
			{
				const int i = localTile(L, cell, 0, v);
				L.water[i] = 0;
				L.deck[i] = 1;
			}
			break;
		default:
			break;
		}
	}

	// THE MECHANICS. Water; the bridges' decks over it; then every land vertex beside the water that
	// is left is its bank: a deck where a bridge lands, a quay on a quayed block, loam round a garden's
	// pond, and a beach elsewhere. One vertex wide, so a quay spoils exactly the tiles a beach did.
	L.sketch.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
	{
		if (L.bridge[i] && L.canal[i])
			L.deck[i] = 1;
		L.sketch[i] = L.deck[i] ? TRAIL : L.water[i] ? WATER : GRASS;
	}
	for (int i = 0; i < n; ++i)
	{
		const int kind = L.kind[L.cell[i]];
		if (L.sketch[i] == GRASS && kind == Marsh)
			L.sketch[i] = mud[i] ? MUD : clay[i] ? DIRT : GRASS;
		if (L.sketch[i] == GRASS && kind == Garden && loam[i])
			L.sketch[i] = LOAM;
		if (L.sketch[i] == GRASS && kind == Quarry && L.rock[i])
			L.sketch[i] = RIDGE_ROCK;
	}
	std::vector<unsigned char> shoreVertex(n, 0);
	{
		const TerrainSketch before = L.sketch;
		int quays = 0;
		for (int i = 0; i < n; ++i)
		{
			if (before[i] == WATER || L.deck[i])
				continue;
			const int x = t.remainderX(i), y = i / t.w;
			bool shore = false, onCanal = false;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(x + dx, y + dy);
					shore = shore || before[j] == WATER;
					onCanal = onCanal || (before[j] == WATER && L.canal[j]);
				}
			if (!shore)
				continue;
			shoreVertex[i] = 1;
			const int kind = L.kind[L.cell[i]];
			if (L.bridge[i])
			{
				L.deck[i] = 1;
				L.sketch[i] = TRAIL;
			}
			else if (quayed(kind))
			{
				L.sketch[i] = TRAIL;
				++quays;
			}
			else
				L.sketch[i] = kind == Garden && !onCanal ? LOAM : SAND;
		}
		context.telemetry.measure("canals.quays.vertices", quays);
		// The banks towers stand against: every tile with a corner in the water or on its bank, as
		// a beach's tiles were (a quay's inland tile is no shoreline, so the map can't say).
		L.bank.assign(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const int x = t.remainderX(i), y = i / t.w;
			for (int c = 0; c < 4; ++c)
			{
				const int v = t.at(x + c % 2, y + c / 2);
				if (shoreVertex[v] || before[v] == WATER || L.deck[v])
					L.bank[i] = 1;
			}
		}
	}

	// Paving stays a vertex off the shore: a tile with quay and pad or piazza corners would be pure
	// Paths, buildable at the water's edge, and bring a tower a tile nearer the far bank than the
	// beach arithmetic allows.
	const std::vector<unsigned char> nearShore = dilate(t, shoreVertex, 1);
	// The pads, paved: at the middle of a homestead, fort, bastion or moat, two side by side on a
	// hamlet, and just behind the gap of a funnel. And every home's piazza round its swarm.
	L.pad.assign(n, 0);
	const auto stampPad = [&](int cell, int u, int v)
	{
		const int i = localTile(L, cell, u, v);
		const int x0 = t.remainderX(i) - kPadSize / 2, y0 = i / t.w - kPadSize / 2;
		for (int dy = 0; dy < kPadSize; ++dy)
			for (int dx = 0; dx < kPadSize; ++dx)
				L.pad[t.at(x0 + dx, y0 + dy)] = 1;
	};
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
		switch (L.kind[cell])
		{
		case Homestead:
		case Fort:
		case Bastion:
		case Moat:
			stampPad(cell, 0, 0);
			break;
		case Hamlet:
			stampPad(cell, -kPadsApart / 2, 0);
			stampPad(cell, kPadsApart / 2, 0);
			break;
		case Funnel:
			stampPad(cell, 0, kPadSize / 2);
			break;
		default:
			break;
		}
	{
		const std::vector<unsigned char> corners = tileCorners(t, L.pad);
		for (int i = 0; i < n; ++i)
			if (corners[i] && L.sketch[i] == GRASS && !nearShore[i])
				L.sketch[i] = TRAIL;
	}
	// The piazza is round the swarm's footprint, which settleRoundColonies puts where homeSwarmSite
	// does (its top-left tile; the footprint's middle is the vertex two tiles in).
	for (int k = 0; k < teams; ++k)
	{
		const MapGeneratorPoint swarm = homeSwarmSite(L.homes[k], 0.0, L.homeRadius);
		paveCore(L.sketch, t, swarm.x + 2, swarm.y + 2, kPiazzaRadius,
				 [&](int i) { return L.homeOf[i] == k && !nearShore[i]; });
	}

	// THE WALLS, once the banks and pads are in the sketch so none stands on them: each built kind's
	// tiles to vertices, leaning away from its lanes; then thickened to three vertices so every wall
	// reads as one mass, never onto a bank, a pad, a lane or another block. A block whose walls cut
	// its land in two loses them (blockWalls' old rule) and becomes the pad kind.
	const int reach = wallReach(o);
	std::vector<unsigned char> ridgeTiles(n, 0), hedgeTiles(n, 0), lanes(n, 0);
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		const BlockWall w = blockWallTiles(L, cell, reach);
		for (int i : w.lane)
			lanes[i] = 1;
		for (int i : w.tiles)
			(L.kind[cell] == Fort || L.kind[cell] == Bastion ? ridgeTiles : hedgeTiles)[i] = 1;
	}
	for (int i = 0; i < n; ++i)
		lanes[i] = lanes[i] || L.pad[i];
	L.lane = lanes;
	const std::vector<unsigned char> walkBefore = walkableTiles(L.sketch, t);
	std::vector<unsigned char> openGround(n, 0);
	for (int i = 0; i < n; ++i)
		openGround[i] = lanes[i] && !ridgeTiles[i] && !hedgeTiles[i];
	const std::vector<unsigned char> laneCorners = tileCorners(t, lanes);
	const std::vector<unsigned char> ridgeVertices = wallVertices(t, ridgeTiles, &openGround, &lanes);
	const std::vector<unsigned char> hedgeVertices = wallVertices(t, hedgeTiles, &openGround, &lanes);
	for (int i = 0; i < n; ++i)
		if (L.sketch[i] == GRASS && !laneCorners[i])
		{
			if (ridgeVertices[i])
				L.sketch[i] = RIDGE_ROCK;
			else if (hedgeVertices[i])
				L.sketch[i] = CLIFFS;
		}
	// Grow only on grass two vertices clear of anything but grass and rock (banks, decks, pads,
	// marsh, loam), off lanes, on a block with walls of the same kind.
	const auto canGrow = [&](TerrainType rock)
	{
		return [&L, &t, &laneCorners, rock](int i)
		{
			if (L.sketch[i] != GRASS || laneCorners[i])
				return false;
			const int kind = L.kind[L.cell[i]];
			if (rock == RIDGE_ROCK ? kind != Fort && kind != Bastion && kind != Quarry
								   : kind != Funnel && kind != Chicane)
				return false;
			const int x = t.remainderX(i), y = i / t.w;
			for (int dy = -2; dy <= 2; ++dy)
				for (int dx = -2; dx <= 2; ++dx)
				{
					const int s = L.sketch[t.at(x + dx, y + dy)];
					if (s != GRASS && s != RIDGE_ROCK && s != CLIFFS)
						return false;
				}
			return true;
		};
	};
	int unresolved = 0;
	thickenRidges(L.sketch, t, canGrow(RIDGE_ROCK), &unresolved, RIDGE_ROCK);
	thickenRidges(L.sketch, t, canGrow(CLIFFS), &unresolved, CLIFFS);
	context.telemetry.measure("canals.walls.unthickened-vertices", unresolved);
	{
		const std::vector<unsigned char> walkAfter = walkableTiles(L.sketch, t);
		std::vector<unsigned char> walled(L.g.cellCount(), 0);
		for (int i = 0; i < n; ++i)
			if (L.sketch[i] == CLIFFS || (L.sketch[i] == RIDGE_ROCK && L.kind[L.cell[i]] != Quarry))
				walled[L.cell[i]] = 1;
		for (int cell = 0; cell < L.g.cellCount(); ++cell)
		{
			if (!walled[cell] ||
				landPieces(L, walkAfter, cell) <= landPieces(L, walkBefore, cell))
				continue;
			context.telemetry.fallback(
				"canals.block.walls-omitted",
				"Walls would disconnect the block; using the simpler block kind.", cell);
			for (int i = 0; i < n; ++i)
				if (L.cell[i] == cell && (L.sketch[i] == CLIFFS || L.sketch[i] == RIDGE_ROCK))
					L.sketch[i] = GRASS;
			L.kind[cell] = L.kind[cell] == Chicane ? Plain : Homestead;
		}
	}
	L.wall.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		L.rock[i] = L.sketch[i] == RIDGE_ROCK && L.kind[L.cell[i]] == Quarry;
		L.wall[i] = (L.sketch[i] == RIDGE_ROCK || L.sketch[i] == CLIFFS) && !L.rock[i];
	}

	// THE LOOK, painted last over the mechanics: tended lawns at home, boardwalk decks, brick for
	// every other paving, and the grand canal's deep core.
	std::vector<unsigned char> grandWater(n, 0);
	for (int i = 0; i < n; ++i)
		grandWater[i] = L.grand[i] && L.sketch[i] == WATER;
	L.deep = grand ? deepCore(t, grandWater, kGrandDeep) : std::vector<unsigned char>(n, 0);
	for (int i = 0; i < n; ++i)
		L.deep[i] = L.deep[i] && L.grand[i] && L.sketch[i] == WATER;
	L.finished = L.sketch;
	LookRoles roles;
	roles.tended = [&L](int i) { return L.homeOf[i] >= 0; };
	roles.path = [&L](int i) { return L.deck[i] ? PathLook::Bridge : PathLook::Street; };
	roles.deep = [&L](int i) { return L.deep[i] != 0; };
	paintClimate(L.finished, t, kLagoonLook, roles);

	context.telemetry.measure("canals.blocks.actual", L.g.cellCount());
	if (context.telemetry.enabled())
		context.telemetry.measure("canals.bridges.open-edges",
								  std::count(open.begin(), open.end(), 1));
	context.telemetry.measure("canals.home.radius", L.homeRadius);
	if (context.telemetry.enabled())
	{
		int counts[kKinds + 1] = {};
		for (int kind : L.kind)
			++counts[kind + 1];
		for (int kind = Home; kind < kKinds; ++kind)
			context.telemetry.measure(std::string("canals.block-kind.") + kKindNames[kind + 1] +
										  ".count",
									  counts[kind + 1]);
	}
	return L;
}

// The towers every colony starts with: on its own block's bank (against the shore), covering the most
// of other blocks' land across the canal, none within range of another colony's swarm; open pads
// beside them for more.
TowerPlan planTowers(const GenerationMap &map, const Layout &L, const GenerationContext &context,
					 const CanalsOptions &o, const std::vector<unsigned char> &bank)
{
	const Torus &t = L.t;
	const int n = t.size(), teams = int(L.homes.size());
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	std::vector<int> owner(n, -1);
	std::vector<unsigned char> buildable(n, 0), target(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if (!map.terrainPropertiesAt(x, y).walkable)
			continue;
		// Land on other blocks belongs to "everyone else": worth shooting at from any colony's bank.
		owner[i] = L.homeOf[i] >= 0 ? L.homeOf[i] : teams;
		target[i] = 1;
		buildable[i] =
			L.homeOf[i] >= 0 && map.terrainPropertiesAt(x, y).buildable && !reserved[i] && !map.isResource(x, y);
	}
	TowerRequest request = startingTowerRequest(o.towers, o.towerCount, kTowerPads, kTowerSpacing);
	request.otherWeight = 1;
	request.ownWeight = 0;
	request.against = &bank;
	return chooseTowerSites(t, owner, buildable, target, swarmSurroundings(t, context, 0), teams,
							request);
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	context.stage = "canals layout";
	const CanalsOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	GenerationMap &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "canals terrain";
	writeVertices(map, L.finished);

	context.stage = "canals colonies";
	if (!settleRoundColonies(game, context, "canals-starts", L.homeOf, L.homes, L.homeRadius))
		return false;

	// Towers on the banks. No tower or pad may close a colony's walk to a bridge, and every colony
	// keeps as many as the fewest got.
	context.stage = "canals towers";
	TowerPlan towers = planTowers(map, L, context, o, L.bank);
	std::vector<unsigned char> bridges(n, 0);
	bool anyBridge = false;
	for (int i = 0; i < n; ++i)
		if (L.bridge[i] && L.canal[i] && map.terrainPropertiesAt(t.remainderX(i), i / t.w).walkable)
			bridges[i] = anyBridge = true;
	if (!settleStartingTowers(game, context, towers, o.towers, o.towers > 0 && o.towerCount > 0,
							  anyBridge ? &bridges : nullptr))
		return false;
	const std::vector<unsigned char> pads = towerFootprints(t, towers);

	context.stage = "canals resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	// The tiles a wall or outcrop blocks, and a tile round them where no field grows, so the walls
	// stay in plain view; the bridges' landings stay clear so no field closes a bridge.
	std::vector<unsigned char> blocked(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const TerrainProperties &p = map.terrainPropertiesAt(t.remainderX(i), i / t.w);
		blocked[i] = !p.walkable && !p.swimmable;
	}
	const std::vector<unsigned char> landings = dilate(t, bridges, 2);
	const std::vector<unsigned char> wallMargin = dilate(t, blocked, 1);
	const auto open = [&](int i)
	{
		return L.land[i] && !reserved[i] && !pads[i] && !landings[i] && !L.pad[i] &&
			   clearGround(map, t.remainderX(i), i / t.w);
	};
	for (int k = 0; k < teams; ++k)
		plantOpenHomeKit(map, t, context, L.kits[k], 0.0, L.homeRadius, kHomeWheat, kHomeWood,
						 kHomeQuarry,
						 [&](int i)
						 {
							 return L.homeOf[i] == k && !reserved[i] && !pads[i] &&
									clearGround(map, t.remainderX(i), i / t.w);
						 });
	// The quarries first, at the foot of the rock, before any field takes the verge: round a quarry
	// block's outcrop, and outside the back of a fort's or bastion's parapet (the walls used to be
	// the stone). Scaled by the stone amount.
	int quarryFaces = 0, wallFaces = 0;
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		const int kind = L.kind[cell];
		const auto inBlock = [&](int i) { return L.cell[i] == cell && open(i); };
		if (kind == Quarry)
		{
			const ShapePoint c = tilePoint(L.g.cells[cell].centre);
			quarryFaces += plantQuarryFaces(
				map, t, {KitSeed{int(std::lround(c.x)), int(std::lround(c.y)), int(kQuarryRock) + 4}},
				int(scaledCount(kQuarryFaceTiles, o.stone)), inBlock);
		}
		else if (kind == Fort || kind == Bastion)
		{
			const int back = localTile(L, cell, 0, -(wallReach(o) + 2));
			wallFaces += plantQuarryFaces(map, t, {KitSeed{t.remainderX(back), back / t.w, 4}},
										  int(scaledCount(kWallFaceTiles, o.stone)),
										  [&](int i) { return inBlock(i) && !L.lane[i]; });
		}
	}
	context.telemetry.measure("canals.quarry-faces.tiles", quarryFaces);
	context.telemetry.measure("canals.wall-faces.tiles", wallFaces);
	// Every block is fertile (its canal is within the growth probe's reach of all of it), so the
	// ambient farmland is a share of every block but the kinds that carry their own cover (orchard,
	// quarry, woodlot, wheatfield, marsh, garden), in patches: 15 and 10 in 100 of the fertile tiles
	// for wheat and wood.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 8, context.stream("canals-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 5, context.stream("canals-split"));
	const auto eligible = [&](int i)
	{
		const int kind = L.kind[L.cell[i]];
		return open(i) && !wallMargin[i] && kind != Orchard && kind != Quarry && kind != Woodlot &&
			   kind != Wheatfield && kind != Marsh && kind != Garden;
	};
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += eligible(i) && fertility.at(t.remainderX(i), i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, eligible, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(fertile * 15 / 100, o.wheat)),
								 int(scaledCount(fertile * 10 / 100, o.wood)),
								 int(scaledCount(area / 900, o.stone)), 0};
		},
		"canals-stone", "canals-fruit");
	// Every block's own thing: the three fruits round an orchard's pond, a woodlot's, wheatfield's or
	// garden's cover on the block's most-noise share of ground; and a wheat clump and a wood clump
	// beside every pad, in the block's frame, outside its walls or water.
	const std::vector<int> cover = periodicNoise(t.w, t.h, 6, context.stream("canals-cover"));
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		const ShapePoint centre = tilePoint(L.g.cells[cell].centre);
		const auto inBlock = [&](int i) { return L.cell[i] == cell && open(i); };
		switch (L.kind[cell])
		{
		case Orchard:
			if (scaledCount(1, o.fruit) > 0)
			{
				const double spin = context.bounded("canals-fruit", 3600) / 3600.0 * 2 * kPi;
				plantOrchard(map, t, context, centre.x, centre.y, kOrchardPond + 2.5, {spin}, 4.5,
							 6, 1, inBlock);
			}
			break;
		case Homestead:
		case Hamlet:
		case Fort:
		case Bastion:
		case Funnel:
		case Chicane:
		case Moat:
		{
			struct Spot
			{
				int u, v, type, radius;
			};
			const int r = wallReach(o), m = int(kMoatOuter) + 2, small = kClumpRadius - 1;
			std::vector<Spot> spots;
			switch (L.kind[cell])
			{
			case Homestead:
				spots = {{0, -kClumpOut, WHEAT, kClumpRadius}, {0, kClumpOut, WOOD, small}};
				break;
			case Hamlet:
				spots = {{0, -(kClumpOut - 1), WHEAT, small}, {0, kClumpOut - 1, WOOD, small}};
				break;
			case Fort:
				spots = {{0, -(r + 2), WHEAT, small}, {-(r + 2), 0, WOOD, small}};
				break;
			case Bastion:
				spots = {{-(r + 2), -(r + 2), WHEAT, small}, {r + 2, r + 2, WOOD, small}};
				break;
			case Funnel:
				spots = {{-r, 3, WHEAT, small}, {r, 3, WOOD, small}};
				break;
			case Chicane:
				spots = {{0, -kClumpOut, WHEAT, small}, {0, kClumpOut, WOOD, small}};
				break;
			default:
				spots = {{-m, 0, WHEAT, small}, {m, 0, WOOD, small}};
				break;
			}
			for (const Spot &spot : spots)
			{
				if (scaledCount(1, spot.type == WHEAT ? o.wheat : o.wood) <= 0)
					continue;
				const int at = localTile(L, cell, spot.u, spot.v);
				if (const int seed = seedNear(t, t.remainderX(at), at / t.w, 3,
											  [&](int i) { return inBlock(i) && !wallMargin[i]; });
					seed >= 0)
					placeResourceClump(map, context, MapGeneratorPoint(t.remainderX(seed), seed / t.w),
									   spot.type, spot.radius);
			}
			break;
		}
		case Woodlot:
		case Wheatfield:
		case Garden:
		{
			std::vector<int> candidates;
			for (int i = 0; i < n; ++i)
				if (inBlock(i))
					candidates.push_back(i);
			plantCoverShare(
				map, t, candidates, L.kind[cell] == Woodlot ? WOOD : WHEAT,
				int(scaledCount(kCoverPercent, L.kind[cell] == Woodlot ? o.wood : o.wheat)),
				[&](int i) { return cover[i]; });
			break;
		}
		default:
			break;
		}
	}
	seedAlgae(map, context, t, "canals-algae", o.algae, AlgaeBand::anyWater(25));
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit});
	// The bridges join every colony to the first; only deposits could close the way, so only those
	// are cleared, never a canal.
	context.stage = "canals routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, 8, -1, -1});
	return true;
}

std::string atTile(const Torus &t, int i)
{
	return "(" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) + ")";
}

std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const GenerationMap &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "canals"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int n = t.size();
	// The terrain is the design's, vertex for vertex: quays, beaches, decks, pads, walls and looks.
	for (int i = 0; i < n; ++i)
		if (map.vertexTerrainAt(t.remainderX(i), i / t.w) != L.finished[i])
			return "The city's terrain differs from its design at " + atTile(t, i) + ".";
	// Every wall stands in its group: parapets and outcrops are Ridges, hedges Obstacles.
	std::vector<unsigned char> ridges(n, 0), hedges(n, 0);
	for (int i = 0; i < n; ++i)
	{
		ridges[i] = L.sketch[i] == RIDGE_ROCK;
		hedges[i] = L.sketch[i] == CLIFFS;
	}
	if (const int bad = wallTerrainMismatch(map, t, ridges, TerrainGroup::Ridges); bad >= 0)
		return "A parapet or outcrop is not Ridges at " + atTile(t, bad) + ".";
	if (const int bad = wallTerrainMismatch(map, t, hedges, TerrainGroup::Obstacles); bad >= 0)
		return "A hedge is not an obstacle at " + atTile(t, bad) + ".";
	// Every bridge and causeway is walked, not swum: each deck tile over the water walks.
	for (int i = 0; i < n; ++i)
		if (L.bridge[i] && L.canal[i] && L.deck[i] &&
			!map.terrainPropertiesAt(t.remainderX(i), i / t.w).walkable &&
			map.terrainPropertiesAt(t.remainderX(i), i / t.w).swimmable)
		{
			bool decked = true;
			const int x = t.remainderX(i), y = i / t.w;
			for (int c = 0; c < 4; ++c)
				decked = decked && L.deck[t.at(x + c % 2, y + c / 2)];
			if (decked)
				return "A bridge is swum, not walked, at " + atTile(t, i) + ".";
		}
	// Every moat holds its water round a dry islet, and every lake block keeps its lake.
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
		if (L.kind[cell] == Moat)
		{
			const int ring = localTile(L, cell, 0, -int(kMoatOuter - 1.5));
			const int middle = localTile(L, cell, 0, 0);
			if (!map.isWater(t.remainderX(ring), ring / t.w) ||
				map.isWater(t.remainderX(middle), middle / t.w))
				return "A moat block has lost its moat at " + atTile(t, middle) + ".";
		}
		else if (L.kind[cell] == Lake)
		{
			const ShapePoint c = tilePoint(L.g.cells[cell].centre);
			if (!map.isWater(t.x(int(c.x)), t.y(int(c.y))))
				return "A lake block has lost its lake at (" + std::to_string(int(c.x)) + ", " +
					   std::to_string(int(c.y)) + ").";
		}
	return walkFromFirstColony(map, context.request.nbTeams, "the city", "over the bridges").error;
}
} // namespace

CanalsOptions::CanalsOptions(const GenerationRequest &r)
	: blockShape(r.option("block-shape")), blockSize(r.option("block-size")),
	  canalWidth(r.option("canal-width")), warp(r.option("warp")),
	  extraBridges(r.option("extra-bridges")), towers(r.option("starting-towers")),
	  towerCount(r.option("tower-count")), grandCanal(r.option("grand-canal")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
	blockShape = resolveDesignChoice<design>(r, "block-shape", 2, "canals/block-shape");
}

GeneratorDefinition canalsDefinition()
{
	return {
		"canals",
		29,
		"Canals",
		// Revision 10: Canals 2.0, the lagoon city on the terrain catalogue (quays, boardwalk
		// bridges, paved campi, terrain walls, marshes, gardens and the grand canal).
		// Revision 11: every starting tower opens with full bullets and a full stone reserve.
		11,
		false,
		// Blocks of 24 give a 256 map about a hundred blocks; a canal of 3 corners (two tiles of
		// water) is sealed against diagonal steps and is reached by a level-2 tower, one upgrade
		// from what the colonies start with; a fifth again in extra bridges keeps most blocks
		// islands. Hexagonal blocks sit kHexPitchPercent of the block size apart.
		{GeneratorControl::choice("block-shape", "Block shape", {"Squares", "Hexagons", "Random"},
								  2, ControlGroup::Layout)
			 .withSearchValues({0, 1}),
		 GeneratorControl{"block-size", "Block size", 16, 40, 2, 24, ControlGroup::Layout}
			 .withSearchRange(22, 34),
		 GeneratorControl{"canal-width", "Canal width", 3, 5, 1, 3, ControlGroup::Terrain}
			 .withSearchRange(3, 4),
		 // FEEDBACK 2026-09-13: warp 80 and extra bridges 30 (were 40 and 20).
		 GeneratorControl{"warp", "Warp", 0, 100, 10, 80, ControlGroup::Terrain}.withSearchRange(
			 30, 100),
		 GeneratorControl{"extra-bridges", "Extra bridges", 0, 100, 10, 30, ControlGroup::Layout}
			 .withSearchRange(20, 70),
		 // 2.0: a deep, wide canal round the city that no tower reaches across, crossed by a few
		 // bridges.
		 GeneratorControl::toggle("grand-canal", "Grand canal", true, ControlGroup::Layout)
			 .withSearchValues({0, 1}),
		 // Level 1 by default since 2026-09-14, so players upgrade their own towers.
		 GeneratorControl{"starting-towers", "Starting tower level", 0, 3, 1, 1,
						  ControlGroup::Layout}
			 .withSearchRange(0, 1),
		 GeneratorControl{"tower-count", "Towers per colony", 1, 4, 1, 2, ControlGroup::Layout}
			 .withSearchRange(1, 3),
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		designFailure<design>,
		validateWorld,
		{"terrain:urban", "feature:canals", "feature:bridges", "style:tight-building"}};
}
