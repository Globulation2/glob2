// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Geometry.h"
#include "Grid.h"
#include <cmath>
#include <vector>
namespace MapGeneration
{
// Drawing on the torus: thick lines (threads, roads, channels, walls) as a path of points with a
// width at each, and filled radial shapes, rasterized onto tile masks. Coordinates are map tiles
// and may run past the edges; every tile is written through the wrap.

/// A point on a stroked path and the path's half width there. The width is interpolated linearly
/// between points, and every point is a round joint, so a path turns without gaps.
struct StrokePoint
{
	double x, y, halfWidth;
};

/// The point `radius` tiles from (cx, cy) at `angle` radians.
inline ShapePoint polarPoint(double cx, double cy, double radius, double angle)
{
	return {cx + radius * std::cos(angle), cy + radius * std::sin(angle)};
}

/// Sets `value` on every tile whose centre lies within the path's half width of it: a tile at
/// (x, y) is covered when its distance to the nearest segment is under the half width
/// interpolated at the nearest point. A closed path joins its last point back to its first.
void strokePath(std::vector<unsigned char> &mask, const Torus &, const std::vector<StrokePoint> &,
				unsigned char value = 1, bool closed = false);

/// `segments` + 1 points along the quadratic Bezier from `from` through the pull of `control`
/// to `to`, with the half width running linearly from one end's to the other's: a thread that
/// sags or bows.
std::vector<StrokePoint> bezierPath(ShapePoint from, ShapePoint control, ShapePoint to,
									double fromHalfWidth, double toHalfWidth, int segments);
/// Visits every tile a RadialShape centred at (cx, cy) and turned by `turn` radians covers,
/// searching only its bounding box: `visit(tile, dx, dy)` with the tile's offset from the centre.
/// Wrapped tiles are visited through the torus, each once while the shape is narrower than the
/// map. Turning one shape to each of several headings stamps the same outline round a centre.
template <typename Visit>
void forEachTileInShape(const Torus &t, double cx, double cy, const RadialShape &shape, double turn,
						Visit visit)
{
	const int reach = int(std::ceil(shape.maximumRadius())) + 1;
	const int x0 = int(std::lround(cx)), y0 = int(std::lround(cy));
	for (int y = y0 - reach; y <= y0 + reach; ++y)
		for (int x = x0 - reach; x <= x0 + reach; ++x)
		{
			const double dx = x - cx, dy = y - cy;
			if (std::hypot(dx, dy) < shape.radiusAt(std::atan2(dy, dx) - turn))
				visit(t.at(x, y), dx, dy);
		}
}

/// Sets `value` on every tile of a RadialShape centred at (cx, cy), turned by `turn`.
inline void fillShape(std::vector<unsigned char> &mask, const Torus &t, double cx, double cy,
					  const RadialShape &shape, double turn = 0, unsigned char value = 1)
{
	forEachTileInShape(t, cx, cy, shape, turn, [&](int i, double, double) { mask[i] = value; });
}
} // namespace MapGeneration
