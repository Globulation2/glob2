// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Tessellation.h"
#include <string>
#include <vector>
struct GenerationContext;
namespace MapGeneration
{
// A maze carved through a Tessellation's cells: which edges are open passages, which stay walls.
// Every function here works on the cell graph alone, so the same maze logic runs on squares,
// hexagons or warped cells. Some cells can be set aside as pockets - colonies' homes - that the
// tree never passes through, each opened later by a single door so it is a cul-de-sac. `open` holds
// one flag per edge.

/// Whether a set of pocket cells leaves a layout a maze can be carved in: every pocket borders at
/// least one other cell to put its door in, and the other cells are all joined together.
bool pocketsFit(const Tessellation &, const std::vector<unsigned char> &pocket);

/// `count` pocket cells spread as far apart as the torus allows, as a pattern fixed by the tiling
/// alone, so a validator can check exactly what generation will use: farthest-point spreading
/// between cell centres from cell 0, taking at each step the farthest cell that still leaves
/// pocketsFit (the lowest index on a tie). Empty if the tiling can't hold them.
std::vector<int> spreadPockets(const Tessellation &, int count);

/// Opens a spanning tree of the cells not `blocked` with a recursive backtracker from a random
/// start: long winding passages with comparatively few, deep dead ends. Returns whether every such
/// cell was reached.
bool carveSpanningTree(const Tessellation &, GenerationContext &, const std::string &stream,
					   const std::vector<unsigned char> &blocked, std::vector<unsigned char> &open);

/// Opens one door into each pocket, through a random edge onto a cell that isn't a pocket, and
/// returns each pocket's door edge in the order given.
std::vector<int> openPocketDoors(const Tessellation &, GenerationContext &, const std::string &stream,
								 const std::vector<int> &pockets,
								 const std::vector<unsigned char> &isPocket,
								 std::vector<unsigned char> &open);

/// Opens extra walls between cells that aren't `blocked`: `percent` of those cells' count, chosen
/// at random from the closed edges between them, giving second routes to flank along.
void openLoops(const Tessellation &, GenerationContext &, const std::string &stream,
			   const std::vector<unsigned char> &blocked, int percent,
			   std::vector<unsigned char> &open);

/// The cells, other than `blocked` ones, with exactly one open edge, and that edge for each (by
/// cell index; -1 elsewhere).
std::vector<int> deadEnds(const Tessellation &, const std::vector<unsigned char> &open,
						  const std::vector<unsigned char> &blocked, std::vector<int> &exitEdge);
} // namespace MapGeneration
