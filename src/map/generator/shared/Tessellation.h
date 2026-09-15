// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Drawing.h"
#include "Geometry.h"
#include "Grid.h"
#include "Topology.h"
#include <string>
#include <vector>
struct GenerationContext;
namespace MapGeneration
{
/// A polygon tiling of the torus: cells, the corners they share and the edges between them, with
/// no terrain attached. Positions are exact fixed-point (Drawing.h's SubtilePoint), so a tiling
/// labels the same tiles on every platform. The lattice wraps exactly, so an edge across the seam
/// is an edge like any other, and every cell lists its corners and edges in the same turning order.
///
/// Edges are identities, not pairs of cells: on a torus two cells wide a cell meets the same
/// neighbour across two different edges, and each is its own wall or opening.
struct Tessellation
{
	enum class Shape
	{
		Square = 0,
		Hexagon = 1
	};
	struct Cell
	{
		int column, row;
		SubtilePoint centre;
		/// Clockwise on the map (y grows downwards); edges[k] joins corners[k] and corners[k + 1].
		std::vector<int> corners, edges;
		/// For each corner, the whole map sizes (in subtile units) to add to its stored position to
		/// get the copy that belongs to this cell. Fixed by the lattice, so it stays right however
		/// the corners are warped, and exact even on a torus two cells across, where a corner's
		/// copies on either side are equally near.
		std::vector<SubtilePoint> cornerShifts;
	};
	struct Edge
	{
		/// The cell that owns the edge, then the cell across it.
		int cells[2];
		/// The ends, in the owner's clockwise order.
		int corners[2];
	};

	Torus t{0, 0};
	Shape shape = Shape::Square;
	int columns = 0, rows = 0;
	/// Each corner's position. A corner shared across the wrap is stored once; a cell's
	/// cornerShifts place the copy that belongs to it.
	std::vector<SubtilePoint> corners;
	std::vector<Cell> cells;
	std::vector<Edge> edges;

	int cellCount() const { return int(cells.size()); }
	int cellAt(int column, int row) const;
	int other(int edge, int cell) const
	{
		return edges[edge].cells[0] == cell ? edges[edge].cells[1] : edges[edge].cells[0];
	}
	/// The copy of `p`, shifted by whole map sizes, nearest `reference`.
	SubtilePoint imageNear(SubtilePoint p, SubtilePoint reference) const;
	/// A cell's corners as one unwrapped polygon round its centre.
	std::vector<SubtilePoint> outline(int cell) const;
	/// An edge's two ends as they lie on `cell`'s outline, in that cell's clockwise order. `cell`
	/// must be one of the edge's two cells.
	std::pair<SubtilePoint, SubtilePoint> edgeEnds(int edge, int cell) const;
	/// The other cell's centre across `edge`, placed beside `cell` rather than wherever it is
	/// stored.
	SubtilePoint centreAcross(int edge, int cell) const;
	/// The tile holding a cell's centre.
	int centreTileX(int cell) const { return int(subtileTile(cells[cell].centre.x)); }
	int centreTileY(int cell) const { return int(subtileTile(cells[cell].centre.y)); }
	/// Per cell, the cell across each of its edges, in edge order.
	RegionGraph neighbours() const;
	/// Squared distance between two cells' centres, the short way round, in subtile units.
	long long distance2(int a, int b) const;
	/// The cell a lattice symmetry takes `cell` to: mirrored across columns and/or rows, then moved
	/// by whole columns and rows. Any such move maps the tiling onto itself, edges onto edges.
	int transform(int cell, int dColumns, int dRows, bool mirrorColumns, bool mirrorRows) const;
};

/// Square cells, columns = width / cellSize across and rows = height / cellSize down. Boundaries
/// fall on whole tiles, i * width / columns, so when cellSize doesn't divide the map neighbouring
/// cells differ in pitch by at most a tile. Corners sit on tile centres: the tile holding a corner
/// is the tile two walls meet on.
Tessellation squareTessellation(int width, int height, int cellSize);

/// Pointy-topped hexagons in offset rows, odd rows shifted half a cell right: at least `pitch` tiles
/// between neighbouring centres across a row (width / pitch columns, like squareTessellation, and
/// never fewer than two), with an even row count so the offset wraps. The pitches stretch to
/// whatever tiles the map exactly, so a hexagon may be a little taller or squatter than regular.
Tessellation hexTessellation(int width, int height, int pitch);

/// The shortest edge of the tiling, in whole tiles between the tiles its two corners lie in, taking
/// the larger of the two axes (the steps a unit takes along it).
int shortestEdgeSteps(const Tessellation &);

/// Least distance, in tiles rounded down, from any cell's centre to the line through any of its
/// edges.
int centreClearance(const Tessellation &);

/// How far warpCorners may move a corner along each axis, in subtile units, so no cell can fold
/// over or lose its centre however the moves combine: a third of the smallest distance from a
/// corner to an edge of one of its cells that it isn't on, over the square root of two.
int warpLimit(const Tessellation &);

/// Moves every corner by up to `reach` subtile units on each axis (never past warpLimit), drawn from
/// `stream`, so the cells become irregular polygons that still tile the torus.
///
/// The warp keeps room for passages between the obstacles a design builds on the tiling: every edge
/// marked in `walls`, and every corner no wall reaches (where a design leaves a pond or post). A move
/// is taken only while every two obstacles that share no corner stay at least min(their unwarped gap,
/// minimumGap) steps apart, tile to tile, and every cell's centre stays at least min(unwarped,
/// minimumClearance) tiles from the lines of its edges; otherwise it is halved, down to no move at
/// all. Every corner costs exactly two draws whatever happens, and a reach of 0 draws nothing.
void warpCorners(Tessellation &, int reach, const std::vector<unsigned char> &walls, int minimumGap,
				 int minimumClearance, GenerationContext &, const std::string &stream);

/// Rasterize selected edges as sealed eight-neighbour barriers, then dilate by
/// `radius` tiles with the shared toroidal morphology operation. `walls` has one
/// flag per edge. Radius 0 retains the sealed centreline; no terrain is attached.
std::vector<unsigned char> rasterizeBoundaries(const Tessellation &,
	const std::vector<unsigned char> &walls, int radius = 0);

/// Contract corner offsets toward `referenceCorners` until the rasterized thick
/// boundaries avoid every nonzero tile in `excluded`. The reference must use the
/// same lattice topology and unwrapped corner coordinate convention. This tests
/// arbitrary protected shapes (farms, building reservations, channels), rather
/// than estimating their clearance with a circle around the cell centre.
///
/// Safe input is unchanged. Each attempt halves all offsets with integer arithmetic;
/// the last allowed attempt snaps to the reference, so work is bounded and consumes
/// no RNG. Returns the number of contractions, or -1 if even the reference collides
/// (leaving its corners installed). maxContractions must be positive. This guarantees
/// mask separation, not polygon validity or any other geometric constraint: callers
/// must supply a suitable reference and validate their finished design as usual.
int relaxWarpOutside(Tessellation &, const std::vector<SubtilePoint> &referenceCorners,
	const std::vector<unsigned char> &walls, const std::vector<unsigned char> &excluded,
	int radius, int maxContractions = 16);

/// Each tile's cell: the cell whose outline contains the tile's centre (forEachTileInPolygon's rule),
/// so every tile belongs to exactly one cell. Empty if some tile was left out or taken twice, which a
/// valid tiling never does.
std::vector<int> labelTiles(const Tessellation &);
/// A subtile point (sixteenths of a tile) in tile units: where a cell's centre falls on the map.
inline ShapePoint tilePoint(SubtilePoint p)
{
	return {p.x / 16.0, p.y / 16.0};
}
} // namespace MapGeneration
