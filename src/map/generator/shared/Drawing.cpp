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
} // namespace MapGeneration
