// SPDX-License-Identifier: GPL-3.0-or-later
#include "Drawing.h"
#include <algorithm>
#include <cmath>
namespace MapGeneration
{
namespace
{
void strokeSegment(std::vector<unsigned char> &mask, const Torus &t, const StrokePoint &a,
				   const StrokePoint &b, unsigned char value)
{
	const double reach = std::max(a.halfWidth, b.halfWidth);
	const int x0 = int(std::floor(std::min(a.x, b.x) - reach)),
			  x1 = int(std::ceil(std::max(a.x, b.x) + reach));
	const int y0 = int(std::floor(std::min(a.y, b.y) - reach)),
			  y1 = int(std::ceil(std::max(a.y, b.y) + reach));
	const double ux = b.x - a.x, uy = b.y - a.y, length2 = ux * ux + uy * uy;
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x)
		{
			const double px = x - a.x, py = y - a.y;
			const double along =
				length2 > 0 ? std::clamp((px * ux + py * uy) / length2, 0.0, 1.0) : 0.0;
			const double dx = px - along * ux, dy = py - along * uy;
			const double halfWidth = a.halfWidth + along * (b.halfWidth - a.halfWidth);
			if (dx * dx + dy * dy < halfWidth * halfWidth)
				mask[t.at(x, y)] = value;
		}
}
} // namespace

void strokePath(std::vector<unsigned char> &mask, const Torus &t,
				const std::vector<StrokePoint> &path, unsigned char value, bool closed)
{
	if (path.empty())
		return;
	if (path.size() == 1)
		strokeSegment(mask, t, path[0], path[0], value);
	for (size_t i = 0; i + 1 < path.size(); ++i)
		strokeSegment(mask, t, path[i], path[i + 1], value);
	if (closed && path.size() > 2)
		strokeSegment(mask, t, path.back(), path.front(), value);
}

void tracePath(std::vector<unsigned char> &mask, const Torus &t,
			   const std::vector<StrokePoint> &path, unsigned char value)
{
	for (size_t i = 0; i < path.size(); ++i)
	{
		int x0 = int(std::lround(path[i].x)), y0 = int(std::lround(path[i].y));
		if (i + 1 == path.size())
		{
			mask[t.at(x0, y0)] = value;
			break;
		}
		const int x1 = int(std::lround(path[i + 1].x)), y1 = int(std::lround(path[i + 1].y));
		const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
		const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
		int error = dx + dy;
		while (x0 != x1 || y0 != y1)
		{
			mask[t.at(x0, y0)] = value;
			const int twice = 2 * error;
			if (twice >= dy)
			{
				error += dy;
				x0 += sx;
			}
			if (twice <= dx)
			{
				error += dx;
				y0 += sy;
			}
		}
	}
}

std::vector<StrokePoint> bezierPath(ShapePoint from, ShapePoint control, ShapePoint to,
									double fromHalfWidth, double toHalfWidth, int segments)
{
	std::vector<StrokePoint> path;
	segments = std::max(1, segments);
	for (int k = 0; k <= segments; ++k)
	{
		const double s = double(k) / segments, r = 1 - s;
		path.push_back({r * r * from.x + 2 * r * s * control.x + s * s * to.x,
						r * r * from.y + 2 * r * s * control.y + s * s * to.y,
						fromHalfWidth + s * (toHalfWidth - fromHalfWidth)});
	}
	return path;
}
std::vector<StrokePoint> bentPath(ShapePoint from, double heading, double length, double bend,
								  double fromHalfWidth, double toHalfWidth, int segments)
{
	const double ux = std::cos(heading), uy = std::sin(heading);
	const ShapePoint to{from.x + length * ux, from.y + length * uy};
	// A quadratic Bezier's middle sits halfway between the chord's middle and its control point,
	// so the control goes twice the bow off the chord, to the left of the heading (-uy, ux).
	const double off = 2 * bend * length;
	const ShapePoint control{(from.x + to.x) / 2 - off * uy, (from.y + to.y) / 2 + off * ux};
	return bezierPath(from, control, to, fromHalfWidth, toHalfWidth, segments);
}

namespace
{
// The distance from a point to a path's centre line, less the path's half width at the nearest
// point on it.
double gapToPath(double x, double y, const std::vector<StrokePoint> &path)
{
	double best = INFINITY;
	if (path.size() == 1)
		return std::hypot(x - path[0].x, y - path[0].y) - path[0].halfWidth;
	for (size_t i = 0; i + 1 < path.size(); ++i)
	{
		const StrokePoint &a = path[i], &b = path[i + 1];
		const double ux = b.x - a.x, uy = b.y - a.y, length2 = ux * ux + uy * uy;
		const double along =
			length2 > 0 ? std::clamp(((x - a.x) * ux + (y - a.y) * uy) / length2, 0.0, 1.0) : 0.0;
		const double gap = std::hypot(x - a.x - along * ux, y - a.y - along * uy) -
						   (a.halfWidth + along * (b.halfWidth - a.halfWidth));
		best = std::min(best, gap);
	}
	return best;
}
} // namespace

double pathClearance(const std::vector<StrokePoint> &a, const std::vector<StrokePoint> &b,
					 double skip)
{
	// Both paths are resampled at least once a tile, so measuring from every point of each to the
	// other's centre line cannot miss a long segment passing between two points. The part of `a`
	// near its root is dropped before measuring in either direction.
	const auto dense = [](const std::vector<StrokePoint> &path)
	{
		std::vector<StrokePoint> points;
		if (path.empty())
			return points;
		points.push_back(path.front());
		for (size_t i = 0; i + 1 < path.size(); ++i)
		{
			const StrokePoint &p = path[i], &q = path[i + 1];
			const int steps = std::max(1, int(std::ceil(std::hypot(q.x - p.x, q.y - p.y))));
			for (int k = 1; k <= steps; ++k)
			{
				const double f = double(k) / steps;
				points.push_back({p.x + f * (q.x - p.x), p.y + f * (q.y - p.y),
								  p.halfWidth + f * (q.halfWidth - p.halfWidth)});
			}
		}
		return points;
	};
	std::vector<StrokePoint> measured;
	if (!a.empty())
		for (const StrokePoint &p : dense(a))
			if (std::hypot(p.x - a.front().x, p.y - a.front().y) >= skip)
				measured.push_back(p);
	if (measured.empty() || b.empty())
		return INFINITY;
	double best = INFINITY;
	for (const StrokePoint &p : measured)
		best = std::min(best, gapToPath(p.x, p.y, b) - p.halfWidth);
	for (const StrokePoint &p : dense(b))
		best = std::min(best, gapToPath(p.x, p.y, measured) - p.halfWidth);
	return best;
}

PathBounds pathBounds(const std::vector<StrokePoint> &path)
{
	if (path.empty())
		return {0, 0, 0};
	double x0 = path[0].x, x1 = x0, y0 = path[0].y, y1 = y0;
	for (const StrokePoint &p : path)
	{
		x0 = std::min(x0, p.x);
		x1 = std::max(x1, p.x);
		y0 = std::min(y0, p.y);
		y1 = std::max(y1, p.y);
	}
	PathBounds bounds{(x0 + x1) / 2, (y0 + y1) / 2, 0};
	for (const StrokePoint &p : path)
		bounds.radius =
			std::max(bounds.radius, std::hypot(p.x - bounds.x, p.y - bounds.y) + p.halfWidth);
	return bounds;
}
} // namespace MapGeneration
