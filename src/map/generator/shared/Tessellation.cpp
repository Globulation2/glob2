#include "GenerationWork.h"
#include "GenerationNumeric.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Tessellation.h"
#include "GenerationContext.h"
#include "Morphology.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
namespace MapGeneration
{
std::vector<StrokePoint> cellCrossing(const Tessellation &g, int edge, double centreRadius,
									  double halfWidth)
{
	if (edge < 0 || edge >= int(g.edges.size()) || !std::isfinite(centreRadius) ||
		!std::isfinite(halfWidth) || centreRadius < 0 || halfWidth < 0)
		throw std::invalid_argument("Invalid cell crossing geometry");
	const int owner = g.edges.at(edge).cells[0];
	const auto ends = g.edgeEnds(edge, owner);
	const ShapePoint a = tilePoint(ends.first), b = tilePoint(ends.second);
	const ShapePoint midpoint{(a.x + b.x) / 2, (a.y + b.y) / 2};
	const ShapePoint from = tilePoint(g.cells.at(owner).centre);
	const ShapePoint to = tilePoint(g.centreAcross(edge, owner));
	const auto approach = [&](ShapePoint centre)
	{
		const double vx = midpoint.x - centre.x, vy = midpoint.y - centre.y;
		const double scale = centreRadius / ::MapGeneration::Numeric::hypot(vx, vy);
		return ShapePoint{centre.x + vx * scale, centre.y + vy * scale};
	};
	if (::MapGeneration::Numeric::hypot(midpoint.x - from.x, midpoint.y - from.y) <= centreRadius ||
		::MapGeneration::Numeric::hypot(midpoint.x - to.x, midpoint.y - to.y) <= centreRadius)
		return {};
	const ShapePoint entry = approach(from), exit = approach(to);
	return {{entry.x, entry.y, halfWidth},
			{midpoint.x, midpoint.y, halfWidth},
			{exit.x, exit.y, halfWidth}};
}

namespace
{
long long floorDiv(long long a, long long b)
{
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}
int wrapIndex(int v, int n)
{
	return ((v % n) + n) % n;
}
// Distance from `p` to the line through `a` and `b`, in subtile units.
double lineDistance(SubtilePoint p, SubtilePoint a, SubtilePoint b)
{
	const long long ux = b.x - a.x, uy = b.y - a.y;
	const long long cross = ux * (p.y - a.y) - uy * (p.x - a.x);
	const double length = ::MapGeneration::Numeric::sqrt(double(ux * ux + uy * uy));
	return length > 0 ? std::abs(double(cross)) / length : 0;
}
int edgeSteps(SubtilePoint a, SubtilePoint b)
{
	const long long dx = std::llabs(subtileTile(a.x) - subtileTile(b.x));
	const long long dy = std::llabs(subtileTile(a.y) - subtileTile(b.y));
	return int(std::max(dx, dy));
}
} // namespace

int Tessellation::cellAt(int column, int row) const
{
	return wrapIndex(row, rows) * columns + wrapIndex(column, columns);
}

SubtilePoint Tessellation::imageNear(SubtilePoint p, SubtilePoint reference) const
{
	const long long w = (long long)t.w * kSubtile, h = (long long)t.h * kSubtile;
	p.x -= floorDiv(p.x - reference.x + w / 2, w) * w;
	p.y -= floorDiv(p.y - reference.y + h / 2, h) * h;
	return p;
}

std::vector<SubtilePoint> Tessellation::outline(int cell) const
{
	const Cell &c = cells.at(cell);
	std::vector<SubtilePoint> points;
	for (size_t k = 0; k < c.corners.size(); ++k)
	{
		::MapGeneration::generationCheckpoint();
		points.push_back({corners.at(c.corners.at(k)).x + c.cornerShifts.at(k).x,
						  corners.at(c.corners.at(k)).y + c.cornerShifts.at(k).y});
	}
	return points;
}

std::pair<SubtilePoint, SubtilePoint> Tessellation::edgeEnds(int edge, int cell) const
{
	const Cell &c = cells.at(cell);
	const size_t n = c.edges.size();
	const size_t k = size_t(std::find(c.edges.begin(), c.edges.end(), edge) - c.edges.begin());
	const auto at = [&](size_t i) -> SubtilePoint
	{
		return {corners.at(c.corners.at(i)).x + c.cornerShifts.at(i).x,
				corners.at(c.corners.at(i)).y + c.cornerShifts.at(i).y};
	};
	return {at(k % n), at((k + 1) % n)};
}

SubtilePoint Tessellation::centreAcross(int edge, int cell) const
{
	const int next = other(edge, cell);
	// The same edge seen from both sides runs in opposite directions; the difference between its
	// two copies is the shift that carries the neighbour beside this cell.
	const SubtilePoint here = edgeEnds(edge, cell).first, there = edgeEnds(edge, next).second;
	return {cells.at(next).centre.x + here.x - there.x, cells.at(next).centre.y + here.y - there.y};
}

RegionGraph Tessellation::neighbours() const
{
	RegionGraph graph(cells.size());
	for (int cell = 0; cell < cellCount(); ++cell)
	{
		::MapGeneration::generationCheckpoint();
		for (int edge : cells.at(cell).edges)
		{
			::MapGeneration::generationCheckpoint();
			graph.at(cell).push_back(other(edge, cell));
		}
	}
	return graph;
}

long long Tessellation::distance2(int a, int b) const
{
	const SubtilePoint q = imageNear(cells.at(b).centre, cells.at(a).centre);
	const long long dx = q.x - cells.at(a).centre.x, dy = q.y - cells.at(a).centre.y;
	return dx * dx + dy * dy;
}

int Tessellation::transform(int cell, int dColumns, int dRows, bool mirrorColumns,
							bool mirrorRows) const
{
	int c = cells.at(cell).column, r = cells.at(cell).row;
	if (shape == Shape::Square)
	{
		c = mirrorColumns ? (columns - c) % columns : c;
		r = mirrorRows ? (rows - r) % rows : r;
		return ((r + dRows) % rows) * columns + (c + dColumns) % columns;
	}
	// Offset rows: an odd row sits half a cell right, so mirroring across columns takes an odd
	// row's cell c to -c - 1 rather than -c, and moving down an odd number of rows moves the column
	// by half a cell, rounded by the row's parity. The row count is even, so mirroring rows keeps
	// every row's parity.
	if (mirrorColumns)
		c = (r & 1) ? -c - 1 : -c;
	if (mirrorRows)
		r = (rows - r) % rows;
	const int column = c + dColumns + int(floorDiv((r & 1) + dRows, 2));
	return cellAt(column, r + dRows);
}

Tessellation squareTessellation(int width, int height, int cellSize)
{
	Tessellation g;
	g.t = Torus(width, height);
	g.shape = Tessellation::Shape::Square;
	g.columns = std::max(1, width / std::max(1, cellSize));
	g.rows = std::max(1, height / std::max(1, cellSize));
	std::vector<int> xs, ys;
	for (int i = 0; i <= g.columns; ++i)
	{
		::MapGeneration::generationCheckpoint();
		xs.push_back(i * width / g.columns);
	}
	for (int i = 0; i <= g.rows; ++i)
	{
		::MapGeneration::generationCheckpoint();
		ys.push_back(i * height / g.rows);
	}
	// A corner is owned by the cell to its lower right, so corner (c, r) has the cell's index.
	for (int r = 0; r < g.rows; ++r)
	{
		::MapGeneration::generationCheckpoint();
		for (int c = 0; c < g.columns; ++c)
		{
			::MapGeneration::generationCheckpoint();
			g.corners.push_back(subtileCentre(xs.at(c), ys.at(r)));
		}
	}
	for (int r = 0; r < g.rows; ++r)
	{
		::MapGeneration::generationCheckpoint();
		for (int c = 0; c < g.columns; ++c)
		{
			::MapGeneration::generationCheckpoint();
			const int self = g.cellAt(c, r);
			Tessellation::Cell cell;
			cell.column = c;
			cell.row = r;
			cell.centre =
				subtileCentre((xs.at(c) + xs.at(c + 1)) / 2, (ys.at(r) + ys.at(r + 1)) / 2);
			// Clockwise from the top right corner, so the edges run east, south, west, north.
			cell.corners = {g.cellAt(c + 1, r), g.cellAt(c + 1, r + 1), g.cellAt(c, r + 1), self};
			const auto shift = [&](int column, int row) -> SubtilePoint
			{
				return {(long long)(column / g.columns) * width * kSubtile,
						(long long)(row / g.rows) * height * kSubtile};
			};
			cell.cornerShifts = {shift(c + 1, r), shift(c + 1, r + 1), shift(c, r + 1),
								 shift(c, r)};
			cell.edges = {2 * self, 2 * self + 1, 2 * g.cellAt(c - 1, r),
						  2 * g.cellAt(c, r - 1) + 1};
			g.cells.push_back(cell);
		}
	}
	for (int self = 0; self < g.cellCount(); ++self)
	{
		::MapGeneration::generationCheckpoint();
		const auto &cell = g.cells.at(self);
		g.edges.push_back({{self, g.cellAt(cell.column + 1, cell.row)},
						   {cell.corners.at(0), cell.corners.at(1)}});
		g.edges.push_back({{self, g.cellAt(cell.column, cell.row + 1)},
						   {cell.corners.at(1), cell.corners.at(2)}});
	}
	return g;
}

Tessellation hexTessellation(int width, int height, int pitch)
{
	Tessellation g;
	g.t = Torus(width, height);
	g.shape = Tessellation::Shape::Hexagon;
	pitch = std::max(1, pitch);
	g.columns = std::max(2, width / pitch);
	// A regular hexagon's rows are sqrt(3) / 2 of its pitch apart; round to the nearest even count.
	const long long half = (10000LL * height * g.columns + 17320LL * width / 2) / (17320LL * width);
	g.rows = 2 * int(std::max(1LL, half));
	const long long w = (long long)width * kSubtile, h = (long long)height * kSubtile;
	// Positions as exact fractions of the map: x in halves of a column, y in thirds of a row.
	const auto at = [&](long long halfColumns, long long thirdRows) -> SubtilePoint
	{ return {floorDiv(halfColumns * w, 2LL * g.columns), floorDiv(thirdRows * h, 3LL * g.rows)}; };
	// Each cell owns its top corner (index 2 * cell) and its upper right corner (2 * cell + 1).
	for (int r = 0; r < g.rows; ++r)
	{
		::MapGeneration::generationCheckpoint();
		for (int c = 0; c < g.columns; ++c)
		{
			::MapGeneration::generationCheckpoint();
			const int p = r & 1;
			g.corners.push_back(at(2 * c + p, 3 * r - 2));
			g.corners.push_back(at(2 * c + p + 1, 3 * r - 1));
		}
	}
	for (int r = 0; r < g.rows; ++r)
	{
		::MapGeneration::generationCheckpoint();
		for (int c = 0; c < g.columns; ++c)
		{
			::MapGeneration::generationCheckpoint();
			const int p = r & 1, self = g.cellAt(c, r);
			const int east = g.cellAt(c + 1, r), west = g.cellAt(c - 1, r);
			const int northEast = g.cellAt(c + p, r - 1), northWest = g.cellAt(c + p - 1, r - 1);
			const int southEast = g.cellAt(c + p, r + 1), southWest = g.cellAt(c + p - 1, r + 1);
			Tessellation::Cell cell;
			cell.column = c;
			cell.row = r;
			cell.centre = at(2 * c + p, 3 * r);
			// Clockwise from the upper right corner: lower right, bottom, lower left, upper left,
			// top; so the edges run east, south-east, south-west, west, north-west, north-east.
			cell.corners = {2 * self + 1,  2 * southEast, 2 * southWest + 1,
							2 * southWest, 2 * west + 1,  2 * self};
			// Each corner is stored with the cell that owns it; a neighbour past the seam owns the
			// copy one map size away.
			const auto shift = [&](int column, int row) -> SubtilePoint
			{ return {floorDiv(column, g.columns) * w, floorDiv(row, g.rows) * h}; };
			cell.cornerShifts = {shift(c, r),
								 shift(c + p, r + 1),
								 shift(c + p - 1, r + 1),
								 shift(c + p - 1, r + 1),
								 shift(c - 1, r),
								 shift(c, r)};
			cell.edges = {3 * self, 3 * self + 1,      3 * self + 2,
						  3 * west, 3 * northWest + 1, 3 * northEast + 2};
			g.cells.push_back(cell);
			const int owned[3] = {east, southEast, southWest};
			for (int k = 0; k < 3; ++k)
			{
				::MapGeneration::generationCheckpoint();
				g.edges.push_back({{self, owned[k]}, {cell.corners.at(k), cell.corners.at(k + 1)}});
			}
		}
	}
	return g;
}

int shortestEdgeSteps(const Tessellation &g)
{
	int shortest = INT_MAX;
	for (int edge = 0; edge < int(g.edges.size()); ++edge)
	{
		::MapGeneration::generationCheckpoint();
		const auto ends = g.edgeEnds(edge, g.edges.at(edge).cells[0]);
		shortest = std::min(shortest, edgeSteps(ends.first, ends.second));
	}
	return shortest;
}

int centreClearance(const Tessellation &g)
{
	double least = 1e18;
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		::MapGeneration::generationCheckpoint();
		const std::vector<SubtilePoint> points = g.outline(cell);
		for (size_t k = 0; k < points.size(); ++k)
		{
			::MapGeneration::generationCheckpoint();
			least = std::min(least, lineDistance(g.cells.at(cell).centre, points.at(k),
												 points.at((k + 1) % points.size())));
		}
	}
	return int(::MapGeneration::Numeric::floor(least / kSubtile));
}

int warpLimit(const Tessellation &g)
{
	double least = 1e18;
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		::MapGeneration::generationCheckpoint();
		const std::vector<SubtilePoint> points = g.outline(cell);
		const size_t n = points.size();
		for (size_t k = 0; k < n; ++k)
		{
			::MapGeneration::generationCheckpoint();
			for (size_t e = 0; e < n; ++e)
			{
				::MapGeneration::generationCheckpoint();
				if (e != k && (e + 1) % n != k)
					least = std::min(
						least, lineDistance(points.at(k), points.at(e), points.at((e + 1) % n)));
			}
		}
	}
	// Three corners move at once (this one and the far edge's two ends), each by up to sqrt(2) times
	// its reach on one axis.
	return std::max(
		0, int(::MapGeneration::Numeric::floor(least / (3 * ::MapGeneration::Numeric::sqrt(2.0)))) -
			   1);
}

std::vector<unsigned char> rasterizeBoundaries(const Tessellation &g,
	const std::vector<unsigned char> &walls, int radius)
{
	assert(walls.size() == g.edges.size() && radius >= 0);
	std::vector<unsigned char> mask(g.t.size(), 0);
	for (int e = 0; e < int(g.edges.size()); ++e)
	{
		::MapGeneration::generationCheckpoint();
		if (walls.at(e))
		{
			const auto ends = g.edgeEnds(e, g.edges.at(e).cells[0]);
			traceSealedPath(mask, g.t, {ends.first, ends.second});
		}
	}
	return radius ? dilate(g.t, mask, radius) : mask;
}

int relaxWarpOutside(Tessellation &g, const std::vector<SubtilePoint> &referenceCorners,
	const std::vector<unsigned char> &walls, const std::vector<unsigned char> &excluded,
	int radius, int maxContractions)
{
	assert(referenceCorners.size() == g.corners.size());
	assert(excluded.size() == size_t(g.t.size()) && maxContractions > 0);
	// Inspect the exact raster footprint, including thickness and the torus seam.
	// A centre-to-line distance cannot represent an arbitrary reserved tile mask.
	for (int contractions = 0; contractions <= maxContractions; ++contractions)
	{
		::MapGeneration::generationCheckpoint();
		const auto boundary = rasterizeBoundaries(g, walls, radius);
		bool safe = true;
		for (int i = 0; i < g.t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (boundary.at(i) && excluded.at(i))
			{
				safe = false;
				break;
			}
		}
		if (safe)
			return contractions;
		if (contractions == maxContractions)
			return -1;
		// Preserve corner identities and shared edges. Never repair overlap by
		// deleting wall pixels: that would invent routes through the barrier.
		for (size_t k = 0; k < g.corners.size(); ++k)
		{
			::MapGeneration::generationCheckpoint();
			g.corners.at(k).x = referenceCorners.at(k).x +
								(contractions + 1 == maxContractions
									 ? 0
									 : (g.corners.at(k).x - referenceCorners.at(k).x) / 2);
			g.corners.at(k).y = referenceCorners.at(k).y +
								(contractions + 1 == maxContractions
									 ? 0
									 : (g.corners.at(k).y - referenceCorners.at(k).y) / 2);
		}
	}
	return -1;
}

void warpCorners(Tessellation &g, int reach, const std::vector<unsigned char> &walls,
				 int minimumGap, int minimumClearance, GenerationContext &context,
				 const std::string &stream)
{
	reach = std::min(reach, warpLimit(g));
	if (reach <= 0)
		return;
	const Torus &t = g.t;
	std::vector<std::vector<int>> cornerCells(g.corners.size()), cornerWalls(g.corners.size());
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		::MapGeneration::generationCheckpoint();
		for (int corner : g.cells.at(cell).corners)
		{
			::MapGeneration::generationCheckpoint();
			cornerCells.at(corner).push_back(cell);
		}
	}
	for (int edge = 0; edge < int(g.edges.size()); ++edge)
	{
		::MapGeneration::generationCheckpoint();
		if (walls.at(edge))
			for (int corner : g.edges.at(edge).corners)
			{
				::MapGeneration::generationCheckpoint();
				cornerWalls.at(corner).push_back(edge);
			}
	}

	// The obstacles passages run between: every wall, and every corner no wall reaches. Each is kept
	// as the tiles its line (or point) covers, with a bounding box for cheap rejections.
	struct Obstacle
	{
		int edge, corner;
		std::vector<int> corners;
		std::vector<std::pair<int, int>> tiles;
		long long cx, cy, radius;
	};
	std::vector<Obstacle> obstacles;
	std::vector<std::vector<int>> cornerObstacles(g.corners.size());
	for (int edge = 0; edge < int(g.edges.size()); ++edge)
	{
		::MapGeneration::generationCheckpoint();
		if (walls.at(edge))
			obstacles.push_back(
				{edge, -1, {g.edges.at(edge).corners[0], g.edges.at(edge).corners[1]}});
	}
	for (int corner = 0; corner < int(g.corners.size()); ++corner)
	{
		::MapGeneration::generationCheckpoint();
		if (cornerWalls.at(corner).empty())
			obstacles.push_back({-1, corner, {corner}});
	}
	const auto trace = [&](Obstacle &o)
	{
		std::vector<std::pair<long long, long long>> raw;
		if (o.edge >= 0)
		{
			const auto ends = g.edgeEnds(o.edge, g.edges.at(o.edge).cells[0]);
			raw = sealedSegmentTiles(ends.first, ends.second);
		}
		else
			raw = {{subtileTile(g.corners.at(o.corner).x), subtileTile(g.corners.at(o.corner).y)}};
		long long x0 = raw.at(0).first, x1 = x0, y0 = raw.at(0).second, y1 = y0;
		o.tiles.clear();
		for (const auto &tile : raw)
		{
			::MapGeneration::generationCheckpoint();
			x0 = std::min(x0, tile.first);
			x1 = std::max(x1, tile.first);
			y0 = std::min(y0, tile.second);
			y1 = std::max(y1, tile.second);
			o.tiles.push_back({t.x(int(tile.first)), t.y(int(tile.second))});
		}
		o.cx = (x0 + x1) / 2;
		o.cy = (y0 + y1) / 2;
		o.radius = std::max(x1 - x0, y1 - y0) / 2 + 1;
	};
	// Steps between two obstacles, the short way round; anything at least `enough` apart by their
	// boxes alone reports `enough`. A tile of `a` at least `least` from `b`'s box can be no nearer to
	// any of `b`'s tiles, so it is skipped: the result is the same as comparing every pair of tiles.
	const auto gap = [&](const Obstacle &a, const Obstacle &b, int enough)
	{
		const long long apart =
			t.chebyshev(int(a.cx), int(a.cy), int(b.cx), int(b.cy)) - a.radius - b.radius;
		if (apart >= enough)
			return enough;
		int least = enough;
		for (const auto &p : a.tiles)
		{
			::MapGeneration::generationCheckpoint();
			if (t.chebyshev(p.first, p.second, int(b.cx), int(b.cy)) - b.radius >= least)
				continue;
			for (const auto &q : b.tiles)
			{
				::MapGeneration::generationCheckpoint();
				least = std::min(least, t.chebyshev(p.first, p.second, q.first, q.second));
			}
		}
		return least;
	};
	for (Obstacle &o : obstacles)
	{
		::MapGeneration::generationCheckpoint();
		trace(o);
		for (int corner : o.corners)
		{
			::MapGeneration::generationCheckpoint();
			cornerObstacles.at(corner).push_back(int(&o - obstacles.data()));
		}
	}
	// Pairs that could come within minimumGap once both move, and the gap each must keep: its
	// unwarped gap, or minimumGap if that is less. Obstacles sharing a corner meet there by design.
	const int drift = 2 * (reach / kSubtile + 2);
	std::vector<std::vector<std::pair<int, int>>> nearby(obstacles.size());
	for (size_t a = 0; a < obstacles.size(); ++a)
	{
		::MapGeneration::generationCheckpoint();
		for (size_t b = a + 1; b < obstacles.size(); ++b)
		{
			::MapGeneration::generationCheckpoint();
			bool touching = false;
			for (int corner : obstacles.at(a).corners)
			{
				::MapGeneration::generationCheckpoint();
				touching = touching || std::count(obstacles.at(b).corners.begin(),
												  obstacles.at(b).corners.end(), corner);
			}
			if (touching)
				continue;
			const int apart = gap(obstacles.at(a), obstacles.at(b), minimumGap + drift);
			if (apart >= minimumGap + drift)
				continue;
			const int floor = std::min(apart, minimumGap);
			nearby.at(a).push_back({int(b), floor});
			nearby.at(b).push_back({int(a), floor});
		}
	}

	const auto clearanceOf = [&](int cell)
	{
		const std::vector<SubtilePoint> points = g.outline(cell);
		double least = 1e18;
		for (size_t k = 0; k < points.size(); ++k)
		{
			::MapGeneration::generationCheckpoint();
			least = std::min(least, lineDistance(g.cells.at(cell).centre, points.at(k),
												 points.at((k + 1) % points.size())));
		}
		return least;
	};
	std::vector<double> clearanceFloor(g.cells.size());
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		::MapGeneration::generationCheckpoint();
		clearanceFloor.at(cell) = std::min(clearanceOf(cell), double(minimumClearance) * kSubtile);
	}

	for (size_t corner = 0; corner < g.corners.size(); ++corner)
	{
		::MapGeneration::generationCheckpoint();
		const int dx = int(context.bounded(stream, std::uint32_t(2 * reach + 1))) - reach;
		const int dy = int(context.bounded(stream, std::uint32_t(2 * reach + 1))) - reach;
		const SubtilePoint home = g.corners.at(corner);
		for (int share = 1; share <= 16; share *= 2)
		{
			::MapGeneration::generationCheckpoint();
			g.corners.at(corner) = {home.x + dx / share, home.y + dy / share};
			for (int o : cornerObstacles.at(corner))
			{
				::MapGeneration::generationCheckpoint();
				trace(obstacles.at(o));
			}
			bool fits = true;
			for (int cell : cornerCells.at(corner))
			{
				::MapGeneration::generationCheckpoint();
				fits = fits && clearanceOf(cell) >= clearanceFloor.at(cell);
			}
			for (size_t k = 0; fits && k < cornerObstacles.at(corner).size(); ++k)
			{
				::MapGeneration::generationCheckpoint();
				const int o = cornerObstacles.at(corner).at(k);
				for (const auto &pair : nearby.at(o))
				{
					::MapGeneration::generationCheckpoint();
					if (gap(obstacles.at(o), obstacles.at(pair.first), pair.second) < pair.second)
					{
						fits = false;
						break;
					}
				}
			}
			if (fits)
				break;
			g.corners.at(corner) = home;
			for (int o : cornerObstacles.at(corner))
			{
				::MapGeneration::generationCheckpoint();
				trace(obstacles.at(o));
			}
		}
	}
}

std::vector<int> labelTiles(const Tessellation &g)
{
	std::vector<int> labels(size_t(g.t.size()), -1);
	bool overlap = false;
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		::MapGeneration::generationCheckpoint();
		forEachTileInPolygon(g.t, g.outline(cell),
							 [&](int tile)
							 {
								 overlap = overlap || labels.at(tile) >= 0;
								 labels.at(tile) = cell;
							 });
	}
	if (overlap || std::find(labels.begin(), labels.end(), -1) != labels.end())
		return {};
	return labels;
}
} // namespace MapGeneration
