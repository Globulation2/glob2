// SPDX-License-Identifier: GPL-3.0-or-later
#include "PowerOfTwo.h"
#include "MazeGenerator.h"
#include "Climates.h"
#include "Drawing.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "GraphMaze.h"
#include "HeightFieldLooks.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Settlements.h"
#include "Sketch.h"
#include "TerrainGroupTable.h"
#include "Tessellation.h"
#include "Unit.h"
#include "Walls.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Maze 2.0: an overgrown jungle temple, a maze in the pen-and-paper sense built to be lived in.
// The world is tiled into cells on the map's own torus - squares or hexagons, their corners
// optionally warped into irregular polygons (Tessellation.h) - and a recursive backtracker carves a
// spanning tree of passages between them (GraphMaze.h), so there is exactly one route between any
// two cells and every junction is a chokepoint. Every edge the tree leaves closed is a temple wall
// of cliff (Obstacles: nothing walks, swims or shoots through it); `loopiness` knocks a few extra
// openings through as rubble breaches, a scree gap between broken wall stubs with mineable rubble
// heaped beside it, so a defended door can be flanked. Every colony starts in its own cul-de-sac.
//
// Revision 17 replaces the original stone-spine walls (a line of stone resource in a water channel
// with sand beaches). That one wall stopped units, was every colony's quarry, watered the crops and
// held the algae. Each job now has terrain of its own:
// - Walls are cliffs, drawn on vertices leaning away from the open ground (wallVertices).
// - Water is placed where the design wants food: a moat inside every home's walls, a fountain in a
//   paved plaza at every junction cell, a pool in the garden cells. Every water body has a sand bank,
//   so its algae regrows.
// - Corridor and dead-end cells grow their food on loam beds along their walls' feet (Fertile ground
//   waters itself and the ground round it). Revision 18 drops the paved temple courts (cells paved
//   wall to wall) and lays every home's open ground as spring meadow (Fertile).
// - A paved road (Paths, twice as fast) runs unbroken down every passage, through every doorway and
//   over every keep's bridge.
// - Stone comes from quarry faces at the foot of every home's back wall and from breach rubble.
//
// Every home is a moated keep: cliff walls, the moat inside them (open at the back, where the quarry
// faces stand on a verge at the wall's foot), its fields on the moat's inner bank and a paved court
// round the swarm. One bridge crosses the moat at the door.
//
// Garden cells are dealt so every home's nearest garden is the same number of cells away (the
// gardens' walk, measured on the carved maze), and that distance is checked again on validation.
namespace
{

// Half the width of the narrowest doorway allowed: a 9-wide doorway still passes a column of
// workers.
constexpr int kMinimumPassageHalf = 4;

// Hexagons sit this share of the cell size apart: a hexagon's edge is only about 0.58 of its pitch,
// so at the square's spacing its doorways and keeps come out too small.
constexpr int kHexPitchPercent = 125;

// Inside a home's moat and its bank: three tiles of fields and a paved court of radius kCoreRadius
// round the swarm, with a tile to spare.
constexpr int kCoreRadius = 4;
constexpr int kHomeYard = 3 + kCoreRadius + 1;

// The verge between a home's back wall and the moat's open ends, where the quarry faces stand.
constexpr int kBackVerge = 5;
// How far from a wall's tiles a quarry face may stand: a wall drawn on vertices leans away from open
// ground, so the tile beside it is mixed and few pure tiles touch the cliff itself.
constexpr int kQuarryReach = 3;

// Every home starts identical: fixed tile counts rather than densities, so the start doesn't depend
// on the resource controls, with wheat and wood 1:1 as for any guaranteed placement.
constexpr int kHomeFarmland = 32;
// A keep whose bank is too short for its kit (a warped cell) plants up to this many tiles further
// inland, so warp doesn't shrink every home's kit to the smallest keep's.
constexpr int kKitSpill = 3;
constexpr int kHomeStone = 12;

// The ambient layer at 100%: wheat, wood and stone tiles per 256 tiles of growing ground (loam and
// the banks of every water), fruit tiles in every dead end's treasure, algae tiles per 400 water
// tiles, and rubble stone on each side of every breach. The amount controls are percentages of these.
constexpr int kShoreWheat = 40, kShoreWood = 20, kShoreStone = 8, kTreasureFruit = 9, kAlgae = 24;
constexpr int kRubbleStone = 6;

// Deposits stay within this many tiles of water, or on loam, which lies along the walls' feet. That
// leaves a clear lane down the middle of every passage however the maze turns.
constexpr int kShoreBand = 3;

// Loam beds reach this many tiles out from a corridor's walls: narrower beds lose more to their
// unbuildable edge row than they water (River, Watershed).
constexpr int kLoamDepth = 5;

// A junction's fountain and the paved plaza round it; a garden's pool.
constexpr int kFountainRadius = 2;
constexpr int kPlazaRadius = 6;
constexpr int kGardenPoolRadius = 4;

// A breach's gap runs this far along its wall either side of the wall's middle.
constexpr int kBreachHalf = 4;

// The number of cells from a home's door to its nearest garden the deal tries first: one cell
// beyond the passage outside the door, so a garden is a short walk into the maze, not a forecourt.
constexpr int kPreferredGardenSteps = 2;

// Positions inside a cell are measured along its exit and across it in eighths of a tile, so ties
// between tiles are broken exactly the same way on every platform.
constexpr int kFrame = 8;

enum CellKind : unsigned char
{
	Corridor,
	Home,
	DeadEnd,
	Junction,
	Garden
};

int wallRadius(const MazeOptions &o)
{
	return o.wallThickness / 2;
}

// Water tiles in a home's moat band; one more than the control so the row the wall's vertices take
// still leaves the width asked for.
int moatBand(const MazeOptions &o)
{
	return o.channelWidth + 1;
}

Tessellation latticeFor(const MazeOptions &o, int width, int height)
{
	if (o.cellShape == int(Tessellation::Shape::Hexagon))
		return hexTessellation(width, height, o.cellSize * kHexPitchPercent / 100);
	return squareTessellation(width, height, o.cellSize);
}

// What the settings alone decide, before anything random: the tiling and where the homes go.
struct MazeLayout
{
	Tessellation g;
	int half = 0, room = 0;
	std::vector<int> homes;
	std::string failure;
};

MazeLayout mazeLayout(const MazeOptions &o, int width, int height, int teams)
{
	MazeLayout L;
	L.g = latticeFor(o, width, height);
	const int r = wallRadius(o);
	// The walls at both ends of the shortest edge leave this much doorway between them.
	L.half = shortestEdgeSteps(L.g) / 2 - r - 1;
	// What a keep has inside its walls, moat and bank.
	L.room = centreClearance(L.g) - r - moatBand(o) - 1;
	if (L.g.columns < 2 || L.g.rows < 2)
		L.failure =
			"The maze needs at least two cells across and down; use a bigger map or smaller "
			"cells.";
	else if (L.half < kMinimumPassageHalf)
		L.failure = "Walls this thick leave the doorways too narrow; use thinner walls or bigger "
					"cells.";
	else if (L.room < kHomeYard)
		L.failure = "Moats this wide leave too little room inside each home; use a narrower moat, "
					"thinner walls or bigger cells.";
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
// tiling, dealt to the colonies in random order; the carved passages and breaches; every cell's
// kind; and the warped cells with every tile's cell. A pure function of the request, so
// validateWorld can build it again.
struct MazeDesign
{
	MazeLayout layout;
	std::vector<unsigned char> isHome, open, breach, kind;
	std::vector<int> exitEdge, deadEnds, labels;
	std::vector<int> gardenSteps; // per colony (team order): cells to its nearest garden, -1 for none
	std::uint32_t lookSeed = 0;
	std::string failure;
};

// Cells from `from` to every cell over the open edges, -1 where unreachable.
std::vector<int> cellSteps(const Tessellation &g, const std::vector<unsigned char> &open, int from)
{
	std::vector<int> steps(g.cellCount(), -1);
	std::vector<int> queue{from};
	steps[from] = 0;
	for (size_t head = 0; head < queue.size(); ++head)
	{
		const int cell = queue[head];
		for (int edge : g.cells[cell].edges)
			if (open[edge])
			{
				const int next = g.other(edge, cell);
				if (steps[next] < 0)
				{
					steps[next] = steps[cell] + 1;
					queue.push_back(next);
				}
			}
	}
	return steps;
}

// Gardens dealt to the homes so each home's nearest is the same number of cells away: the first
// distance (kPreferredGardenSteps, then outwards) at which every home has a cell of its own that
// close and no nearer any other home, then the rest of the share anywhere. Records each home's
// nearest garden in d.gardenSteps.
void dealGardens(MazeDesign &d, const MazeOptions &o, GenerationContext &context)
{
	const Tessellation &g = d.layout.g;
	const std::vector<int> &homes = d.layout.homes;
	const int n = g.cellCount(), teams = int(homes.size());
	d.gardenSteps.assign(teams, -1);
	std::vector<int> candidates;
	for (int cell = 0; cell < n; ++cell)
		if (d.kind[cell] == Corridor || d.kind[cell] == Junction)
			candidates.push_back(cell);
	const int nonHome = n - teams;
	const int wanted = o.gardens <= 0 ? 0
									  : std::max(teams, int(std::lround(nonHome * o.gardens / 100.0)));
	if (wanted == 0 || candidates.empty())
		return;
	std::vector<std::vector<int>> steps;
	int farthest = 0;
	for (int home : homes)
	{
		steps.push_back(cellSteps(g, d.open, home));
		for (int s : steps.back())
			farthest = std::max(farthest, s);
	}
	context.shuffle(candidates.begin(), candidates.end(), "maze-gardens");
	std::vector<int> order;
	order.push_back(kPreferredGardenSteps);
	for (int s = 1; s <= farthest; ++s)
		if (s != kPreferredGardenSteps)
			order.push_back(s);
	std::vector<unsigned char> taken(n, 0);
	bool dealt = false;
	for (int distance : order)
	{
		std::vector<int> pick(teams, -1);
		std::vector<unsigned char> used(n, 0);
		bool all = true;
		for (int k = 0; k < teams && all; ++k)
		{
			for (int cell : candidates)
			{
				if (used[cell] || steps[k][cell] != distance)
					continue;
				bool nearer = false;
				for (int j = 0; j < teams && !nearer; ++j)
					nearer = j != k && steps[j][cell] >= 0 && steps[j][cell] < distance;
				if (nearer)
					continue;
				pick[k] = cell;
				used[cell] = 1;
				break;
			}
			all = pick[k] >= 0;
		}
		if (!all)
			continue;
		for (int cell : pick)
			taken[cell] = 1;
		dealt = true;
		break;
	}
	// No distance suits every home (a crowded map): each home's nearest free cell instead, and the
	// spread is what validation judges.
	if (!dealt)
		for (int k = 0; k < teams; ++k)
		{
			int best = -1;
			for (int cell : candidates)
				if (!taken[cell] && steps[k][cell] > 0 &&
					(best < 0 || steps[k][cell] < steps[k][best]))
					best = cell;
			if (best >= 0)
				taken[best] = 1;
		}
	int count = int(std::count(taken.begin(), taken.end(), 1));
	for (int cell : candidates)
		if (count < wanted && !taken[cell])
		{
			taken[cell] = 1;
			++count;
		}
	for (int cell = 0; cell < n; ++cell)
		if (taken[cell])
			d.kind[cell] = Garden;
	for (int k = 0; k < teams; ++k)
		for (int cell = 0; cell < n; ++cell)
			if (taken[cell] && steps[k][cell] >= 0 &&
				(d.gardenSteps[k] < 0 || steps[k][cell] < d.gardenSteps[k]))
				d.gardenSteps[k] = steps[k][cell];
}

MazeDesign designMaze(const GenerationRequest &request, GenerationContext &context)
{
	const MazeOptions o(request);
	context.telemetry.choice("maze.cell-shape", o.cellShape == 0 ? "Squares" : "Hexagons");
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
	const std::vector<unsigned char> tree = d.open;
	openLoops(g, context, "maze", d.isHome, o.loopiness, d.open);
	d.breach.assign(g.edges.size(), 0);
	for (size_t edge = 0; edge < g.edges.size(); ++edge)
		d.breach[edge] = d.open[edge] && !tree[edge];
	d.deadEnds = deadEnds(g, d.open, d.isHome, d.exitEdge);
	for (size_t k = 0; k < L.homes.size(); ++k)
		d.exitEdge[L.homes[k]] = doors[k];

	// Every cell's kind: homes, the dead ends' treasure cells, junctions (three ways or more) with
	// their fountains, and the rest corridors, among which gardens are dealt.
	d.kind.assign(g.cells.size(), Corridor);
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		int ways = 0;
		for (int edge : g.cells[cell].edges)
			ways += d.open[edge] != 0;
		if (ways >= 3)
			d.kind[cell] = Junction;
	}
	for (int cell : d.deadEnds)
		d.kind[cell] = DeadEnd;
	for (int home : L.homes)
		d.kind[home] = Home;
	dealGardens(d, o, context);

	// Warping never brings two walls that share no corner closer than the narrowest passage allows,
	// so the validation above still holds for the warped cells: walls meeting at a corner may close
	// up, passages may not. A breach's stubs are walls too.
	const int r = wallRadius(o);
	const int passageSteps = 2 * (r + 1 + kMinimumPassageHalf);
	const int keepClearance = r + moatBand(o) + 1 + kHomeYard;
	std::vector<unsigned char> walls(g.edges.size(), 0);
	for (size_t edge = 0; edge < g.edges.size(); ++edge)
		walls[edge] = !d.open[edge] || d.breach[edge];
	const std::vector<SubtilePoint> unwarped = g.corners;
	warpCorners(g, warpLimit(g) * o.warp / 100, walls, passageSteps,
				std::max(passageSteps / 2, keepClearance - kHomeYard / 2), context, "maze-warp");
	// The keeps were built, the ruin round them has shifted: every home's corners go back where
	// the lattice put them, so every keep keeps the same shape and room however far the rest
	// warped. warpLimit bounds each move so that no mix of moved and unmoved corners folds a cell.
	for (int home : L.homes)
		for (int corner : g.cells[home].corners)
			g.corners[corner] = unwarped[corner];
	d.labels = labelTiles(g);
	if (d.labels.empty())
		d.failure = "the maze's cells did not tile the map";
	d.lookSeed = context.bounded("maze-looks", 1u << 30);

	context.telemetry.measure("maze.cells.actual", g.cellCount());
	context.telemetry.measure("maze.passage.half-width", L.half);
	context.telemetry.measure("maze.keep.room", L.room);
	if (context.telemetry.enabled())
	{
		context.telemetry.measure("maze.edges.open", std::count(d.open.begin(), d.open.end(), 1));
		context.telemetry.measure("maze.breaches.actual",
								  std::count(d.breach.begin(), d.breach.end(), 1));
		for (const auto &[kind, key] :
			 {std::pair<int, const char *>{Junction, "maze.junctions.actual"},
			  {Garden, "maze.gardens.actual"}})
			context.telemetry.measure(key, std::count(d.kind.begin(), d.kind.end(), kind));
		int least = -1, most = -1;
		for (int s : d.gardenSteps)
			if (s >= 0)
			{
				least = least < 0 ? s : std::min(least, s);
				most = std::max(most, s);
			}
		context.telemetry.measure("maze.garden.steps.min", least);
		context.telemetry.measure("maze.garden.steps.max", most);
	}
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

// How far back a cell reaches behind its centre, in kFrame units: the largest -u of its tiles.
int cellBack(const Torus &t, const MazeDesign &d, int cell, const CellFrame &frame)
{
	int back = 0;
	for (int i = 0; i < t.size(); ++i)
		if (d.labels[i] == cell)
			back = std::max(back, -frame.measure(t, t.remainderX(i), i / t.w).u);
	return back;
}

// The middle of an edge as it lies on `cell`'s outline (its owner's by default): across the seam
// the two cells see the edge at different images.
SubtilePoint edgeMiddle(const Tessellation &g, int edge, int cell = -1)
{
	const auto ends = g.edgeEnds(edge, cell < 0 ? g.edges[edge].cells[0] : cell);
	return {(ends.first.x + ends.second.x) / 2, (ends.first.y + ends.second.y) / 2};
}

// The maze's terrain, a pure function of its design (so validateWorld rebuilds it and compares it
// vertex for vertex), with the tile masks the later stages need.
struct MazeTerrain
{
	TerrainSketch mechanics, finished;
	// Tiles: walls, breach floors, the road through each doorway, the tiles a road corner touches, home moats,
	// bridges, and the paved court round each swarm.
	std::vector<unsigned char> wall, floor, throat, onRoad, moat, bridge;
	std::vector<unsigned char> wallVertex;
	// The ground within kQuarryReach tiles of a wall, where quarry faces stand.
	std::vector<unsigned char> wallFoot;
	// Ground kept clear of deposits: the road's own tiles and the ground round each bridge.
	std::vector<unsigned char> landing;
};

MazeTerrain sketchTerrain(const MazeDesign &d, const MazeOptions &o, const Torus &t)
{
	const Tessellation &g = d.layout.g;
	const int n = t.size(), r = wallRadius(o);
	MazeTerrain M;
	const auto cellOf = [&](int i) { return d.labels[i]; };
	const auto kindOf = [&](int i) { return d.kind[d.labels[i]]; };

	// Walls: every closed edge, and every breach's wall less the gap round its middle.
	std::vector<unsigned char> closed(g.edges.size(), 0);
	for (size_t edge = 0; edge < g.edges.size(); ++edge)
		closed[edge] = !d.open[edge];
	M.wall = rasterizeBoundaries(g, closed, r);
	M.floor.assign(n, 0);
	{
		const std::vector<unsigned char> breachWall = rasterizeBoundaries(g, d.breach, r);
		std::vector<unsigned char> gap(n, 0);
		const int reach = kBreachHalf + r;
		for (int edge = 0; edge < int(g.edges.size()); ++edge)
			if (d.breach[edge])
			{
				const SubtilePoint mid = edgeMiddle(g, edge);
				const int mx = int(subtileTile(mid.x)), my = int(subtileTile(mid.y));
				for (int dy = -reach; dy <= reach; ++dy)
					for (int dx = -reach; dx <= reach; ++dx)
						if (dx * dx + dy * dy <= reach * reach)
							gap[t.at(mx + dx, my + dy)] = 1;
			}
		for (int i = 0; i < n; ++i)
		{
			if (breachWall[i] && !gap[i])
				M.wall[i] = 1;
			M.floor[i] = breachWall[i] && gap[i] && !M.wall[i];
		}
	}
	std::vector<std::int64_t> fromWall = distanceSquaredTo(t, M.wall);
	M.wallFoot = dilate(t, M.wall, kQuarryReach);

	// Homes: the moat band inside the walls, open at the back where the verge and its quarry stand.
	std::vector<unsigned char> water(n, 0);
	M.moat.assign(n, 0);
	{
		std::vector<unsigned char> outside(n, 0);
		for (int i = 0; i < n; ++i)
			outside[i] = !d.isHome[cellOf(i)];
		const std::vector<std::int64_t> fromOutside = distanceSquaredTo(t, outside);
		const double inner = r + .5, outer = r + .5 + moatBand(o);
		for (int home : d.layout.homes)
		{
			const CellFrame frame = frameFor(g, home, d.exitEdge[home]);
			const int back = cellBack(t, d, home, frame);
			// The verge behind the moat's open ends must hold the quarry: on a warped or hexagonal
			// cell the back is often a corner and a fixed verge a sliver, so the moat opens further
			// forward, a tile at a time, until the verge has room for three quarries at the wall's
			// foot (the swarm's surroundings kept clear).
			int cut = -back + (r + 1 + kBackVerge) * kFrame;
			for (; cut < 0; cut += kFrame)
			{
				int room = 0;
				for (int i = 0; i < n; ++i)
				{
					if (cellOf(i) != home || M.wall[i] || !M.wallFoot[i])
						continue;
					const CellTile c = frame.measure(t, t.remainderX(i), i / t.w);
					room += c.u <= cut && std::max(std::abs(c.u), std::abs(c.v)) > 7 * kFrame;
				}
				if (room >= 3 * kHomeStone)
					break;
			}
			for (int i = 0; i < n; ++i)
			{
				if (cellOf(i) != home || M.wall[i])
					continue;
				const double depth = std::sqrt(double(fromOutside[i]));
				if (depth > inner && depth <= outer &&
					frame.measure(t, t.remainderX(i), i / t.w).u > cut)
					M.moat[i] = water[i] = 1;
			}
		}
	}
	// Fountains at junctions and pools in gardens, at the cells' centres.
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		const int radius = d.kind[cell] == Junction ? kFountainRadius
						   : d.kind[cell] == Garden ? kGardenPoolRadius
													: -1;
		if (radius < 0)
			continue;
		const int cx = g.centreTileX(cell), cy = g.centreTileY(cell);
		for (int dy = -radius; dy <= radius; ++dy)
			for (int dx = -radius; dx <= radius; ++dx)
			{
				const int i = t.at(cx + dx, cy + dy);
				if (dx * dx + dy * dy <= radius * radius + radius && cellOf(i) == cell && !M.wall[i])
					water[i] = 1;
			}
	}

	// The road: from each cell's centre through every open doorway's middle to the next centre,
	// a sealed line (the bridge at a home's door is laid whatever the roads setting).
	std::vector<unsigned char> trace(n, 0);
	for (int edge = 0; edge < int(g.edges.size()); ++edge)
		if (d.open[edge] && !d.breach[edge])
		{
			const int a = g.edges[edge].cells[0];
			traceSealedPath(trace, t, {g.cells[a].centre, edgeMiddle(g, edge), g.centreAcross(edge, a)});
		}
	for (int i = 0; i < n; ++i)
		if (M.wall[i] || water[i])
			trace[i] = 0;
	M.bridge.assign(n, 0);
	{
		std::vector<unsigned char> crossing(n, 0);
		for (int i = 0; i < n; ++i)
			crossing[i] = trace[i] && M.moat[i];
		// The trace may graze the moat beside its crossing: the bridge spans the band round it.
		std::vector<unsigned char> span = dilate(t, crossing, 2);
		for (int i = 0; i < n; ++i)
			M.bridge[i] = span[i] && d.isHome[cellOf(i)] && !M.wall[i] &&
						  (M.moat[i] || std::sqrt(double(fromWall[i])) <= r + moatBand(o) + 2);
		// Re-trace the crossing through water, which the road trace above stopped at.
		std::vector<unsigned char> full(n, 0);
		for (int home : d.layout.homes)
		{
			const int edge = d.exitEdge[home];
			traceSealedPath(full, t, {g.cells[home].centre, edgeMiddle(g, edge, home)});
		}
		for (int i = 0; i < n; ++i)
		{
			if (full[i] && M.moat[i])
				crossing[i] = 1;
			if (!M.bridge[i] && crossing[i])
				M.bridge[i] = 1;
		}
		// The bridge spans the moat's width and three tiles across (the crossing and one either side).
		const std::vector<unsigned char> wide = dilate(t, crossing, 1);
		for (int i = 0; i < n; ++i)
			M.bridge[i] = wide[i] && d.isHome[cellOf(i)] && !M.wall[i] &&
						  (M.moat[i] || M.bridge[i]);
		for (int i = 0; i < n; ++i)
			if (M.bridge[i])
				water[i] = M.moat[i] = 0;
	}

	// Doorways: the road's tiles where it passes through a doorway. They stay paved like the rest of
	// the road (one continuous road reads better than rough patches at every cell boundary), but no
	// wall takes a corner of them and no deposit is planted on them.
	M.throat.assign(n, 0);
	{
		std::vector<unsigned char> line(n, 0);
		for (int edge = 0; edge < int(g.edges.size()); ++edge)
			if (d.open[edge] && !d.breach[edge])
			{
				const auto ends = g.edgeEnds(edge, g.edges[edge].cells[0]);
				traceSealedPath(line, t, {ends.first, ends.second});
			}
		const std::vector<unsigned char> band = dilate(t, line, 2);
		for (int i = 0; i < n; ++i)
			M.throat[i] =
				band[i] && trace[i] && !M.wall[i] && !water[i] && !M.floor[i] && !M.bridge[i];
	}

	// The mechanic sketch, one canonical type per rule.
	TerrainSketch &s = M.mechanics;
	s.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
		if (cornerOf(t, water, i))
			s[i] = WATER;
	layBeaches(s, t);
	// Loam along the feet of a corridor's or dead end's walls.
	const double loamReach = r + .5 + kLoamDepth;
	std::vector<unsigned char> loam(n, 0);
	for (int i = 0; i < n; ++i)
		loam[i] = (kindOf(i) == Corridor || kindOf(i) == DeadEnd) && !M.wall[i] && !M.throat[i] &&
				  !M.floor[i] && std::sqrt(double(fromWall[i])) <= loamReach;
	for (int i = 0; i < n; ++i)
		if (s[i] == GRASS && cornerOf(t, loam, i))
			s[i] = LOAM;
	// Roles for the looks: every paved vertex is a court (temple paving), a road or a bridge.
	std::vector<unsigned char> court(n, 0), road(n, 0), deck(n, 0);
	std::vector<unsigned char> courtTiles(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int cell = cellOf(i);
		if (M.wall[i] || water[i])
			continue;
		if (d.kind[cell] == Junction)
		{
			const int ox = t.offsetX(g.centreTileX(cell), t.remainderX(i));
			const int oy = t.offsetY(g.centreTileY(cell), i / t.w);
			courtTiles[i] = ox * ox + oy * oy <= kPlazaRadius * kPlazaRadius;
		}
	}
	for (int i = 0; i < n; ++i)
		if ((s[i] == GRASS || s[i] == LOAM) && cornerOf(t, courtTiles, i))
			s[i] = TRAIL, court[i] = 1;
	for (int home : d.layout.homes)
	{
		const int cx = g.centreTileX(home), cy = g.centreTileY(home);
		paveCore(s, t, cx, cy, kCoreRadius, [&](int i) { return cellOf(i) == home; });
		for (int dy = -kCoreRadius; dy <= kCoreRadius; ++dy)
			for (int dx = -kCoreRadius; dx <= kCoreRadius; ++dx)
				if (s[t.at(cx + dx, cy + dy)] == TRAIL)
					court[t.at(cx + dx, cy + dy)] = 1;
	}
	M.onRoad.assign(n, 0);
	if (o.pavedRoads)
	{
		const std::vector<unsigned char> corners = tileCorners(t, trace);
		for (int i = 0; i < n; ++i)
			if (corners[i] && (s[i] == GRASS || s[i] == LOAM || s[i] == SAND))
			{
				s[i] = TRAIL;
				road[i] = 1;
				const int x = t.remainderX(i), y = i / t.w;
				M.onRoad[i] = M.onRoad[t.at(x - 1, y)] = M.onRoad[t.at(x, y - 1)] =
					M.onRoad[t.at(x - 1, y - 1)] = 1;
			}
	}
	// The bridge: decked over the moat, the road carried straight across it.
	{
		const std::vector<unsigned char> corners = tileCorners(t, M.bridge);
		for (int i = 0; i < n; ++i)
			if (corners[i])
			{
				s[i] = TRAIL;
				deck[i] = 1;
				road[i] = court[i] = 0;
			}
	}
	// Breach floors, then the walls over everything.
	{
		const std::vector<unsigned char> rubble = tileCorners(t, M.floor);
		for (int i = 0; i < n; ++i)
			if (rubble[i] && s[i] != WATER)
			{
				s[i] = SCREE;
				road[i] = court[i] = deck[i] = 0;
			}
	}
	{
		std::vector<unsigned char> openGround(n, 0), strict(n, 0);
		for (int i = 0; i < n; ++i)
		{
			openGround[i] = !M.wall[i] && !water[i];
			strict[i] = M.throat[i] || M.floor[i] || M.bridge[i];
		}
		M.wallVertex = wallVertices(t, M.wall, &openGround, &strict);
		for (int i = 0; i < n; ++i)
			if (M.wallVertex[i])
			{
				s[i] = CLIFFS;
				road[i] = court[i] = deck[i] = 0;
			}
	}

	// Every home's open ground is spring meadow (Fertile: buildable crop land that waters the ground
	// round it), its fields, banks' backs and court edges alike: a green keep in the temple
	// (FEEDBACK 2026-11-11).
	for (int i = 0; i < n; ++i)
		if (s[i] == GRASS && d.isHome[cellOf(i)])
			s[i] = LOAM;

	// The looks: overgrown road for every road, plaza and keep court, a boardwalk bridge,
	// spring meadow over every home, moss over the loam and lush grass round the water, split by
	// fractal noise.
	std::mt19937 rng(d.lookSeed);
	const std::vector<int> moss = fractalNoise(t.w, t.h, 16, 3, rng);
	const std::vector<int> lush = fractalNoise(t.w, t.h, 24, 3, rng);
	const std::vector<int> fromWet = stepsFromWet(t, s);
	TerrainSketch looks(n, 0);
	for (int i = 0; i < n; ++i)
	{
		// One paving for every road, plaza and keep court; only the moat's bridge is a boardwalk.
		if (deck[i])
			looks[i] = BOARDWALK;
		else if (road[i] || court[i] || s[i] == TRAIL)
			looks[i] = OVERGROWN_ROAD;
		else if (s[i] == LOAM && d.isHome[cellOf(i)])
			looks[i] = SPRING_MEADOW;
		else if (s[i] == LOAM && moss[i] > 30000)
			looks[i] = MOSS;
		else if (s[i] == GRASS)
		{
			const int kind = d.kind[cellOf(i)];
			const bool tended = kind == Garden || kind == Home;
			const int wet = fromWet[i] < 0 ? 99 : fromWet[i];
			if ((tended && lush[i] > 20000) || lush[i] > 52000 - 2000 * std::max(0, 8 - wet))
				looks[i] = LUSH_GRASS;
		}
	}
	M.finished = composeLooks(s, looks);
	M.landing = dilate(t, M.bridge, 3);
	for (int i = 0; i < n; ++i)
		M.landing[i] = M.landing[i] || trace[i];
	return M;
}

// Places the first `count` tiles of a region in the order `before` ranks them.
template <typename Order>
int fillInOrder(GenerationMap &map, std::vector<CellTile> region, int count, int resourceType,
				Order before)
{
	std::stable_sort(region.begin(), region.end(), before);
	int placed = 0;
	for (int i = 0; i < count && i < int(region.size()); ++i, ++placed)
		map.setResourceByIndex(region[i].x, region[i].y, resourceType, 1);
	return placed;
}

// A cell's free resource habitat in row order, measured in its exit's frame. Starter kits need
// ground for both wheat and wood; treasures need only their specific fruit. No deposit may already
// occupy a candidate.
std::vector<CellTile> resourceTilesOfCell(const GenerationMap &map, const Torus &t,
										  const MazeDesign &d, int cell, const CellFrame &frame,
										  int &back, int resourceType = -1)
{
	std::vector<CellTile> tiles;
	back = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if (d.labels[i] != cell || map.isResource(x, y))
			continue;
		const bool habitat = resourceType >= 0
								 ? map.terrainSupportsResourceAtByIndex(x, y, resourceType)
								 : map.terrainSupportsResourceAtByIndex(x, y, WHEAT) &&
									   map.terrainSupportsResourceAtByIndex(x, y, WOOD);
		if (!habitat)
			continue;
		tiles.push_back(frame.measure(t, x, y));
		back = std::max(back, -tiles.back().u);
	}
	return tiles;
}

// Steps from water or its shoreline, excluding the road down the middle of a passage.
std::vector<int> shoreDepth(const GenerationMap &map, const std::vector<unsigned char> &onRoad)
{
	const Torus t(map);
	std::vector<unsigned char> shore(size_t(t.size()), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const auto &terrain = map.terrainPropertiesAt(i);
		shore[i] = (terrain.shoreline || terrain.swimmable) && !onRoad[i];
	}
	return stepsFrom(t, shore);
}

// A home's fields, seen from inside looking out through its door: wheat banks the moat's left inner
// bank and wood its right, both growing forward from the back; only tiles within kShoreBand of the
// moat's shore take anything, and a square round the swarm stays clear.
struct HomeKit
{
	int wheat = 0, wood = 0;
};
// With `place` false nothing is planted and the result is the room each side has: every home then
// plants the least room any home has (at most kHomeFarmland), so all homes start alike even where
// the cells are not.
HomeKit furnishHome(GenerationMap &map, const Torus &t, const MazeDesign &d,
					const MazeTerrain &M, const std::vector<int> &depth, int cell, int count,
					bool place)
{
	const CellFrame frame = frameFor(d.layout.g, cell, d.exitEdge[cell]);
	int back;
	const std::vector<CellTile> grass = resourceTilesOfCell(map, t, d, cell, frame, back);
	std::vector<CellTile> left, right;
	for (const CellTile &c : grass)
	{
		const int i = t.at(c.x, c.y);
		const int reach = depth[i];
		if (reach < 1 || reach > kShoreBand + kKitSpill || M.throat[i] || M.landing[i] ||
			map.getBuilding(c.x, c.y) != NOGBID ||
			map.getGroundUnit(c.x, c.y) != NOGUID)
			continue;
		// A 9x9 square round the swarm stays clear: its 4x4 footprint plus the ring its first
		// workers step out into.
		if (std::abs(c.u) <= 4 * kFrame && std::abs(c.v) <= 4 * kFrame)
			continue;
		if (c.v < 0)
			left.push_back(c);
		else if (c.v > 0)
			right.push_back(c);
	}
	// The bank first, from the back forward; a squeezed (warped) keep spills a little inland.
	const auto shoreFromBack = [&](const CellTile &a, const CellTile &b)
	{
		const bool aInland = depth[t.at(a.x, a.y)] > kShoreBand;
		const bool bInland = depth[t.at(b.x, b.y)] > kShoreBand;
		if (aInland != bInland)
			return bInland;
		return a.u != b.u ? a.u < b.u : std::abs(a.v) > std::abs(b.v);
	};
	HomeKit kit;
	if (!place)
		return {int(left.size()), int(right.size())};
	kit.wheat = fillInOrder(map, left, count, WHEAT, shoreFromBack);
	kit.wood = fillInOrder(map, right, count, WOOD, shoreFromBack);
	return kit;
}

// A treasure: a compact patch of one fruit at the far end of a dead end, so colonies have to go and
// take it rather than passing it on the way somewhere else.
void placeTreasure(GenerationMap &map, const Torus &t, const MazeDesign &d, int cell, int size,
				   int fruitType)
{
	const CellFrame frame = frameFor(d.layout.g, cell, d.exitEdge[cell]);
	int back;
	std::vector<CellTile> tiles = resourceTilesOfCell(map, t, d, cell, frame, back, fruitType);
	const int anchor = -back + 5 * kFrame;
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

// Deposits scattered over the growing ground outside the homes - loam beds and the banks of every
// water - as compact clumps. Densities are per 256 such tiles; fruitTiles of fruit, when the dead
// ends hold no treasure, follow in small clumps of each kind in turn.
void scatterThroughMaze(GenerationMap &map, GenerationContext &context, const MazeDesign &d,
						const MazeTerrain &M, const MazeOptions &o, int fruitTiles)
{
	const int w = map.getW(), h = map.getH();
	const std::vector<int> depth = shoreDepth(map, M.onRoad);
	std::vector<unsigned char> free(size_t(w) * h, 0);
	std::vector<int> pool;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t i = size_t(y) * w + x;
			const auto &terrain = map.terrainPropertiesAt(int(i));
			const bool growing = (depth[i] >= 1 && depth[i] <= kShoreBand) ||
								 (terrain.fertilitySource && !terrain.swimmable);
			if (growing && !d.isHome[d.labels[i]] && !M.onRoad[i] && !M.throat[i] &&
				!M.floor[i] && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID &&
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
			if (free[candidate] &&
				map.terrainSupportsResourceAtByIndex(powerOfTwoRemainder(candidate, w), candidate / w,
													 resourceType))
				seed = candidate;
		}
		if (seed < 0)
			return 0;
		std::vector<int> frontier{seed};
		free[seed] = 0;
		int placed = 0;
		for (size_t head = 0; head < frontier.size() && placed < size; ++head, ++placed)
		{
			const int x = powerOfTwoRemainder(frontier[head], w), y = frontier[head] / w;
			map.setResourceByIndex(x, y, resourceType, 1);
			for (const auto &s : kCardinalSteps)
			{
				const size_t next = size_t(map.normalizeY(y + s[1])) * w + map.normalizeX(x + s[0]);
				if (free[next] && map.terrainSupportsResourceAtByIndex(powerOfTwoRemainder(next, w),
																	   next / w, resourceType))
				{
					free[next] = 0;
					frontier.push_back(int(next));
				}
			}
		}
		for (size_t k = placed; k < frontier.size(); ++k)
			free[frontier[k]] = 1;
		return placed;
	};

	const int ground = int(pool.size());
	for (const auto &layer : {std::pair<int, int>{WHEAT, int(scaledCount(kShoreWheat, o.wheat))},
							  std::pair<int, int>{WOOD, int(scaledCount(kShoreWood, o.wood))},
							  std::pair<int, int>{STONE, int(scaledCount(kShoreStone, o.stone))}})
	{
		int remaining = ground * layer.second / 256;
		for (int attempt = 0; remaining > 0 && attempt < ground; ++attempt)
			remaining -=
				clump(layer.first, std::min(remaining, 4 + int(context.bounded("resources", 9))));
	}
	for (int attempt = 0, kind = 0; fruitTiles > 0 && attempt < ground;
		 ++attempt, kind = (kind + 1) % 3)
		fruitTiles -=
			clump(CHERRY + kind, std::min(fruitTiles, 3 + int(context.bounded("resources", 3))));
}

// Algae is seeded over every water: the moats, fountains and garden pools all have sand banks, so
// it regrows, and algae on water never blocks anyone on foot.
void seedAlgae(GenerationMap &map, GenerationContext &context, int algae)
{
	const int w = map.getW(), h = map.getH();
	std::vector<MapGeneratorPoint> water;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (map.terrainSupportsResourceAtByIndex(x, y, ALGA))
				water.emplace_back(x, y);
	if (water.empty())
		return;
	int remaining = int(water.size()) * algae / 400;
	for (int attempt = 0; remaining > 0 && attempt < int(water.size()) / 4 + 16; ++attempt)
		remaining -= placeResourceClump(map, context,
										water[context.bounded("resources", water.size())], ALGA, 1);
}

bool generate(GenerationWorld &game, GenerationContext &context)
{
	context.stage = "maze layout";
	const MazeOptions o(context.request);
	GenerationMap &map = game.map;
	const int teams = context.request.nbTeams;
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
	const MazeTerrain M = sketchTerrain(d, o, t);
	writeVertices(map, M.finished);

	const auto chamber = [&](int team)
	{
		std::vector<unsigned char> home(size_t(t.size()), 0);
		for (int i = 0; i < t.size(); ++i)
			home[i] = d.labels[i] == homes[team] &&
					  map.terrainPropertiesAt(t.remainderX(i), i / t.w).walkable;
		return home;
	};
	// placeSettlement measures from the footprint's top-left tile; this centres the 4x4 swarm.
	const auto centre = [&](int team)
	{ return MapGeneratorPoint(g.centreTileX(homes[team]) - 2, g.centreTileY(homes[team]) - 2); };
	if (!settleColonies(game, context, "starts", chamber, centre))
		return false;

	context.stage = "maze resources";
	const std::vector<int> depth = shoreDepth(map, M.onRoad);
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	int least = -1, most = 0, faces = 0;
	// The quarry first, stone at the foot of the back wall behind the moat's open ends, so the
	// fields growing forward from the back leave its verge alone.
	for (int home : homes)
	{
		const CellFrame frame = frameFor(g, home, d.exitEdge[home]);
		const int back = cellBack(t, d, home, frame);
		const double reach = double(back) / kFrame - wallRadius(o) - 1;
		const int qx = int(std::lround(frame.cx - frame.fx * reach));
		const int qy = int(std::lround(frame.cy - frame.fy * reach));
		// A wall drawn on vertices leans away from open ground, so the tile beside it is a mixed one
		// and few pure tiles touch the cliff: the face stands on the verge within a few tiles of it.
		const auto quarryGround = [&](int i)
		{
			return d.labels[i] == home && !reserved[i] && !M.throat[i] && !M.landing[i] &&
				   clearGround(map, t.remainderX(i), i / t.w);
		};
		int placed = growPatchesNear(map, t, qx, qy, 2 * o.cellSize, STONE, kHomeStone,
									 [&](int i) { return M.wallFoot[i] && quarryGround(i); })
						 .tiles;
		// A warped corner can leave the wall's foot too little pure ground: the rest of the quarry
		// then stands on clear ground nearest the back.
		if (placed < kHomeStone)
			placed += growPatchesNear(map, t, qx, qy, 2 * o.cellSize, STONE, kHomeStone - placed,
									  quarryGround)
						  .tiles;
		faces += placed;
	}
	int kitTiles = kHomeFarmland;
	for (int home : homes)
	{
		const HomeKit room = furnishHome(map, t, d, M, depth, home, 0, false);
		kitTiles = std::min({kitTiles, room.wheat, room.wood});
	}
	for (int home : homes)
	{
		const HomeKit kit = furnishHome(map, t, d, M, depth, home, kitTiles, true);
		least = least < 0 ? std::min(kit.wheat, kit.wood) : std::min({least, kit.wheat, kit.wood});
		most = std::max({most, kit.wheat, kit.wood});
	}
	context.telemetry.measure("maze.home.farmland.min", least);
	context.telemetry.measure("maze.home.farmland.max", most);
	context.telemetry.measure("maze.quarry-faces.tiles", faces);

	// Rubble heaped beside every breach, on both sides, never in its gap.
	int rubble = 0;
	const int rubbleTiles = int(scaledCount(kRubbleStone, o.stone));
	for (int edge = 0; edge < int(g.edges.size()) && rubbleTiles > 0; ++edge)
		if (d.breach[edge])
		{
			const SubtilePoint mid = edgeMiddle(g, edge);
			const int mx = int(subtileTile(mid.x)), my = int(subtileTile(mid.y));
			const int reach = kBreachHalf + wallRadius(o) + 4;
			for (int side = 0; side < 2; ++side)
			{
				const int cell = g.edges[edge].cells[side];
				const SubtilePoint c = side == 0 ? g.cells[cell].centre
												 : g.centreAcross(edge, g.edges[edge].cells[0]);
				const double dx = double(c.x - mid.x), dy = double(c.y - mid.y);
				const double length = std::max(1.0, std::hypot(dx, dy));
				const double step = wallRadius(o) + 2.0;
				const int sx = int(std::lround(mx + dx / length * step));
				const int sy = int(std::lround(my + dy / length * step));
				rubble += plantQuarryFaces(map, t, {KitSeed{sx, sy, reach}}, rubbleTiles,
										   [&](int i)
										   {
											   return d.labels[i] == cell && !M.floor[i] &&
													  !M.throat[i] && !M.onRoad[i] &&
													  t.dist2(t.remainderX(i), i / t.w, mx, my) <=
														  reach * reach &&
													  clearGround(map, t.remainderX(i), i / t.w);
										   });
			}
		}
	context.telemetry.measure("maze.rubble.tiles", rubble);

	// Fruit is treasure: one patch at the far end of every dead end, with fruit types dealt
	// round-robin so every kind is somewhere in the maze and a colony has to go and fight for the
	// ones it lacks. Without treasure the same fruit is scattered over the growing ground instead.
	std::vector<int> deadEnds = d.deadEnds;
	const int fruit = int(scaledCount(kTreasureFruit, o.fruit));
	if (fruit > 0 && o.treasure)
	{
		context.shuffle(deadEnds.begin(), deadEnds.end(), "resources");
		const int firstType = context.bounded("resources", 3);
		for (size_t i = 0; i < deadEnds.size(); ++i)
			placeTreasure(map, t, d, deadEnds[i], fruit, CHERRY + int((firstType + i) % 3));
	}
	scatterThroughMaze(map, context, d, M, o, o.treasure ? 0 : int(deadEnds.size()) * fruit);
	seedAlgae(map, context, int(scaledCount(kAlgae, o.algae)));
	return true;
}

// Which cells may share ground: two cells round a corner are joined when the open edges at that
// corner connect them, which includes any two cells across an open edge or breach.
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

// The maze's defining guarantees, checked on the finished world rather than trusted:
// - every colony can walk to every other one;
// - the terrain is the design's, vertex for vertex (walls, moats, bridges, roads, breaches);
// - the walls hold even for swimmers: no ground or water a colony can reach joins two cells except
//   through the passages and breaches the maze opened (deposits count as cleared, since they can be);
// - every home has the same fields and its whole quarry, and every breach's gap is clear;
// - every home's nearest garden is the same number of cells away, or one more.
std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	const GenerationMap &map = game.map;
	const ColonyWalk walk =
		walkFromFirstColony(map, context.request.nbTeams, "the maze", "through the maze");
	if (!walk.error.empty())
		return walk.error;
	GenerationContext replay(context.request);
	const MazeDesign d = designMaze(context.request, replay);
	if (!d.failure.empty())
		return "The maze could not be rebuilt: " + d.failure;
	const MazeOptions o(context.request);
	const Torus t(map);
	const MazeTerrain M = sketchTerrain(d, o, t);
	for (int i = 0; i < t.size(); ++i)
		if (map.vertexTerrainAt(t.remainderX(i), i / t.w) != M.finished[i])
			return "The maze's terrain differs from its design at (" +
				   std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) + ").";

	std::vector<unsigned char> reached(size_t(t.size()), 0);
	{
		std::vector<int> queue;
		for (int tile : walk.workers.front())
			if (!reached[tile])
			{
				reached[tile] = 1;
				queue.push_back(tile);
			}
		for (size_t head = 0; head < queue.size(); ++head)
		{
			const int x = t.remainderX(queue[head]), y = queue[head] / t.w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int next = t.at(x + dx, y + dy);
					if (reached[next])
						continue;
					const TerrainProperties &p = map.terrainPropertiesAt(next);
					if (p.walkable || p.swimmable)
					{
						reached[next] = 1;
						queue.push_back(next);
					}
				}
		}
	}
	const std::vector<unsigned char> joined = joinedCells(d);
	const int n = d.layout.g.cellCount();
	const RegionLeak leak = firstRegionLeak(t, reached, d.labels, [&](int a, int b)
											{ return joined[size_t(a) * n + b] != 0; });
	if (leak.tile >= 0)
		return "A maze wall leaks at (" + std::to_string(t.remainderX(leak.tile)) + ", " +
			   std::to_string(leak.tile / t.w) + ").";
	if (int bad = wallTerrainMismatch(map, t, M.wallVertex, TerrainGroup::Obstacles); bad >= 0)
		return "A maze wall vertex is not cliff at (" + std::to_string(t.remainderX(bad)) + ", " +
			   std::to_string(bad / t.w) + ").";

	std::vector<int> wheat(n, 0), wood(n, 0), stone(n, 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if (M.floor[i] && map.isResource(x, y))
			return "A breach is blocked at (" + std::to_string(x) + ", " + std::to_string(y) + ").";
		if (!d.isHome[d.labels[i]])
			continue;
		const int type = map.getResource(x, y).type;
		wheat[d.labels[i]] += type == WHEAT;
		wood[d.labels[i]] += type == WOOD;
		stone[d.labels[i]] += type == STONE;
	}
	const int first = d.layout.homes.front();
	for (int home : d.layout.homes)
	{
		if (wheat[home] != wheat[first] || wood[home] != wood[first])
			return "The homes' fields differ: " + std::to_string(wheat[home]) + " wheat and " +
				   std::to_string(wood[home]) + " wood against " + std::to_string(wheat[first]) +
				   " and " + std::to_string(wood[first]) + ".";
		if (stone[home] < kHomeStone)
			return "A home's quarry has only " + std::to_string(stone[home]) + " stone.";
	}
	int least = -1, most = -1;
	for (int s : d.gardenSteps)
		if (s >= 0)
		{
			least = least < 0 ? s : std::min(least, s);
			most = std::max(most, s);
		}
	if (o.gardens > 0 && (least < 0 || most - least > 1))
		return "The gardens are not shared fairly: the nearest is " + std::to_string(least) +
			   " cells from one home and " + std::to_string(most) + " from another.";
	return "";
}
} // namespace

MazeOptions::MazeOptions(const GenerationRequest &r)
	: cellShape(r.option("cell-shape")), cellSize(r.option("cell-size")),
	  channelWidth(r.option("channel-width")), wallThickness(r.option("wall-thickness")),
	  loopiness(r.option("loopiness")), warp(r.option("warp")), gardens(r.option("gardens")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount")), pavedRoads(r.option("sand-roads") != 0),
	  treasure(r.option("dead-end-treasure") != 0)
{
	if (cellShape == 2)
	{
		std::vector<int> feasible;
		const auto domain = GenerationRequest::control(r.method, "cell-shape").searchValues();
		for (int shape : domain)
		{
			MazeOptions candidate = *this;
			candidate.cellShape = shape;
			if (mazeLayout(candidate, 1 << r.wDec, 1 << r.hDec, r.nbTeams).failure.empty())
				feasible.push_back(shape);
		}
		// Leave the concrete layout validator to explain an incompatible shared setup.
		cellShape = feasible.empty()
						? domain.front()
						: GenerationContext::choiceFromSeed(r.seed, "maze/cell-shape", feasible);
	}
}

GeneratorDefinition mazeDefinition()
{
	return {
		"maze",
		11,
		"Maze",
		18, // 18: no paved temple courts; homes spring meadow; one paving but the bridge
		false,
		{GeneratorControl::choice("cell-shape", "Cell shape", {"Squares", "Hexagons", "Random"}, 2)
			 .withSearchValues({0, 1}),
		 GeneratorControl{"cell-size",
						  "Cell size",
						  24,
						  48,
						  1,
						  28,
						  ControlGroup::Layout,
						  false,
						  false,
						  {24, 28, 32, 40, 48}}
			 .withSearchValues({28, 32, 40}),
		 // Cells snap to 24, 28, 32, 40 or 48 tiles; the default 28 gives 81 square cells on a 256
		 // map, corridors about as wide as the original maze's at 32 now that walls are thin.
		 // Hexagons sit a quarter as far apart again (kHexPitchPercent).
		 // Warp moves the cells' corners by up to this share of what keeps every cell whole, so
		 // squares and hexagons become irregular polygons; doorways never narrow past the
		 // smallest a passage may be, so at small cells warp is gentler.
		 GeneratorControl{"warp", "Warp", 0, 100, 25, 0, ControlGroup::Layout}.withSearchRange(0,
																							   75),
		 // The temple walls' thickness in tiles; drawn on vertices, they show about half of it as
		 // pure cliff.
		 GeneratorControl{"wall-thickness", "Wall thickness", 3, 7, 2, 5, ControlGroup::Layout}
			 .withSearchValues({3, 5}),
		 // The water of every home's moat (the control kept the original maze's channel id).
		 GeneratorControl{"channel-width", "Moat width", 1, 6, 1, 2, ControlGroup::Layout}
			 .withSearchRange(1, 3),
		 GeneratorControl{"loopiness",
						  "Loopiness",
						  0,
						  50,
						  1,
						  5,
						  ControlGroup::Layout,
						  false,
						  false,
						  {0, 5, 10, 20, 35, 50}}
			 .withSearchValues({5, 10, 20}),
		 // Loopiness is the share of non-home cells that get one extra opening, a rubble breach:
		 // the default 5 breaches a few walls, flanking routes without dissolving the maze.
		 // Gardens are the share of non-home cells that hold a pool and its farmland, at least
		 // one per colony when any; 0 leaves the junction fountains and loam beds alone.
		 GeneratorControl{"gardens", "Gardens", 0, 40, 5, 15, ControlGroup::Layout}
			 .withSearchValues({10, 15, 25}),
		 // Percentages of the ambient scatter, the dead-end treasure, the rubble and the algae
		 // (kShoreWheat and company); every home's kit is unscaled.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount"),
		 // Off, passages have no road down the middle; the keeps' bridges stay. (The id is the
		 // original maze's sand roads.)
		 GeneratorControl::toggle("sand-roads", "Paved roads", true, ControlGroup::Layout)
			 .withSearchValues({1}),
		 // Off, the treasure's fruit is scattered over the growing ground instead.
		 GeneratorControl::toggle("dead-end-treasure", "Treasure in dead ends", true,
								  ControlGroup::Resources)
			 .withSearchValues({0, 1})},
		generate,
		true,
		validate,
		validateWorld,
		{"terrain:arena", "feature:maze", "feature:stone-walls", "style:tight-building",
		 "style:siege"}};
}
