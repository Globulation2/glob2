// SPDX-License-Identifier: GPL-3.0-or-later
#include "CanalsGenerator.h"
#include "Channels.h"
#include "Farmland.h"
#include "FertilityField.h"
#include "Game.h"
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
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Tessellation.h"
#include "Topology.h"
#include "Towers.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>
using namespace MapGeneration;

// Canals: a lagoon city. The map is cut into blocks by a grid of narrow canals - squares, or hexagons
// (FEEDBACK 2026-09-14: "support a hexagon lattice ... an option similar to the maze generator") -
// warped so the blocks are irregular, and every canal is just wide enough to stop a unit and just
// narrow enough for a tower on one bank to shoot the other. Every colony starts on a block of its own with a pond and a
// kit, and a handful of sand bridges join the blocks: only the bridges a tree needs to connect the
// colonies' blocks, plus a few more at random, so most blocks are islands until someone can swim.
// Towers reach across the canals from the first minute and armies cannot, so where the first towers
// go is the opening; once swimming pools are built every canal is a road and the map turns inside
// out.
//
// The city has no centre: the colonies' blocks are the ones farthest apart on the block graph
// (spreadPockets), every other block is dealt its kind by a weighted draw, and fairness is
// statistical (the lobby keeps the best-scoring of several seeds).
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Water blocks walking until
// a colony can swim, so the canals are a timing rule; a tower scans square rings with no line of
// sight, and a straight canal w corners wide puts the banks' grass w + 4 tiles apart (Channels.h),
// so the default canal of 3 is reached by a level-2 tower (range 7) and not by a level-1 (range 5).
// Every bank has sand beside water within reach of every tile, so algae grows everywhere and crops
// regrow everywhere; the fight is over bridges and banks, not fields.
//
// FEEDBACK 2026-09-13 (first play): "remove the pond from the user's starting base. increase the
// default rate of bridges across the squares 1.5x, and the default amount of warping 2x. it's also a
// little boring right now: can every grid cell have one of N types? I see some of them occasionally
// have a lake in the center; let's have a few different types - the fruit type, the lake type. I
// want a couple of types that have a sand patch protecting a 4x4 spot on the square, with no
// resources, which can be used as a building spot. maybe a few of them have a sand patch. come up
// with a few more cool ideas; each cell has its own little surprise." So: home blocks have no pond
// (their canal waters them), extra bridges default to 30% (was 20) and warp to 80 (was 40), and
// every other block is dealt one of fourteen kinds (BlockKind) from a weighted draw, and a facing:
// plain fields, a lake, an orchard round a pond, a homestead or a hamlet (building pads with wheat and
// wood beside them), a quarry, a woodlot, a wheatfield, a dune of bare sand, and five kinds built of
// stone or water (second play): a fort, a bastion, a funnel, a chicane and a moat.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say.
constexpr int kHomeWheat = 14, kHomeWood = 12, kHomeQuarry = 2;
// Hexagonal blocks sit this share of the block size apart, as Maze's cells do: a hexagon's edge is only
// about 0.58 of its pitch, so at the square's spacing its canals would leave blocks too small for their
// kinds.
constexpr int kHexPitchPercent = 150;
// A bridge is a line of sand corners kBridgeHalfWidth wide across the canal (a 1-corner line already
// carries units two tiles wide, since a tile with a sand corner is no longer pure water), reaching
// this far past the canal's edge onto each bank so it lands on grass, not beach.
constexpr double kBridgeLanding = 2.5;
// What a block holds (first play: "each cell has its own little surprise"), and the weight each kind
// is dealt with, out of 100. Second play, FEEDBACK 2026-09-13: "i like the new square types you
// added, but too many of them are empty of resources. like with the 4x4 squares - those squares are to
// hold farming buildings. they only matter if there are resources elsewhere on that island outside the
// sand square. can we also add some types that are more like forts? with some stone that guides
// enemies into a kill zone or something? just add some cool ideas like this to make the cells more
// interesting", and "a few of them should be intended for building buildings on of course. but too
// many of them are boring right now". So the pad kinds became homesteads and hamlets (a wheat and a
// wood clump beside every pad, and the ambient fields over the rest of the block), plain blocks went
// from 24 in 100 to 6, and five built kinds joined, each with a clump of wheat and one of wood outside
// its stone or water and the ambient fields too, so what the stone guards is worth guarding.
enum BlockKind
{
	Home = -1,
	Plain = 0,
	Lake,       // a pond at the middle, the fields round it
	Orchard,    // a small pond with the three fruits round it
	Homestead,  // a 4x4 pad of grass in a ring of sand, a wheat and a wood clump either side of it
	Hamlet,     // two such pads side by side, the clumps before and behind them
	Quarry,     // a stone clump of radius 3
	Woodlot,    // wood over most of the block
	Wheatfield, // wheat over most of the block
	Dune,       // a disc of bare sand: walkable, unbuildable, and nothing grows on it
	Fort,       // a square of stone wall round a pad with one gate at the front, the kill zone
	Bastion,    // four corner walls round a pad, an opening in the middle of every side
	Funnel,     // two walls in a V that guide a walk onto a three-tile gap, the pad just behind it
	Chicane,    // two staggered walls with a corridor between them, the way across the block
	Moat        // a ring of water round an islet with a pad, one causeway of sand onto it
};
constexpr int kKinds = 14;
constexpr int kKindWeight[kKinds] = {6, 9, 8, 10, 6, 5, 7, 7, 4, 9, 8, 7, 6, 8};
// The kinds' features: the lake's and the orchard pond's radii, the pad (4 tiles square in a ring one
// corner wide; stampFarmPlot), how far apart a hamlet's two pads stand, the quarry's radius, the dune's
// radius, and the share of a woodlot or wheatfield under its crop.
constexpr double kLakeRadius = 3.5, kOrchardPond = 2.5, kDuneRadius = 4.0;
constexpr int kPadSize = 4, kPadRing = 1, kPadsApart = 10, kQuarryRadius = 3, kCoverPercent = 60;
// The built kinds, in tiles from the block's middle in the block's own frame (every block faces one of
// four ways, dealt with its kind; localTile). A wall's reach is the half block less kWallMargin (a
// beach, a bridge's landing and a lane round the wall), at most kWallReach: 6 on the default block of
// 24, so a fort is 13 tiles square, its wall a tile beyond the pad's sand ring. A fort's gate and the
// funnel's gap are kGateWidth tiles; a bastion's corner arms are two short of the half side, leaving
// kGateWidth + 2 open in the middle of every side; the funnel's walls run diagonally in from the back
// corners, two tiles thick because a unit steps diagonally between two stones on a diagonal; the
// chicane's walls stand kChicaneOffset either side of the middle, each reaching kChicaneOverlap past it
// from its own side, so the corridor between them is entered at one end and left at the other. Any wall
// tile the warp has put within two of water, on sand, or off its block is left out, and a block whose
// walls would cut its land or a bridge off (blockWalls) gets no walls and the pad kind instead. A moat
// is kMoatWidth corners of water (two tiles of water, which only a swimmer crosses) round an islet of
// kMoatOuter - kMoatWidth in radius, exactly a pad, its ring and its beach; it needs a block of
// kMoatMinimumBlock with every corner within kMoatOuter + 1.5 of the middle on the block, else it is
// dealt as a lake. Beside every pad kind stand a wheat clump of kClumpRadius (a homestead's; the others
// one smaller) and a wood clump one smaller, kClumpOut from the middle or just outside a built kind's
// stone or water, so the pad has something to farm.
constexpr int kWallReach = 6, kWallMargin = 4, kGateWidth = 3, kChicaneOffset = 3,
			  kChicaneOverlap = 2;
constexpr double kMoatOuter = 7.5, kMoatWidth = 3.0;
constexpr int kMoatMinimumBlock = 22, kClumpRadius = 3, kClumpOut = 6;
// Tower pads beside each colony's starting towers, and the spacing between a colony's sites.
constexpr int kTowerPads = 2, kTowerSpacing = 6;

struct Layout
{
	Torus t{1, 1};
	Tessellation g;
	std::vector<int> cell; // every tile's block
	std::vector<int> homeCell;
	std::vector<int> kind;            // every block's BlockKind
	std::vector<int> facing;          // every block's quarter turn (0-3) for its built kind
	std::vector<unsigned char> stone; // the built kinds' walls, as tiles
	std::vector<int> homeOf;          // the home block a tile is in, or -1
	std::vector<unsigned char> canal, bridge, water, sand, land;
	Farm pads; // the building pads, stamped as farm plots
	TerrainSketch sketch;
	std::vector<ShapePoint> homes, kits;
	double homeRadius = 0;
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

// THE WALLS of a built kind, as the tiles of the block they may stand on: on the block, clear of water
// by two corners all round (so the beaches leave them pure grass and nothing stands on a bridge's
// landing), and off any pad. The block must stay one piece with them in - its land and its bridges,
// eight ways, as a unit walks - else the block gets no walls and the pad kind instead (a chicane,
// which has no pad, becomes plain).
void blockWalls(Layout &L, int cell, int reach, GenerationContext &context)
{
	const Torus &t = L.t;
	const int n = t.size();
	const auto wallable = [&](int i)
	{
		if (L.cell[i] != cell || L.canal[i] || L.pads.plot[i])
			return false;
		const int x = i % t.w, y = i / t.w;
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
	const auto wall = [&](int u, int v)
	{
		const int i = localTile(L, cell, u, v);
		if (wallable(i))
			tiles.insert(i);
	};
	const int half = kGateWidth / 2;
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
		break;
	case Bastion:
		for (int k = 0; k < reach - 2; ++k)
			for (const int su : {-1, 1})
				for (const int sv : {-1, 1})
				{
					wall(su * reach, sv * (reach - k));
					wall(su * (reach - k), sv * reach);
				}
		break;
	case Funnel:
		for (int k = 0; k <= reach - 3; ++k)
		{
			wall(-reach + k, -reach + k);
			wall(-reach + k + 1, -reach + k);
			wall(reach - k, -reach + k);
			wall(reach - k - 1, -reach + k);
		}
		break;
	case Chicane:
		for (int u = -reach; u <= kChicaneOverlap; ++u)
			wall(u, -kChicaneOffset);
		for (int u = -kChicaneOverlap; u <= reach; ++u)
			wall(u, kChicaneOffset);
		break;
	default:
		return;
	}
	if (tiles.empty())
		return;
	std::vector<unsigned char> walk(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		const bool water = L.sketch[i] == WATER && L.sketch[t.at(x + 1, y)] == WATER &&
						   L.sketch[t.at(x, y + 1)] == WATER &&
						   L.sketch[t.at(x + 1, y + 1)] == WATER;
		walk[i] = (L.cell[i] == cell && !water) || (L.bridge[i] && L.canal[i]);
	}
	for (int i : tiles)
		walk[i] = 0;
	const std::vector<int> region = connectedRegions(walk, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<int> size;
	for (int i = 0; i < n; ++i)
		if (walk[i] && region[i] >= 0)
		{
			if (region[i] >= int(size.size()))
				size.resize(region[i] + 1, 0);
			++size[region[i]];
		}
	// Bridges of other blocks make their own small regions; only pieces of this block's land count.
	int pieces = 0;
	for (int i = 0; i < n; ++i)
		if (walk[i] && L.cell[i] == cell && region[i] >= 0 && size[region[i]] > 0)
		{
			++pieces;
			size[region[i]] = 0;
		}
	if (pieces > 1)
	{
		context.telemetry.fallback(
			"canals.block.walls-omitted",
			"Walls would disconnect the block; using the simpler block kind.", cell);
		L.kind[cell] = L.kind[cell] == Chicane ? Plain : Homestead;
		return;
	}
	for (int i : tiles)
		L.stone[i] = 1;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const CanalsOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// The blocks: a square or hexagonal tiling, warped. Every edge is a canal, so every edge is an
	// obstacle the warp keeps apart: opposite sides of a block stay at least the canal plus eight tiles
	// apart, so a block always keeps a few tiles of grass across its narrowest, and a block's centre
	// keeps at least a fifth of the block from its sides, so a pond fits. The built kinds are drawn in
	// a square frame whatever the block's shape; on a hexagon a wall tile that falls outside the block
	// is simply left out (blockWalls).
	L.g = o.blockShape == int(Tessellation::Shape::Hexagon)
			  ? hexTessellation(t.w, t.h, o.blockSize * kHexPitchPercent / 100)
			  : squareTessellation(t.w, t.h, o.blockSize);
	if (L.g.columns < 2 || L.g.rows < 2)
	{
		L.failure = "The canals need at least two blocks across and down; use a bigger map or "
					"smaller blocks.";
		return L;
	}
	const std::vector<unsigned char> walls(L.g.edges.size(), 1);
	warpCorners(L.g, warpLimit(L.g) * o.warp / 100, walls, o.canalWidth + 8, o.blockSize / 5,
				context, "canals-warp");
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
	// Every other block's kind, dealt from a weighted draw in block order (first play: "each cell
	// has its own little surprise").
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

	// The canals: every edge stroked canalWidth corners wide. A canal's water is canalWidth - 1 tiles
	// across, and a diagonal canal is only sealed against a diagonal step when its water is two tiles
	// thick, which is why the narrowest canal offered is 3.
	L.canal.assign(n, 0);
	for (int edge = 0; edge < int(L.g.edges.size()); ++edge)
	{
		const auto ends = L.g.edgeEnds(edge, L.g.edges[edge].cells[0]);
		const ShapePoint a = tilePoint(ends.first), b = tilePoint(ends.second);
		strokePath(L.canal, t, {{a.x, a.y, o.canalWidth / 2.0}, {b.x, b.y, o.canalWidth / 2.0}});
	}

	// The bridges: a tree of the shortest block-to-block paths from the first colony's block to every
	// other colony's, so every colony can be walked to, and then `extraBridges` percent of the blocks'
	// count in further bridges at random (openLoops), which is where the flanking routes come from.
	std::vector<unsigned char> open(L.g.edges.size(), 0);
	if (teams > 1)
	{
		std::vector<int> parentEdge(L.g.cellCount(), -1), order{L.homeCell[0]};
		std::vector<unsigned char> seen(L.g.cellCount(), 0);
		seen[L.homeCell[0]] = 1;
		for (size_t head = 0; head < order.size(); ++head)
			for (int edge : L.g.cells[order[head]].edges)
			{
				const int next = L.g.other(edge, order[head]);
				if (!seen[next])
				{
					seen[next] = 1;
					parentEdge[next] = edge;
					order.push_back(next);
				}
			}
		for (int k = 1; k < teams; ++k)
			for (int cell = L.homeCell[k]; parentEdge[cell] >= 0;)
			{
				open[parentEdge[cell]] = 1;
				cell = L.g.other(parentEdge[cell], cell);
			}
	}
	openLoops(graph, context, "canals-bridges", std::vector<unsigned char>(L.g.cellCount(), 0),
			  o.extraBridges, open);
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
		const double reach = o.canalWidth / 2.0 + kBridgeLanding;
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
	L.sand.assign(n, 0);
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
	// Every block's water and sand feature, at its middle: a lake, an orchard's pond, a dune, or a
	// moat (a ring of water with one causeway of sand corners across it, at the block's front; a moat
	// whose ring would not lie wholly on its block is a lake).
	const RadialShape lake(kLakeRadius, 0.3, context, "canals-lakes");
	const RadialShape orchardPond(kOrchardPond, 0.3, context, "canals-lakes");
	const RadialShape dune(kDuneRadius, 0.3, context, "canals-dunes");
	const RadialShape moatRing(kMoatOuter, 0.0, context, "canals-moats");
	const RadialShape islet(kMoatOuter - kMoatWidth, 0.0, context, "canals-moats");
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
		if (L.kind[cell] == Lake)
			fillShape(L.water, t, c.x, c.y, lake, 0.0);
		else if (L.kind[cell] == Orchard)
			fillShape(L.water, t, c.x, c.y, orchardPond, 0.0);
		else if (L.kind[cell] == Dune)
			fillShape(L.sand, t, c.x, c.y, dune, 0.0);
		else if (L.kind[cell] == Moat)
		{
			// Round the rounded middle, as the pad will be, so the islet holds it exactly.
			fillShape(L.water, t, cx, cy, moatRing, 0.0);
			fillShape(L.water, t, cx, cy, islet, 0.0, 0);
			for (int v = int(kMoatOuter - kMoatWidth) - 1; v <= int(kMoatOuter) + 1; ++v)
			{
				const int i = localTile(L, cell, 0, v);
				L.water[i] = 0;
				L.sand[i] = 1;
			}
		}
	}
	L.sketch.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
		L.sketch[i] = L.water[i]                                   ? WATER
					  : (L.sand[i] || (L.bridge[i] && L.canal[i])) ? SAND
																   : GRASS;
	// The pads (stampFarmPlot): at the middle of a homestead, fort, bastion or moat, two side by side
	// on a hamlet, and just behind the gap of a funnel.
	const FarmPlot pad{kPadSize, kPadSize, kPadRing};
	L.pads.water = L.water;
	L.pads.sand.assign(n, 0);
	L.pads.plot.assign(n, 0);
	L.pads.row.assign(n, -1);
	const auto stampPad = [&](int cell, int u, int v)
	{
		const int i = localTile(L, cell, u, v);
		stampFarmPlot(L.sketch, t, L.pads, i % t.w - kPadSize / 2, i / t.w - kPadSize / 2, pad);
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
	L.water = L.pads.water;
	// The walls (blockWalls), once the pads' sand is in the sketch, so none stands on it.
	L.stone.assign(n, 0);
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
		blockWalls(L, cell, wallReach(o), context);
	context.telemetry.measure("canals.blocks.actual", L.g.cellCount());
	if (context.telemetry.enabled())
		context.telemetry.measure("canals.bridges.open-edges",
								  std::count(open.begin(), open.end(), 1));
	context.telemetry.measure("canals.home.radius", L.homeRadius);
	if (context.telemetry.enabled())
	{
		const char *kindNames[] = {"home",   "plain",   "lake",    "orchard",    "homestead",
								   "hamlet", "quarry",  "woodlot", "wheatfield", "dune",
								   "fort",   "bastion", "funnel",  "chicane",    "moat"};
		int counts[kKinds + 1] = {};
		for (int kind : L.kind)
			++counts[kind + 1];
		for (int kind = Home; kind < kKinds; ++kind)
			context.telemetry.measure(std::string("canals.block-kind.") + kindNames[kind + 1] +
										  ".count",
									  counts[kind + 1]);
	}
	return L;
}

// The towers every colony starts with: on its own block's bank (against the beach), covering the most
// of other blocks' land across the canal, none within range of another colony's swarm; open pads
// beside them for more.
TowerPlan planTowers(const Map &map, const Layout &L, const GenerationContext &context,
					 const CanalsOptions &o, const std::vector<unsigned char> &bank)
{
	const Torus &t = L.t;
	const int n = t.size(), teams = int(L.homes.size());
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	std::vector<int> owner(n, -1);
	std::vector<unsigned char> buildable(n, 0), target(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (map.isWater(x, y))
			continue;
		// Land on other blocks belongs to "everyone else": worth shooting at from any colony's bank.
		owner[i] = L.homeOf[i] >= 0 ? L.homeOf[i] : teams;
		target[i] = 1;
		buildable[i] =
			L.homeOf[i] >= 0 && map.isGrass(x, y) && !reserved[i] && !map.isResource(x, y);
	}
	TowerRequest request = startingTowerRequest(o.towers, o.towerCount, kTowerPads, kTowerSpacing);
	request.otherWeight = 1;
	request.ownWeight = 0;
	request.against = &bank;
	return chooseTowerSites(t, owner, buildable, target, swarmSurroundings(t, context, 0), teams,
							request);
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "canals layout";
	const CanalsOptions o(context.request);
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

	context.stage = "canals terrain";
	TerrainSketch terrain = L.sketch;
	layBeaches(terrain, t);
	for (int i = 0; i < n; ++i)
		if (L.bridge[i] && L.canal[i])
			terrain[i] = SAND;
	writeUndermap(map, terrain);
	// The built kinds' walls: stone on every designed wall tile (all pure grass, by the design's own
	// check), before anything else is placed.
	for (int i = 0; i < n; ++i)
		if (L.stone[i] && map.isGrass(i % t.w, i / t.w))
			map.setResource(i % t.w, i / t.w, STONE, 1);

	context.stage = "canals colonies";
	if (!settleRoundColonies(game, context, "canals-starts", L.homeOf, L.homes, L.homeRadius))
		return false;

	// Towers on the banks. No tower or pad may close a colony's walk to a bridge, and every colony
	// keeps as many as the fewest got.
	context.stage = "canals towers";
	const std::vector<unsigned char> bank = beachTiles(map, t);
	TowerPlan towers = planTowers(map, L, context, o, bank);
	std::vector<unsigned char> bridges(n, 0);
	bool anyBridge = false;
	for (int i = 0; i < n; ++i)
		if (L.bridge[i] && L.canal[i] && !map.isWater(i % t.w, i / t.w))
			bridges[i] = anyBridge = true;
	if (!settleStartingTowers(game, context, towers, o.towers, o.towers > 0 && o.towerCount > 0,
							  anyBridge ? &bridges : nullptr))
		return false;
	const std::vector<unsigned char> pads = towerFootprints(t, towers);

	context.stage = "canals resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantOpenHomeKit(map, t, context, L.kits[k], 0.0, L.homeRadius, kHomeWheat, kHomeWood,
						 kHomeQuarry,
						 [&](int i)
						 {
							 return L.homeOf[i] == k && !reserved[i] && !pads[i] &&
									clearGround(map, i % t.w, i / t.w);
						 });
	// Every block is fertile (its canal is within the growth probe's reach of all of it), so the
	// ambient farmland is a share of every block but the kinds that carry their own cover (orchard,
	// quarry, woodlot, wheatfield, dune), in patches: the pad kinds and the built kinds too, since a pad
	// only matters with something to farm beside it (second play), 15 and 10 in 100 of the fertile tiles
	// for wheat and wood (12 and 8 while only plain, lake and home blocks had them). The bridges'
	// landings stay clear so no field closes a bridge, and a tile's width round every wall, so the
	// stone stays in plain view.
	const std::vector<unsigned char> landings = dilate(t, bridges, 2);
	const std::vector<unsigned char> wallMargin = dilate(t, L.stone, 1);
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 8, context.stream("canals-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 5, context.stream("canals-split"));
	const auto open = [&](int i)
	{
		return L.land[i] && !reserved[i] && !pads[i] && !landings[i] && !L.pads.plot[i] &&
			   clearGround(map, i % t.w, i / t.w);
	};
	const auto eligible = [&](int i)
	{
		const int kind = L.kind[L.cell[i]];
		return open(i) && !wallMargin[i] && kind != Orchard && kind != Quarry && kind != Woodlot &&
			   kind != Wheatfield && kind != Dune;
	};
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += eligible(i) && fertility.at(i % t.w, i / t.w) > 0;
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
	// Every block's own thing (first play): the three fruits round an orchard's pond, a quarry's
	// stone, a woodlot's or wheatfield's cover on the block's most-noise share of ground; and (second
	// play) a wheat clump and a wood clump beside every pad, in the block's frame, outside its stone or
	// water: a homestead's before and behind the pad, a hamlet's before and behind its pair, a fort's
	// behind and beside it (never before the gate), a bastion's at two corners, a funnel's either side of
	// the pad inside the V, a chicane's outside both walls, a moat's either side of the ring.
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
		case Quarry:
			if (scaledCount(1, o.stone) > 0)
				if (const int seed = seedNear(t, int(centre.x), int(centre.y), 3, inBlock);
					seed >= 0)
					placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w),
									   STONE, kQuarryRadius);
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
				if (const int seed = seedNear(t, at % t.w, at / t.w, 3,
											  [&](int i) { return inBlock(i) && !wallMargin[i]; });
					seed >= 0)
					placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w),
									   spot.type, spot.radius);
			}
			break;
		}
		case Woodlot:
		case Wheatfield:
		{
			std::vector<int> levels;
			for (int i = 0; i < n; ++i)
				if (inBlock(i))
					levels.push_back(cover[i]);
			if (levels.empty())
				break;
			const int share = std::clamp(
				int(scaledCount(kCoverPercent, L.kind[cell] == Woodlot ? o.wood : o.wheat)), 0,
				100);
			const int level = percentile(levels, 100 - share);
			plantCover(map, t, L.land, L.kind[cell] == Woodlot ? WOOD : WHEAT,
					   [&](int i) { return inBlock(i) && cover[i] >= level; });
			break;
		}
		default:
			break;
		}
	}
	seedAlgae(map, context, t, "canals-algae", o.algae, AlgaeBand::anyWater(25));
	clearFarmPlots(map, t, {L.pads});
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit});
	// The bridges join every colony to the first; only deposits could close the way, so only those
	// are cleared, never a canal.
	context.stage = "canals routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, 8, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "canals"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int n = t.size();
	// Every built kind's wall stands, and every moat holds its water round a dry islet.
	for (int i = 0; i < n; ++i)
		if (L.stone[i] && map.getResource(i % t.w, i / t.w).type != STONE)
			return "A wall's stone is missing at (" + std::to_string(i % t.w) + ", " +
				   std::to_string(i / t.w) + ").";
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
		if (L.kind[cell] == Moat)
		{
			const int ring = localTile(L, cell, 0, -int(kMoatOuter - 1.5));
			const int middle = localTile(L, cell, 0, 0);
			if (!map.isWater(ring % t.w, ring / t.w) || map.isWater(middle % t.w, middle / t.w))
				return "A moat block has lost its moat at (" + std::to_string(middle % t.w) + ", " +
					   std::to_string(middle / t.w) + ").";
		}
	// Home blocks are dry by design (first play); every lake block keeps its lake.
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
		if (L.kind[cell] == Lake)
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
	  towerCount(r.option("tower-count")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition canalsDefinition()
{
	return {
		"canals",
		29,
		"Canals",
		5,
		false,
		// Blocks of 24 give a 256 map about a hundred blocks; a canal of 3 corners (two tiles of
		// water) is sealed against diagonal steps and is reached by a level-2 tower, one upgrade
		// from what the colonies start with; a fifth again in extra bridges keeps most blocks
		// islands. Hexagonal blocks sit kHexPitchPercent of the block size apart.
		{GeneratorControl::choice("block-shape", "Block shape", {"Squares", "Hexagons"}, 0,
								  ControlGroup::Layout),
		 {"block-size", "Block size", 16, 40, 2, 24, ControlGroup::Layout},
		 {"canal-width", "Canal width", 3, 5, 1, 3, ControlGroup::Terrain},
		 // FEEDBACK 2026-09-13: warp 80 and extra bridges 30 (were 40 and 20).
		 {"warp", "Warp", 0, 100, 10, 80, ControlGroup::Terrain},
		 {"extra-bridges", "Extra bridges", 0, 100, 10, 30, ControlGroup::Layout},
		 // Level 1 by default since 2026-09-14, so players upgrade their own towers.
		 {"starting-towers", "Starting tower level", 0, 3, 1, 1, ControlGroup::Layout},
		 {"tower-count", "Towers per colony", 1, 4, 1, 2, ControlGroup::Layout},
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		designFailure<design>,
		validateWorld};
}
