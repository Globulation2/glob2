// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Drawing.h"
#include "Points.h"
#include "Tessellation.h"
#include <array>
#include <functional>
#include <random>
#include <string>
#include <vector>
struct GenerationContext;
namespace MapGeneration
{
// A maze carved through a graph of cells: which edges are open passages, which stay walls. Every
// function here works on the cell graph alone, so the same maze logic runs on squares, hexagons or
// warped cells (a Tessellation), or on any irregular cells a design drew: Voronoi cells round
// scattered sites (Points.h), chambers joined by tunnels, plazas joined by streets. Some cells can be set aside as pockets - colonies' homes - that the
// tree never passes through, each opened later by a single door so it is a cul-de-sac. `open` holds
// one flag per edge.

/// The graph the maze functions work on: each cell's edges in a fixed order, each edge's two cells
/// (its owner first), and the squared distance between two cells' centres. Edges are identities, not
/// pairs, so two cells may share more than one.
struct CellGraph
{
	std::vector<std::vector<int>> cellEdges;
	std::vector<std::array<int, 2>> edgeCells;
	std::function<long long(int, int)> distance2;

	int cellCount() const { return int(cellEdges.size()); }
	int other(int edge, int cell) const
	{
		return edgeCells[edge][0] == cell ? edgeCells[edge][1] : edgeCells[edge][0];
	}
};

/// A tessellation's cells as a graph, edges numbered as the tessellation numbers them. The graph
/// refers to the tessellation for distances, so it must not outlive it.
CellGraph cellGraph(const Tessellation &);

/// Scattered sites' cells as a graph: one edge per neighbouring pair (neighbours as siteNeighbours
/// lists them), owned by the lower site, numbered in order of the lower then the higher site, and
/// distances the short way round the torus in tiles squared.
CellGraph cellGraph(const Torus &, const std::vector<Site> &sites,
					const std::vector<std::vector<int>> &neighbours);

/// Whether a set of pocket cells leaves a layout a maze can be carved in: every pocket borders at
/// least one other cell to put its door in, and the other cells are all joined together.
bool pocketsFit(const CellGraph &, const std::vector<unsigned char> &pocket);

/// `count` pocket cells spread as far apart as the torus allows, as a pattern fixed by the tiling
/// alone, so a validator can check exactly what generation will use: farthest-point spreading
/// between cell centres from cell 0, taking at each step the farthest cell that still leaves
/// pocketsFit (the lowest index on a tie). Empty if the tiling can't hold them.
std::vector<int> spreadPockets(const CellGraph &, int count);

/// Closed edges with neither endpoint blocked, in ascending edge-ID order. An empty
/// blocked mask excludes no cells. Parallel edges retain their separate identities.
/// This supplies a stable candidate list before a caller shuffles or ranks shortcuts.
std::vector<int> closedEdges(const CellGraph &, const std::vector<unsigned char> &open,
							 const std::vector<unsigned char> &blocked = {});

/// For each requested edge, the distance between its endpoints using only open edges:
/// the detour a shortcut would avoid, measured in graph edges, not tiles or travel time.
/// The result is indexed by edge ID; unrequested or unreachable edges have value -1.
/// Distances use the original open graph for every candidate, without adding shortcuts
/// as they are measured. No RNG is consumed; stable sorting can preserve a seeded tie order.
std::vector<int> edgeDetours(const CellGraph &, const std::vector<unsigned char> &open,
							 const std::vector<int> &edges);

/// Opens a spanning tree of the cells not `blocked` with a recursive backtracker from a random
/// start: long winding passages with comparatively few, deep dead ends. Returns whether every such
/// cell was reached.
bool carveSpanningTree(const CellGraph &, GenerationContext &, const std::string &stream,
					   const std::vector<unsigned char> &blocked, std::vector<unsigned char> &open);

/// Opens a spanning tree of the cells not `blocked` that joins near neighbours first: Kruskal's
/// algorithm over the edges between them in order of their cells' distance, each distance stretched
/// by up to `jitterPercent` of itself by a draw from `stream`, so the tree changes from seed to
/// seed while still preferring short links. Where carveSpanningTree winds long passages through a
/// maze, this makes the network a traveller would: eskers between drumlins, bridges between
/// islands, roads between towns, each to the next one over. Returns whether every such cell was
/// reached.
bool carveNearTree(const CellGraph &, GenerationContext &, const std::string &stream,
				   const std::vector<unsigned char> &blocked, int jitterPercent,
				   std::vector<unsigned char> &open);

/// Opens one door into each pocket, through a random edge onto a cell that isn't a pocket, and
/// returns each pocket's door edge in the order given.
std::vector<int> openPocketDoors(const CellGraph &, GenerationContext &, const std::string &stream,
								 const std::vector<int> &pockets,
								 const std::vector<unsigned char> &isPocket,
								 std::vector<unsigned char> &open);

/// Opens extra walls between cells that aren't `blocked`: `percent` of those cells' count, chosen
/// at random from the closed edges between them, giving second routes to flank along.
void openLoops(const CellGraph &, GenerationContext &, const std::string &stream,
			   const std::vector<unsigned char> &blocked, int percent,
			   std::vector<unsigned char> &open);

/// A wandering corridor (Drawing.h's carveCorridor) along every open edge of a graph, from the one
/// cell's point to the other's, carved into `mask` on the tiles `eligible(tile)` allows: tunnels
/// between chambers, eskers over the water between drumlins (eligible: water only, so the corridor
/// is nothing across the land it joins). Corridors are carved in edge order, each drawing from
/// `random` as wanderingPath does. Returns each carved edge's two cells, in that order.
template <typename Eligible>
std::vector<std::array<int, 2>>
carveOpenEdges(std::vector<unsigned char> &mask, const Torus &t, const CellGraph &g,
			   const std::vector<ShapePoint> &points, const std::vector<unsigned char> &open,
			   double halfWidth, double wander, double widthJitter, std::mt19937 &random,
			   Eligible eligible)
{
	std::vector<std::array<int, 2>> carved;
	std::vector<unsigned char> line(mask.size(), 0);
	for (size_t edge = 0; edge < open.size(); ++edge)
	{
		if (!open[edge])
			continue;
		const int a = g.edgeCells[edge][0], b = g.edgeCells[edge][1];
		const std::vector<StrokePoint> path =
			wanderingPath(t, points[a], points[b], halfWidth, wander, widthJitter, random);
		strokePath(line, t, path);
		// Only the stroke's bounding circle need be looked at, and cleared for the next edge.
		const PathBounds bounds = pathBounds(path);
		const int reach = int(std::ceil(bounds.radius)) + 1;
		const int x0 = int(std::lround(bounds.x)), y0 = int(std::lround(bounds.y));
		for (int y = y0 - reach; y <= y0 + reach; ++y)
			for (int x = x0 - reach; x <= x0 + reach; ++x)
			{
				const int i = t.at(x, y);
				if (line[i] && eligible(i))
					mask[i] = 1;
				line[i] = 0;
			}
		carved.push_back({a, b});
	}
	return carved;
}

/// The cells, other than `blocked` ones, with exactly one open edge, and that edge for each (by
/// cell index; -1 elsewhere).
std::vector<int> deadEnds(const CellGraph &, const std::vector<unsigned char> &open,
						  const std::vector<unsigned char> &blocked, std::vector<int> &exitEdge);

/// The same functions on a tessellation's own graph (cellGraph).
bool pocketsFit(const Tessellation &, const std::vector<unsigned char> &pocket);
std::vector<int> spreadPockets(const Tessellation &, int count);
bool carveSpanningTree(const Tessellation &, GenerationContext &, const std::string &stream,
					   const std::vector<unsigned char> &blocked, std::vector<unsigned char> &open);
std::vector<int> openPocketDoors(const Tessellation &, GenerationContext &,
								 const std::string &stream, const std::vector<int> &pockets,
								 const std::vector<unsigned char> &isPocket,
								 std::vector<unsigned char> &open);
void openLoops(const Tessellation &, GenerationContext &, const std::string &stream,
			   const std::vector<unsigned char> &blocked, int percent,
			   std::vector<unsigned char> &open);
std::vector<int> deadEnds(const Tessellation &, const std::vector<unsigned char> &open,
						  const std::vector<unsigned char> &blocked, std::vector<int> &exitEdge);
} // namespace MapGeneration
