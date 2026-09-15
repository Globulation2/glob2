// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Farmland.h"
#include "Grid.h"
#include "Sketch.h"
#include <vector>
namespace MapGeneration
{
// Lanes and lots: the torus cut into blocks by a grid of one-tile lanes, each block holding one
// sand-ringed grass pad (a lot) that a building fits on. A sand lane can never grow shut and never be
// built on (Map::incResource seeds a crop only on its own terrain; buildings need pure grass), so the
// lanes are the ways that stay open whatever a player builds, and the pads are the only room, so
// building is a discrete choice - take a lot, fill it - that reads at a glance.
//
// The lanes wrap the torus exactly: with `pitch` tiles asked for, the map is cut into
// round(side / pitch) blocks along each axis, the lane positions spread by whole tiles, so blocks are
// `pitch` or a tile more or less wide (Maze's cells differ the same way). Vertices, not tiles: a
// lane vertex is one sand corner, which spoils the tile on each side of it for building
// (GAME_RULES_FOR_MAP_DESIGN.md), so a block `p` tiles across has `p - 3` tiles of pure grass.

struct LaneGrid
{
	Torus t{1, 1};
	std::vector<int> laneX, laneY; // the x of every vertical lane and the y of every horizontal one
	int columns() const { return int(laneX.size()); }
	int rows() const { return int(laneY.size()); }
	/// The block a tile (x, y) lies in, by the lane on its left/top: column index and row index.
	int column(int x) const;
	int row(int y) const;
	/// A block's ground between its lanes, as the first tile past the left/top lane and the count
	/// of tiles up to (not including) the right/bottom lane.
	struct Span
	{
		int start, count;
	};
	Span columnSpan(int column) const;
	Span rowSpan(int row) const;
};

/// Lanes about `pitch` tiles apart across a torus, the first at (x0, y0). A pitch under 4 or wider
/// than a side gives one lane per axis.
LaneGrid layLanes(const Torus &, int pitch, int x0, int y0);

/// Every tile a lane runs through (the lane's own tiles, one wide), as a mask.
std::vector<unsigned char> laneTiles(const LaneGrid &);

/// A pad stamped in the middle of a block: `size` by `size` tiles of pure grass in a one-vertex
/// ring of sand (Canals' homestead pad), centred in the block; the pad's grass tiles are marked in
/// `pads.plot` and its sand in `pads.sand` (a Farm used as the plot register, as Canals uses one).
/// The pad takes the first of `sizes` (tried in order) that fits: a `size` pad is `size` + 1 grass
/// vertices with a ring vertex either side, and a ring vertex may stand on the lane's own vertex,
/// so a block of `p` tiles (p - 1 between its lanes) holds a pad up to p - 2: a 6 in a block of 8,
/// a 10 in a block of 12. `shrinkBy` vertices are kept back on every side besides, for a ditch's
/// beach. Returns the size stamped, 0 when none fit, and the pad's top-left tile in `x0`/`y0`.
int stampLotPad(TerrainSketch &, const LaneGrid &, Farm &pads, int column, int row,
				const std::vector<int> &sizes, int &x0, int &y0, int shrinkBy = 0);
} // namespace MapGeneration
