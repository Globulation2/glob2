// SPDX-License-Identifier: GPL-3.0-or-later
#include "Tessellation.h"
#include "GenerationContext.h"
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
	const int owner = g.edges[edge].cells[0];
	const auto ends = g.edgeEnds(edge, owner);
	const ShapePoint a = tilePoint(ends.first), b = tilePoint(ends.second);
	const ShapePoint midpoint{(a.x + b.x) / 2, (a.y + b.y) / 2};
	const ShapePoint from = tilePoint(g.cells[owner].centre);
	const ShapePoint to = tilePoint(g.centreAcross(edge, owner));
	const auto approach = [&](ShapePoint centre)
	{
		const double vx = midpoint.x - centre.x, vy = midpoint.y - centre.y;
		const double scale = centreRadius / std::hypot(vx, vy);
		return ShapePoint{centre.x + vx * scale, centre.y + vy * scale};
	};
	if (std::hypot(midpoint.x - from.x, midpoint.y - from.y) <= centreRadius ||
		std::hypot(midpoint.x - to.x, midpoint.y - to.y) <= centreRadius)
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
	const double length = std::sqrt(double(ux * ux + uy * uy));
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
	const Cell &c = cells[cell];
	std::vector<SubtilePoint> points;
	for (size_t k = 0; k < c.corners.size(); ++k)
		points.push_back({corners[c.corners[k]].x + c.cornerShifts[k].x,
						  corners[c.corners[k]].y + c.cornerShifts[k].y});
	return points;
}

std::pair<SubtilePoint, SubtilePoint> Tessellation::edgeEnds(int edge, int cell) const
{
	const Cell &c = cells[cell];
	const size_t n = c.edges.size();
	const size_t k = size_t(std::find(c.edges.begin(), c.edges.end(), edge) - c.edges.begin());
	const auto at = [&](size_t i) -> SubtilePoint
	{
		return {corners[c.corners[i]].x + c.cornerShifts[i].x,
				corners[c.corners[i]].y + c.cornerShifts[i].y};
	};
	return {at(k % n), at((k + 1) % n)};
}

SubtilePoint Tessellation::centreAcross(int edge, int cell) const
{
	const int next = other(edge, cell);
	// The same edge seen from both sides runs in opposite directions; the difference between its
	// two copies is the shift that carries the neighbour beside this cell.
	const SubtilePoint here = edgeEnds(edge, cell).first, there = edgeEnds(edge, next).second;
	return {cells[next].centre.x + here.x - there.x, cells[next].centre.y + here.y - there.y};
}

RegionGraph Tessellation::neighbours() const
{
	RegionGraph graph(cells.size());
	for (int cell = 0; cell < cellCount(); ++cell)
		for (int edge : cells[cell].edges)
			graph[cell].push_back(other(edge, cell));
	return graph;
}

long long Tessellation::distance2(int a, int b) const
{
	const SubtilePoint q = imageNear(cells[b].centre, cells[a].centre);
	const long long dx = q.x - cells[a].centre.x, dy = q.y - cells[a].centre.y;
	return dx * dx + dy * dy;
}

int Tessellation::transform(int cell, int dColumns, int dRows, bool mirrorColumns,
							bool mirrorRows) const
{
	int c = cells[cell].column, r = cells[cell].row;
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
		xs.push_back(i * width / g.columns);
	for (int i = 0; i <= g.rows; ++i)
		ys.push_back(i * height / g.rows);
	// A corner is owned by the cell to its lower right, so corner (c, r) has the cell's index.
	for (int r = 0; r < g.rows; ++r)
		for (int c = 0; c < g.columns; ++c)
			g.corners.push_back(subtileCentre(xs[c], ys[r]));
	for (int r = 0; r < g.rows; ++r)
		for (int c = 0; c < g.columns; ++c)
		{
			const int self = g.cellAt(c, r);
			Tessellation::Cell cell;
			cell.column = c;
			cell.row = r;
			cell.centre = subtileCentre((xs[c] + xs[c + 1]) / 2, (ys[r] + ys[r + 1]) / 2);
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
	for (int self = 0; self < g.cellCount(); ++self)
	{
		const auto &cell = g.cells[self];
		g.edges.push_back(
			{{self, g.cellAt(cell.column + 1, cell.row)}, {cell.corners[0], cell.corners[1]}});
		g.edges.push_back(
			{{self, g.cellAt(cell.column, cell.row + 1)}, {cell.corners[1], cell.corners[2]}});
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
		for (int c = 0; c < g.columns; ++c)
		{
			const int p = r & 1;
			g.corners.push_back(at(2 * c + p, 3 * r - 2));
			g.corners.push_back(at(2 * c + p + 1, 3 * r - 1));
		}
	for (int r = 0; r < g.rows; ++r)
		for (int c = 0; c < g.columns; ++c)
		{
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
				g.edges.push_back({{self, owned[k]}, {cell.corners[k], cell.corners[k + 1]}});
		}
	return g;
}

int shortestEdgeSteps(const Tessellation &g)
{
	int shortest = INT_MAX;
	for (int edge = 0; edge < int(g.edges.size()); ++edge)
	{
		const auto ends = g.edgeEnds(edge, g.edges[edge].cells[0]);
		shortest = std::min(shortest, edgeSteps(ends.first, ends.second));
	}
	return shortest;
}

int centreClearance(const Tessellation &g)
{
	double least = 1e18;
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		const std::vector<SubtilePoint> points = g.outline(cell);
		for (size_t k = 0; k < points.size(); ++k)
			least = std::min(least, lineDistance(g.cells[cell].centre, points[k],
												 points[(k + 1) % points.size()]));
	}
	return int(std::floor(least / kSubtile));
}

int warpLimit(const Tessellation &g)
{
	double least = 1e18;
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		const std::vector<SubtilePoint> points = g.outline(cell);
		const size_t n = points.size();
		for (size_t k = 0; k < n; ++k)
			for (size_t e = 0; e < n; ++e)
				if (e != k && (e + 1) % n != k)
					least =
						std::min(least, lineDistance(points[k], points[e], points[(e + 1) % n]));
	}
	// Three corners move at once (this one and the far edge's two ends), each by up to sqrt(2) times
	// its reach on one axis.
	return std::max(0, int(std::floor(least / (3 * std::sqrt(2.0)))) - 1);
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
		for (int corner : g.cells[cell].corners)
			cornerCells[corner].push_back(cell);
	for (int edge = 0; edge < int(g.edges.size()); ++edge)
		if (walls[edge])
			for (int corner : g.edges[edge].corners)
				cornerWalls[corner].push_back(edge);

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
		if (walls[edge])
			obstacles.push_back({edge, -1, {g.edges[edge].corners[0], g.edges[edge].corners[1]}});
	for (int corner = 0; corner < int(g.corners.size()); ++corner)
		if (cornerWalls[corner].empty())
			obstacles.push_back({-1, corner, {corner}});
	const auto trace = [&](Obstacle &o)
	{
		std::vector<std::pair<long long, long long>> raw;
		if (o.edge >= 0)
		{
			const auto ends = g.edgeEnds(o.edge, g.edges[o.edge].cells[0]);
			raw = sealedSegmentTiles(ends.first, ends.second);
		}
		else
			raw = {{subtileTile(g.corners[o.corner].x), subtileTile(g.corners[o.corner].y)}};
		long long x0 = raw[0].first, x1 = x0, y0 = raw[0].second, y1 = y0;
		o.tiles.clear();
		for (const auto &tile : raw)
		{
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
	// boxes alone reports `enough`.
	const auto gap = [&](const Obstacle &a, const Obstacle &b, int enough)
	{
		const long long apart =
			t.chebyshev(int(a.cx), int(a.cy), int(b.cx), int(b.cy)) - a.radius - b.radius;
		if (apart >= enough)
			return enough;
		int least = enough;
		for (const auto &p : a.tiles)
			for (const auto &q : b.tiles)
				least = std::min(least, t.chebyshev(p.first, p.second, q.first, q.second));
		return least;
	};
	for (Obstacle &o : obstacles)
	{
		trace(o);
		for (int corner : o.corners)
			cornerObstacles[corner].push_back(int(&o - obstacles.data()));
	}
	// Pairs that could come within minimumGap once both move, and the gap each must keep: its
	// unwarped gap, or minimumGap if that is less. Obstacles sharing a corner meet there by design.
	const int drift = 2 * (reach / kSubtile + 2);
	std::vector<std::vector<std::pair<int, int>>> nearby(obstacles.size());
	for (size_t a = 0; a < obstacles.size(); ++a)
		for (size_t b = a + 1; b < obstacles.size(); ++b)
		{
			bool touching = false;
			for (int corner : obstacles[a].corners)
				touching = touching || std::count(obstacles[b].corners.begin(),
												  obstacles[b].corners.end(), corner);
			if (touching)
				continue;
			const int apart = gap(obstacles[a], obstacles[b], minimumGap + drift);
			if (apart >= minimumGap + drift)
				continue;
			const int floor = std::min(apart, minimumGap);
			nearby[a].push_back({int(b), floor});
			nearby[b].push_back({int(a), floor});
		}

	const auto clearanceOf = [&](int cell)
	{
		const std::vector<SubtilePoint> points = g.outline(cell);
		double least = 1e18;
		for (size_t k = 0; k < points.size(); ++k)
			least = std::min(least, lineDistance(g.cells[cell].centre, points[k],
												 points[(k + 1) % points.size()]));
		return least;
	};
	std::vector<double> clearanceFloor(g.cells.size());
	for (int cell = 0; cell < g.cellCount(); ++cell)
		clearanceFloor[cell] = std::min(clearanceOf(cell), double(minimumClearance) * kSubtile);

	for (size_t corner = 0; corner < g.corners.size(); ++corner)
	{
		const int dx = int(context.bounded(stream, std::uint32_t(2 * reach + 1))) - reach;
		const int dy = int(context.bounded(stream, std::uint32_t(2 * reach + 1))) - reach;
		const SubtilePoint home = g.corners[corner];
		for (int share = 1; share <= 16; share *= 2)
		{
			g.corners[corner] = {home.x + dx / share, home.y + dy / share};
			for (int o : cornerObstacles[corner])
				trace(obstacles[o]);
			bool fits = true;
			for (int cell : cornerCells[corner])
				fits = fits && clearanceOf(cell) >= clearanceFloor[cell];
			for (size_t k = 0; fits && k < cornerObstacles[corner].size(); ++k)
			{
				const int o = cornerObstacles[corner][k];
				for (const auto &pair : nearby[o])
					if (gap(obstacles[o], obstacles[pair.first], pair.second) < pair.second)
					{
						fits = false;
						break;
					}
			}
			if (fits)
				break;
			g.corners[corner] = home;
			for (int o : cornerObstacles[corner])
				trace(obstacles[o]);
		}
	}
}

std::vector<int> labelTiles(const Tessellation &g)
{
	std::vector<int> labels(size_t(g.t.size()), -1);
	bool overlap = false;
	for (int cell = 0; cell < g.cellCount(); ++cell)
		forEachTileInPolygon(g.t, g.outline(cell),
							 [&](int tile)
							 {
								 overlap = overlap || labels[tile] >= 0;
								 labels[tile] = cell;
							 });
	if (overlap || std::find(labels.begin(), labels.end(), -1) != labels.end())
		return {};
	return labels;
}
} // namespace MapGeneration
