// SPDX-License-Identifier: GPL-3.0-or-later
#include "Lots.h"
#include <algorithm>
#include <cmath>
namespace MapGeneration
{
namespace
{
// Lane positions along one axis of length `side`: `count` lanes spread by whole tiles from `first`,
// so consecutive lanes are side / count apart give or take a tile and the last wraps onto the first.
std::vector<int> spreadLanes(int side, int count, int first)
{
	std::vector<int> lanes;
	for (int k = 0; k < count; ++k)
		lanes.push_back((first + int(std::int64_t(k) * side / count)) % side);
	std::sort(lanes.begin(), lanes.end());
	return lanes;
}
// The index of the last lane at or before `v` on a wrapped axis (the last lane when v lies before
// the first, since the last wraps round to it).
int laneBefore(const std::vector<int> &lanes, int v)
{
	int index = int(lanes.size()) - 1;
	for (size_t k = 0; k < lanes.size(); ++k)
		if (lanes[k] <= v)
			index = int(k);
	return index;
}
LaneGrid::Span spanAfter(const std::vector<int> &lanes, int index, int side)
{
	const int from = lanes[index], to = lanes[(index + 1) % lanes.size()];
	const int width = ((to - from) % side + side) % side;
	// One lane per axis: the block is everything but the lane itself.
	return {(from + 1) % side, (lanes.size() == 1 ? side : width) - 1};
}
} // namespace

int LaneGrid::column(int x) const
{
	return laneBefore(laneX, t.x(x));
}
int LaneGrid::row(int y) const
{
	return laneBefore(laneY, t.y(y));
}
LaneGrid::Span LaneGrid::columnSpan(int column) const
{
	return spanAfter(laneX, column, t.w);
}
LaneGrid::Span LaneGrid::rowSpan(int row) const
{
	return spanAfter(laneY, row, t.h);
}

LaneGrid layLanes(const Torus &t, int pitch, int x0, int y0)
{
	LaneGrid grid;
	grid.t = t;
	const int p = std::max(4, pitch);
	const int columns = std::max(1, int(std::lround(double(t.w) / p)));
	const int rows = std::max(1, int(std::lround(double(t.h) / p)));
	grid.laneX = spreadLanes(t.w, columns, t.x(x0));
	grid.laneY = spreadLanes(t.h, rows, t.y(y0));
	return grid;
}

std::vector<unsigned char> laneTiles(const LaneGrid &grid)
{
	const Torus &t = grid.t;
	std::vector<unsigned char> mask(t.size(), 0);
	for (int x : grid.laneX)
		for (int y = 0; y < t.h; ++y)
			mask[t.at(x, y)] = 1;
	for (int y : grid.laneY)
		for (int x = 0; x < t.w; ++x)
			mask[t.at(x, y)] = 1;
	return mask;
}

int stampLotPad(TerrainSketch &sketch, const LaneGrid &grid, Farm &pads, int column, int row,
				const std::vector<int> &sizes, int &x0, int &y0, int shrinkBy)
{
	const Torus &t = grid.t;
	const LaneGrid::Span across = grid.columnSpan(column), down = grid.rowSpan(row);
	// The room a pad has, in vertices from the left lane's vertex to the right lane's: a pad of
	// `size` tiles is `size` + 1 grass vertices with a ring vertex before and after, and either ring
	// vertex may stand on the lane's own vertex (sand on sand), so `size` + 1 vertices must fit
	// strictly between the two lanes, less `shrinkBy` vertices kept back on every side for a
	// ditch's beach. A block's span counts the tiles between its lanes, one fewer than the vertices
	// between them plus one... the same number.
	const int room = std::min(across.count, down.count) - 1 - 2 * shrinkBy;
	for (int size : sizes)
	{
		if (size <= 0 || size > room)
			continue;
		const FarmPlot pad{size, size, 1};
		x0 = t.x(across.start + shrinkBy + (across.count - 2 * shrinkBy - size - 1) / 2);
		y0 = t.y(down.start + shrinkBy + (down.count - 2 * shrinkBy - size - 1) / 2);
		stampFarmPlot(sketch, t, pads, x0, y0, pad);
		return size;
	}
	return 0;
}
} // namespace MapGeneration
