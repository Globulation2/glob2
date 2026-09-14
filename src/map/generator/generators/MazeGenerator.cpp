// SPDX-License-Identifier: GPL-3.0-or-later
#include "MazeGenerator.h"
#include "Drawing.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GraphMaze.h"
#include "Grid.h"
#include "Pipeline.h"
#include "Resources.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Tessellation.h"
#include "Unit.h"
#include "Walls.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// A maze in the pen-and-paper sense, built to be lived in. The world is tiled into cells on the
// map's own torus - squares or hexagons, their corners optionally warped into irregular polygons
// (Tessellation.h) - and a recursive backtracker carves a spanning tree of passages between them
// (GraphMaze.h). Every edge the tree leaves closed becomes a wall: a line of stone in a narrow water
// channel. Passages fill their cells with grass, so colonies farm and build their way out into the
// maze, and every colony starts in its own cul-de-sac.
//
// Terrain is designed in a TerrainSketch, one undermap corner at a time. Each tile's terrain comes
// from its four undermap corners (Map::regenerateMap), so the whole map is drawn from one distance
// field: steps from the corners of every wall's stone line. Walking out from a wall, the corners one
// step out stay land, the next channel-width + 1 are water and everything further is land again;
// layBeaches turns the land beside that water to sand. In tiles, that is the stone spine, two sandy
// flank tiles, channel-width all-water tiles, a two-tile shore and then grass. A corner no wall
// reaches gets a pond as wide as a wall's water instead. Nothing else is ever drawn, so a passage
// is simply ground no wall comes near, and its shape follows the cells'.
//
// WHY A MAZE PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Walls are stone,
// which can never be cleared, flanked by water, which ground units cannot cross until they swim,
// so the maze's routes are permanent for the whole early game. A spanning tree means exactly one
// route between any two cells, so every junction is a chokepoint worth holding, and each home is a
// cul-de-sac with a single door that is easy to defend. Loopiness adds a few second routes so a
// defended door can be flanked. Farmland lines the passages' shores, next to channel water where
// wheat and wood regrow, and sand roads keep that growth from ever sealing a passage. Fruit
// sits at the ends of dead ends, off every through route, so fetching it is a deliberate raid.
// Hexagons give every junction three ways on instead of two or three, so their mazes wind more.
//
// THE SIZES AT THE DEFAULTS (256x256, square cells of 32, channel width 2): an 8x8 grid of 64
// cells; each half-cell of 16 tiles is 9 of passage, 5 of shore, flank and spine, and 2 of channel,
// so passages are 19 tiles wide and two passages are 14 tiles apart across a wall. Hexagons at cell
// size 32 are about 48 tiles apart (kHexPitchPercent), which keeps their slanted doorways as wide as
// a small base.
namespace
{

// The undermap corners one step from a wall's stone line stay land (its flank); the channel's
// water starts one step further out.
constexpr int kFlankSteps = 1;

// Half a doorway's width is taken by the wall at each end: the spine, two tiles of flank,
// channel-width tiles of water and two tiles of shore, measured in tiles along the edge. The spine,
// flank and shore account for this many tiles of every half-edge.
constexpr int kWallFootprint = 5;

// Half the width of the narrowest passage allowed: a 9-wide doorway still passes a column of
// workers and seats the 4x4 swarm with room to spare.
constexpr int kMinimumPassageHalf = 4;

// Hexagons sit this share of the cell size apart. A hexagon's edge is only about 0.58 of its pitch,
// so at the square's spacing its doorways would come out too narrow to pass the size check.
constexpr int kHexPitchPercent = 150;

// Every home starts identical: fixed tile counts rather than densities, so the start doesn't
// depend on the resource controls, with wheat and wood 1:1 as for any guaranteed placement.
constexpr int kHomeFarmland = 32;
constexpr int kHomeStone = 12;

// The ambient layer at 100%: wheat, wood and stone tiles per 256 shore tiles, fruit tiles in every
// dead end's treasure, and algae tiles per 400 water tiles. The amount controls are percentages of
// these, as on every other generator (until 2026-09-14 they were these counts themselves).
constexpr int kShoreWheat = 48, kShoreWood = 24, kShoreStone = 16, kTreasureFruit = 9, kAlgae = 24;

// Deposits hug each passage's shores, at most this many tiles in. That leaves a clear lane down
// the middle of every passage however the maze turns, so no deposit can seal a route.
constexpr int kShoreBand = 3;

// Positions inside a cell are measured along its exit and across it in eighths of a tile, so ties
// between tiles are broken exactly the same way on every platform.
constexpr int kFrame = 8;

Tessellation latticeFor(const MazeOptions &o, int width, int height)
{
	if (o.cellShape == int(Tessellation::Shape::Hexagon))
		return hexTessellation(width, height, o.cellSize * kHexPitchPercent / 100);
	return squareTessellation(width, height, o.cellSize);
}

// The walls at both ends of the shortest edge leave this much doorway between them.
int passageHalf(const Tessellation &g, int channelWidth)
{
	return shortestEdgeSteps(g) / 2 - kWallFootprint - channelWidth;
}

// What the settings alone decide, before anything random: the tiling and where the homes go.
struct MazeLayout
{
	Tessellation g;
	int half = 0;
	std::vector<int> homes;
	std::string failure;
};

MazeLayout mazeLayout(const MazeOptions &o, int width, int height, int teams)
{
	MazeLayout L;
	L.g = latticeFor(o, width, height);
	L.half = passageHalf(L.g, o.channelWidth);
	if (L.g.columns < 2 || L.g.rows < 2)
		L.failure =
			"The maze needs at least two cells across and down; use a bigger map or smaller "
			"cells.";
	else if (L.half < kMinimumPassageHalf)
		L.failure = "Channels this wide leave too little grass in each cell; use narrower channels "
					"or bigger cells.";
	else if (int((L.homes = spreadPockets(L.g, teams)).size()) != teams)
		L.failure = "The maze has too few cul-de-sacs for this many colonies; use a bigger map or "
					"smaller cells.";
	return L;
}

std::string validate(const GenerationRequest &r)
{
	return mazeLayout(MazeOptions(r), 1 << r.wDec, 1 << r.hDec, r.nbTeams).failure;
}

// The whole maze as drawn from the seed: the layout's home pattern at a random symmetry of the
// tiling, dealt to the colonies in random order; the carved passages; and the warped cells with
// every tile's cell. A pure function of the request, so validateWorld can build it again.
struct MazeDesign
{
	MazeLayout layout;
	std::vector<unsigned char> isHome, open;
	std::vector<int> exitEdge, deadEnds, labels;
	std::string failure;
};

MazeDesign designMaze(const GenerationRequest &request, GenerationContext &context)
{
	const MazeOptions o(request);
	MazeDesign d;
	d.layout = mazeLayout(o, 1 << request.wDec, 1 << request.hDec, request.nbTeams);
	MazeLayout &L = d.layout;
	Tessellation &g = L.g;
	// The pockets come in the fixed order farthest-point spreading finds them (from cell 0), so the
	// deal decides which colony gets which cul-de-sac (dealStarts, Pipeline.h; FEEDBACK 2026-09-13:
	// the same player kept starting in the same spot).
	if (L.failure.empty())
		dealStarts(context, L.homes);
	if (!L.failure.empty())
	{
		d.failure = L.failure;
		return d;
	}
	// A translation or reflection of a valid layout is still valid.
	const int dColumns = int(context.bounded("maze", g.columns));
	const int dRows = int(context.bounded("maze", g.rows));
	const bool mirrorColumns = context.bounded("maze", 2), mirrorRows = context.bounded("maze", 2);
	for (int &home : L.homes)
		home = g.transform(home, dColumns, dRows, mirrorColumns, mirrorRows);
	context.shuffle(L.homes.begin(), L.homes.end(), "maze");

	d.isHome.assign(g.cells.size(), 0);
	for (int home : L.homes)
		d.isHome[home] = 1;
	d.open.assign(g.edges.size(), 0);
	if (!carveSpanningTree(g, context, "maze", d.isHome, d.open))
	{
		d.failure = "the non-home cells did not form one connected maze";
		return d;
	}
	const std::vector<int> doors = openPocketDoors(g, context, "maze", L.homes, d.isHome, d.open);
	openLoops(g, context, "maze", d.isHome, o.loopiness, d.open);
	d.deadEnds = deadEnds(g, d.open, d.isHome, d.exitEdge);
	for (size_t k = 0; k < L.homes.size(); ++k)
		d.exitEdge[L.homes[k]] = doors[k];

	// Warping never brings two walls (or ponds) that share no corner closer than the narrowest
	// passage allows, nor a wall closer to a cell's centre than half that, so the validation above
	// still holds for the warped cells: walls meeting at a corner may close up, passages may not.
	const int passageSteps = 2 * (kWallFootprint + o.channelWidth + kMinimumPassageHalf);
	std::vector<unsigned char> walls(g.edges.size(), 0);
	for (size_t edge = 0; edge < g.edges.size(); ++edge)
		walls[edge] = !d.open[edge];
	warpCorners(g, warpLimit(g) * o.warp / 100, walls, passageSteps, passageSteps / 2, context,
				"maze-warp");
	d.labels = labelTiles(g);
	if (d.labels.empty())
		d.failure = "the maze's cells did not tile the map";
	context.telemetry.measure("maze.cells.actual", g.cellCount());
	context.telemetry.measure("maze.passage.half-width", L.half);
	if (context.telemetry.enabled())
		context.telemetry.measure("maze.edges.open", std::count(d.open.begin(), d.open.end(), 1));
	context.telemetry.measure("maze.dead-ends.actual", d.deadEnds.size());
	context.telemetry.measure("maze.warp.clearance", passageSteps);
	return d;
}

// Measurements of a cell's tiles from its centre, along its exit (u, positive towards the door)
// and across it (v, positive to the door's right), in kFrame units.
struct CellTile
{
	int x, y, u, v;
};
struct CellFrame
{
	double fx, fy;
	int cx, cy;
	CellTile measure(const Torus &t, int x, int y) const
	{
		const int ox = t.offsetX(cx, x), oy = t.offsetY(cy, y);
		return {x, y, int(std::lround((ox * fx + oy * fy) * kFrame)),
				int(std::lround((ox * -fy + oy * fx) * kFrame))};
	}
};
CellFrame frameFor(const Tessellation &g, int cell, int exitEdge)
{
	const SubtilePoint centre = g.cells[cell].centre;
	const auto ends = g.edgeEnds(exitEdge, cell);
	const double dx = double(ends.first.x + ends.second.x) / 2 - centre.x;
	const double dy = double(ends.first.y + ends.second.y) / 2 - centre.y;
	const double length = std::hypot(dx, dy);
	return {dx / length, dy / length, g.centreTileX(cell), g.centreTileY(cell)};
}

// Places the first `count` tiles of a region in the order `before` ranks them.
template <typename Order>
void fillInOrder(Map &map, std::vector<CellTile> region, int count, int resourceType, Order before)
{
	std::stable_sort(region.begin(), region.end(), before);
	for (int i = 0; i < count && i < int(region.size()); ++i)
		map.setResource(region[i].x, region[i].y, resourceType, 1);
}

// A cell's free grass tiles - no deposit on them, which also leaves out the walls' stone - in row
// order, measured in its exit's frame; `back` is how far they reach behind the centre and `side`
// how far across, both in kFrame units.
std::vector<CellTile> grassOfCell(const Map &map, const Torus &t, const MazeDesign &d, int cell,
								  const CellFrame &frame, int &back, int &side)
{
	std::vector<CellTile> tiles;
	back = side = 0;
	for (int i = 0; i < t.size(); ++i)
		if (d.labels[i] == cell && map.isGrass(i % t.w, i / t.w) &&
			!map.isResource(i % t.w, i / t.w))
		{
			tiles.push_back(frame.measure(t, i % t.w, i / t.w));
			back = std::max(back, -tiles.back().u);
			side = std::max(side, std::abs(tiles.back().v));
		}
	return tiles;
}

// Distance in 8-neighbour steps from every tile to the nearest shore - a tile that isn't pure
// grass - not counting the sand road, which runs down the middle of passages, not their edges.
std::vector<int> shoreDepth(const Map &map, const std::vector<unsigned char> &onRoad)
{
	const Torus t(map);
	std::vector<unsigned char> shore(size_t(t.size()), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			shore[size_t(y) * t.w + x] = !map.isGrass(x, y) && !onRoad[size_t(y) * t.w + x];
	return stepsFrom(t, shore);
}

// A home's own cell, seen from inside looking out through its exit: wheat banks the left shore
// and wood the right, both growing forward from the dead end's back wall; stone is a compact
// deposit at the back wall's centre (a line along it would read as a second wall). Only tiles
// within kShoreBand of a shore take anything (`depth`, from shoreDepth), so the middle of the
// passage stays clear whatever the cell's shape, and so does a square around the swarm, so workers
// always walk straight out. The back wall is the shore behind the centre: its three rows of tiles,
// less the side shores at their ends.
void furnishHome(Map &map, const Torus &t, const MazeDesign &d, const std::vector<int> &depth,
				 int cell)
{
	const CellFrame frame = frameFor(d.layout.g, cell, d.exitEdge[cell]);
	int back, side;
	const std::vector<CellTile> grass = grassOfCell(map, t, d, cell, frame, back, side);
	int backWidth = 0;
	for (const CellTile &c : grass)
		if (c.u <= -back + 2 * kFrame)
			backWidth = std::max(backWidth, std::abs(c.v));
	const int lane = std::max(1 * kFrame, backWidth - kShoreBand * kFrame);
	std::vector<CellTile> left, right, rear;
	for (const CellTile &c : grass)
	{
		const int reach = depth[t.at(c.x, c.y)];
		if (reach < 1 || reach > kShoreBand || map.getBuilding(c.x, c.y) != NOGBID ||
			map.getGroundUnit(c.x, c.y) != NOGUID)
			continue;
		// A 9x9 square round the swarm stays clear: its 4x4 footprint plus the ring its first
		// workers step out into.
		if (std::abs(c.u) <= 4 * kFrame && std::abs(c.v) <= 4 * kFrame)
			continue;
		if (c.u <= -back + 2 * kFrame && std::abs(c.v) <= lane)
			rear.push_back(c);
		else if (c.v < 0)
			left.push_back(c);
		else if (c.v > 0)
			right.push_back(c);
	}
	const auto shoreFromBack = [](const CellTile &a, const CellTile &b)
	{ return a.u != b.u ? a.u < b.u : std::abs(a.v) > std::abs(b.v); };
	const auto nearBackCentre = [back](const CellTile &a, const CellTile &b)
	{
		const int da = std::max(a.u + back, std::abs(a.v)),
				  db = std::max(b.u + back, std::abs(b.v));
		if (da != db)
			return da < db;
		if (std::abs(a.v) != std::abs(b.v))
			return std::abs(a.v) < std::abs(b.v);
		return a.u != b.u ? a.u < b.u : a.v < b.v;
	};
	fillInOrder(map, left, kHomeFarmland, WHEAT, shoreFromBack);
	fillInOrder(map, right, kHomeFarmland, WOOD, shoreFromBack);
	fillInOrder(map, rear, kHomeStone, STONE, nearBackCentre);
}

// A treasure: a compact patch of one fruit at the far end of a dead end, so colonies have to
// go and take it rather than passing it on the way somewhere else. It sits a few tiles back
// from the end wall, so its front always faces the passage's clear lane.
void placeTreasure(Map &map, const Torus &t, const MazeDesign &d, int cell, int size, int fruitType)
{
	const CellFrame frame = frameFor(d.layout.g, cell, d.exitEdge[cell]);
	int back, side;
	std::vector<CellTile> tiles = grassOfCell(map, t, d, cell, frame, back, side);
	const int anchor = -back + 3 * kFrame;
	fillInOrder(map, tiles, size, fruitType,
				[anchor](const CellTile &a, const CellTile &b)
				{
					const int da = std::max(std::abs(a.u - anchor), std::abs(a.v));
					const int db = std::max(std::abs(b.u - anchor), std::abs(b.v));
					if (da != db)
						return da < db;
					if (std::abs(a.v) != std::abs(b.v))
						return std::abs(a.v) < std::abs(b.v);
					return a.u != b.u ? a.u < b.u : a.v < b.v;
				});
}

// Deposits scattered along the shores of every passage outside the homes, as compact clumps
// grown over free shore tiles. Only tiles within kShoreBand of a shore are eligible, which is
// what keeps each passage's middle clear. Densities are per 256 shore tiles; fruitTiles of fruit,
// when the dead ends hold no treasure, follow in small clumps of each kind in turn.
void scatterThroughMaze(Map &map, GenerationContext &context, const MazeDesign &d,
						const std::vector<unsigned char> &onRoad, const MazeOptions &o,
						int fruitTiles)
{
	const int w = map.getW(), h = map.getH();
	const std::vector<int> depth = shoreDepth(map, onRoad);
	const int band = std::min(kShoreBand, d.layout.half - 1);
	std::vector<unsigned char> free(size_t(w) * h, 0);
	std::vector<int> pool;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t i = size_t(y) * w + x;
			if (depth[i] >= 1 && depth[i] <= band && !d.isHome[d.labels[i]] &&
				!map.isResource(x, y) && map.getBuilding(x, y) == NOGBID &&
				map.getGroundUnit(x, y) == NOGUID)
			{
				free[i] = 1;
				pool.push_back(int(i));
			}
		}
	if (pool.empty())
		return;

	auto clump = [&](int resourceType, int size)
	{
		int seed = -1;
		// A few tries for a free seed tile; late in the scatter most of the pool is taken and a
		// clump is simply skipped.
		for (int attempt = 0; attempt < 32 && seed < 0; ++attempt)
		{
			const int candidate = pool[context.bounded("resources", pool.size())];
			if (free[candidate])
				seed = candidate;
		}
		if (seed < 0)
			return 0;
		const auto &steps = kCardinalSteps;
		std::vector<int> frontier{seed};
		free[seed] = 0;
		int placed = 0;
		for (size_t head = 0; head < frontier.size() && placed < size; ++head, ++placed)
		{
			const int x = frontier[head] % w, y = frontier[head] / w;
			map.setResource(x, y, resourceType, 1);
			for (const auto &s : steps)
			{
				const size_t n = size_t(map.normalizeY(y + s[1])) * w + map.normalizeX(x + s[0]);
				if (free[n])
				{
					free[n] = 0;
					frontier.push_back(int(n));
				}
			}
		}
		for (size_t k = placed; k < frontier.size(); ++k)
			free[frontier[k]] = 1;
		return placed;
	};

	const int shore = int(pool.size());
	for (const auto &layer : {std::pair<int, int>{WHEAT, int(scaledCount(kShoreWheat, o.wheat))},
							  std::pair<int, int>{WOOD, int(scaledCount(kShoreWood, o.wood))},
							  std::pair<int, int>{STONE, int(scaledCount(kShoreStone, o.stone))}})
	{
		// Density per 256 shore tiles, so the defaults (wheat 48, wood 24, stone 16) cover about
		// 19%, 9% and 6% of the shore band, in clumps of 4 to 12 tiles: big enough to be worth a
		// trip, small enough to leave room between them. Wheat is twice wood, the ratio that read
		// right in playtesting on the other generators.
		int remaining = shore * layer.second / 256;
		for (int attempt = 0; remaining > 0 && attempt < shore; ++attempt)
			remaining -=
				clump(layer.first, std::min(remaining, 4 + int(context.bounded("resources", 9))));
	}
	for (int attempt = 0, kind = 0; fruitTiles > 0 && attempt < shore;
		 ++attempt, kind = (kind + 1) % 3)
		fruitTiles -=
			clump(CHERRY + kind, std::min(fruitTiles, 3 + int(context.bounded("resources", 3))));
}

// Algae is seeded along the channels. They are only a tile or two wide, so any water tile can
// seed a clump: channel water borders a passage's shore, where the algae stays harvestable, and
// algae on water never blocks anyone on foot.
void seedAlgae(Map &map, GenerationContext &context, int algae)
{
	const int w = map.getW(), h = map.getH();
	std::vector<MapGeneratorPoint> water;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (map.isWater(x, y))
				water.emplace_back(x, y);
	if (water.empty())
		return;
	// Density per 400 water tiles (the default 24 seeds about 6% of the channels), in clumps of up
	// to 5 tiles. The attempt cap only ends the loop on a map with almost no free water.
	int remaining = int(water.size()) * algae / 400;
	for (int attempt = 0; remaining > 0 && attempt < int(water.size()) / 4 + 16; ++attempt)
		remaining -= placeResourceClump(map, context,
										water[context.bounded("resources", water.size())], ALGA, 1);
}

// Each closed edge's stone line, from corner tile to corner tile inclusive, so walls meeting at a
// corner share its tile and nothing can slip between them at any angle.
std::vector<unsigned char> wallSpines(const Torus &t, const MazeDesign &d)
{
	const Tessellation &g = d.layout.g;
	std::vector<unsigned char> spine(size_t(t.size()), 0);
	for (int edge = 0; edge < int(g.edges.size()); ++edge)
		if (!d.open[edge])
		{
			const auto ends = g.edgeEnds(edge, g.edges[edge].cells[0]);
			traceSealedPath(spine, t, {ends.first, ends.second});
		}
	return spine;
}

// The tile of every corner where all the edges meeting are open. No wall reaches it, but the
// passages round it stay apart all the same: its pond keeps the corner's chambers from merging into
// one open plaza, so a junction of passages still reads as one.
std::vector<unsigned char> openCorners(const Torus &t, const MazeDesign &d)
{
	const Tessellation &g = d.layout.g;
	std::vector<unsigned char> walled(g.corners.size(), 0), ponds(size_t(t.size()), 0);
	for (int edge = 0; edge < int(g.edges.size()); ++edge)
		if (!d.open[edge])
			walled[g.edges[edge].corners[0]] = walled[g.edges[edge].corners[1]] = 1;
	for (size_t corner = 0; corner < g.corners.size(); ++corner)
		if (!walled[corner])
			ponds[t.at(int(subtileTile(g.corners[corner].x)),
					   int(subtileTile(g.corners[corner].y)))] = 1;
	return ponds;
}

// A sand road down every open edge, from each cell's centre through the edge's middle to the
// next centre: undermap sand on the corners of every tile a sealed line passes, so each road tile is
// pure sand and consecutive ones share a side. At a home it stops against the swarm's 4x4
// footprint (the settlement is anchored on the centre), so the swarm still stands on grass. Only
// grass corners take sand, so a road can never reach into a channel. Returns the tiles the road
// touches, which the shore scatter doesn't count as shore.
std::vector<unsigned char> layRoads(TerrainSketch &sketch, const Torus &t, const MazeDesign &d)
{
	const Tessellation &g = d.layout.g;
	std::vector<unsigned char> path(size_t(t.size()), 0);
	for (int edge = 0; edge < int(g.edges.size()); ++edge)
		if (d.open[edge])
		{
			const int a = g.edges[edge].cells[0];
			const auto ends = g.edgeEnds(edge, a);
			const SubtilePoint middle{(ends.first.x + ends.second.x) / 2,
									  (ends.first.y + ends.second.y) / 2};
			traceSealedPath(path, t, {g.cells[a].centre, middle, g.centreAcross(edge, a)});
		}
	std::vector<unsigned char> road = tileCorners(t, path);
	for (int cell = 0; cell < g.cellCount(); ++cell)
		if (d.isHome[cell])
			for (int dy = -2; dy <= 2; ++dy)
				for (int dx = -2; dx <= 2; ++dx)
					road[t.at(g.centreTileX(cell) + dx, g.centreTileY(cell) + dy)] = 0;
	std::vector<unsigned char> onRoad(size_t(t.size()), 0);
	for (int i = 0; i < t.size(); ++i)
		if (road[i] && sketch[i] == GRASS)
		{
			sketch[i] = SAND;
			const int x = i % t.w, y = i / t.w;
			onRoad[i] = onRoad[t.at(x - 1, y)] = onRoad[t.at(x, y - 1)] =
				onRoad[t.at(x - 1, y - 1)] = 1;
		}
	return onRoad;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "maze layout";
	const MazeOptions o(context.request);
	Map &map = game.map;
	const int teams = context.request.nbTeams;
	map.makeHomogenMap(WATER);
	for (int i = 0; i < teams; ++i)
		game.addTeam();
	const MazeDesign d = designMaze(context.request, context);
	if (!d.failure.empty())
	{
		context.detail = d.failure;
		return false;
	}
	const Tessellation &g = d.layout.g;
	const std::vector<int> &homes = d.layout.homes;

	context.stage = "maze terrain";
	const Torus t(map);
	const std::vector<unsigned char> spine = wallSpines(t, d);
	const std::vector<int> fromWall = stepsFrom(t, tileCorners(t, spine));
	const std::vector<int> fromPond = stepsFrom(t, tileCorners(t, openCorners(t, d)));
	const int channelEdge = kFlankSteps + 1 + o.channelWidth;
	TerrainSketch sketch(size_t(t.size()), GRASS);
	for (int i = 0; i < t.size(); ++i)
		if ((fromWall[i] > kFlankSteps && fromWall[i] <= channelEdge) ||
			(fromPond[i] >= 0 && fromPond[i] <= channelEdge))
			sketch[i] = WATER;
	layBeaches(sketch, t);
	// Without roads, passages are grass from shore to shore. Map::incResource only seeds a resource
	// on its own terrain and buildings need pure grass, so nothing can grow over a road tile or be
	// built on one: a road links every cell to the maze and can never be closed.
	const std::vector<unsigned char> onRoad =
		o.sandRoads ? layRoads(sketch, t, d) : std::vector<unsigned char>(size_t(t.size()), 0);
	writeUndermap(map, sketch);
	const DesignedStone stone = designedStone(map, t, spine);
	if (stone.gaps)
	{
		context.detail = "a wall spine tile is not solid grass";
		return false;
	}
	for (int i = 0; i < t.size(); ++i)
		if (stone.stone[i])
			map.setResource(i % t.w, i / t.w, STONE, 1);

	const auto chamber = [&](int team)
	{
		std::vector<unsigned char> home(size_t(t.size()), 0);
		for (int i = 0; i < t.size(); ++i)
			home[i] = d.labels[i] == homes[team] && !map.isWater(i % t.w, i / t.w);
		return home;
	};
	// placeSettlement measures from the footprint's top-left tile; this centres the 4x4 swarm.
	const auto centre = [&](int team)
	{ return MapGeneratorPoint(g.centreTileX(homes[team]) - 2, g.centreTileY(homes[team]) - 2); };
	if (!settleColonies(game, context, "starts", chamber, centre))
		return false;

	context.stage = "maze resources";
	const std::vector<int> depth = shoreDepth(map, onRoad);
	for (int home : homes)
		furnishHome(map, t, d, depth, home);
	// Fruit is treasure: one patch at the far end of every dead end that isn't a home, with fruit
	// types dealt round-robin so every kind is somewhere in the maze and a colony has to go and
	// fight for the ones it lacks. Placed before the shore scatter, which works around them.
	// Without treasure the same fruit is scattered along the passages' shores instead.
	std::vector<int> deadEnds = d.deadEnds;
	const int fruit = int(scaledCount(kTreasureFruit, o.fruit));
	if (fruit > 0 && o.treasure)
	{
		context.shuffle(deadEnds.begin(), deadEnds.end(), "resources");
		const int firstType = context.bounded("resources", 3);
		for (size_t i = 0; i < deadEnds.size(); ++i)
			placeTreasure(map, t, d, deadEnds[i], fruit, CHERRY + int((firstType + i) % 3));
	}
	scatterThroughMaze(map, context, d, onRoad, o, o.treasure ? 0 : int(deadEnds.size()) * fruit);
	seedAlgae(map, context, int(scaledCount(kAlgae, o.algae)));
	return true;
}

// Which cells may share ground: two cells round a corner are joined when the open edges at that
// corner connect them, which includes any two cells across an open edge.
std::vector<unsigned char> joinedCells(const MazeDesign &d)
{
	const Tessellation &g = d.layout.g;
	const int n = g.cellCount();
	std::vector<std::vector<int>> cornerCells(g.corners.size());
	for (int cell = 0; cell < n; ++cell)
		for (int corner : g.cells[cell].corners)
			cornerCells[corner].push_back(cell);
	std::vector<unsigned char> joined(size_t(n) * n, 0);
	std::vector<int> parent(n);
	std::iota(parent.begin(), parent.end(), 0);
	const auto find = [&](int c)
	{
		while (parent[c] != c)
			c = parent[c] = parent[parent[c]];
		return c;
	};
	for (size_t corner = 0; corner < g.corners.size(); ++corner)
	{
		for (int cell : cornerCells[corner])
			parent[cell] = cell;
		for (int cell : cornerCells[corner])
			for (int edge : g.cells[cell].edges)
				if (d.open[edge] && (g.edges[edge].corners[0] == int(corner) ||
									 g.edges[edge].corners[1] == int(corner)))
					parent[find(cell)] = find(g.other(edge, cell));
		for (int a : cornerCells[corner])
			for (int b : cornerCells[corner])
				if (find(a) == find(b))
					joined[size_t(a) * n + b] = 1;
	}
	return joined;
}

// The maze's defining guarantees, checked on the finished world rather than trusted: every
// colony can walk to every other one, and the walls hold - no ground a colony can reach joins two
// cells except through the passages the maze opened. Water, buildings and every resource
// (including the wall spines) block the walk; units don't, since they move.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	const ColonyWalk walk =
		walkFromFirstColony(game.map, context.request.nbTeams, "the maze", "through the maze");
	if (!walk.error.empty())
		return walk.error;
	GenerationContext replay(context.request);
	const MazeDesign d = designMaze(context.request, replay);
	if (!d.failure.empty())
		return "The maze could not be rebuilt: " + d.failure;
	const Torus t(game.map);
	std::vector<unsigned char> reached(size_t(t.size()), 0);
	for (int i = 0; i < t.size(); ++i)
		reached[i] = walk.steps[i] >= 0;
	const std::vector<unsigned char> joined = joinedCells(d);
	const int n = d.layout.g.cellCount();
	const RegionLeak leak = firstRegionLeak(t, reached, d.labels, [&](int a, int b)
											{ return joined[size_t(a) * n + b] != 0; });
	if (leak.tile >= 0)
		return "A maze wall leaks at (" + std::to_string(leak.tile % t.w) + ", " +
			   std::to_string(leak.tile / t.w) + ").";
	return "";
}
} // namespace

MazeOptions::MazeOptions(const GenerationRequest &r)
	: cellShape(r.option("cell-shape")), cellSize(r.option("cell-size")),
	  channelWidth(r.option("channel-width")), loopiness(r.option("loopiness")),
	  warp(r.option("warp")), wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount")), sandRoads(r.option("sand-roads") != 0),
	  treasure(r.option("dead-end-treasure") != 0)
{
}

GeneratorDefinition mazeDefinition()
{
	return {
		"maze",
		11,
		"Maze",
		14,
		false,
		{GeneratorControl::choice("cell-shape", "Cell shape", {"Squares", "Hexagons"}, 0),
		 {"cell-size",
		  "Cell size",
		  24,
		  48,
		  1,
		  32,
		  ControlGroup::Layout,
		  false,
		  false,
		  {24, 32, 40, 48}},
		 // Cells snap to 24, 32, 40 or 48 tiles; the default 32 gives 64 square cells on a 256
		 // map, enough corridors to feel like a maze without passages narrower than a small base.
		 // Hexagons sit half as far apart again (kHexPitchPercent).
		 // Warp moves the cells' corners by up to this share of what keeps every cell whole, so
		 // squares and hexagons become irregular polygons; doorways never narrow past the
		 // smallest a passage may be, so at small cells warp is gentler.
		 {"warp", "Warp", 0, 100, 25, 0, ControlGroup::Layout},
		 // Open water on each side of a wall's stone line; passages widen to fill the rest.
		 {"channel-width", "Channel width", 1, 6, 1, 2, ControlGroup::Layout},
		 {"loopiness",
		  "Loopiness",
		  0,
		  50,
		  1,
		  5,
		  ControlGroup::Layout,
		  false,
		  false,
		  {0, 5, 10, 20, 35, 50}},
		 // Loopiness is the share of non-home cells that get one extra opening: the default 5
		 // opens about 3 walls on a 256 map, a few flanking routes without dissolving the maze.
		 // Densities for the deposits scattered along the passages (per 256 shore tiles); homes
		 // always get the same fixed amounts. Fruit is the size, in tiles, of the treasure at
		 // every dead end that isn't a home.
		 // Percentages of the shore scatter, the dead-end treasure and the channel algae (kShoreWheat
		 // and company); every home's kit is unscaled.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount"),
		 // Off, passages are grass from shore to shore, with no sand road down the middle.
		 GeneratorControl::toggle("sand-roads", "Sand roads", true, ControlGroup::Layout),
		 // Off, the treasure's fruit is scattered along the passages' shores instead.
		 GeneratorControl::toggle("dead-end-treasure", "Treasure in dead ends", true,
								  ControlGroup::Resources)},
		generate,
		true,
		validate,
		validateWorld};
}
