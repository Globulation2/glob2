#include "GenerationWork.h"
#include "GenerationNumeric.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Drawing.h"
#include <algorithm>
#include <cmath>
namespace MapGeneration
{
bool strokeIntersectsMask(const Torus &t, const std::vector<StrokePoint> &path,
						  const std::vector<unsigned char> &protectedMask)
{
	std::vector<unsigned char> stroke(t.size(), 0);
	strokePath(stroke, t, path);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (stroke.at(i) && protectedMask.at(i))
			return true;
	}
	return false;
}

void fillRectangle(std::vector<unsigned char> &mask, const Torus &t, RegionBounds b,
				   unsigned char value)
{
	for (int y = b.y0; y < b.y1; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = b.x0; x < b.x1; ++x)
		{
			::MapGeneration::generationCheckpoint();
			mask.at(t.at(x, y)) = value;
		}
	}
}

std::vector<StrokePoint> downhillPath(ShapePoint centre, double fromRadius, double toRadius,
									  double heading, double fromHalfWidth, double toHalfWidth,
									  std::mt19937 &random, const DownhillStyle &style,
									  const Stretch &stretch)
{
	for (double v :
		 {centre.x, centre.y, fromRadius, toRadius, heading, fromHalfWidth, toHalfWidth, style.step,
		  style.memory, style.angularNoise, style.maxDrift, stretch.sx, stretch.sy})
	{
		::MapGeneration::generationCheckpoint();
		if (!std::isfinite(v))
			return {};
	}
	if (fromRadius < 0 || toRadius <= fromRadius || style.step <= 0 || style.memory < 0 ||
		style.memory >= 1 || style.angularNoise < 0 || style.maxDrift < 0 || fromHalfWidth <= 0 ||
		toHalfWidth <= 0 || stretch.sx <= 0 || stretch.sy <= 0)
		return {};
	// A defensive cap prevents an accidentally microscopic step allocating unbounded
	// memory. 65536 segments already exceed a full diagonal of any supported map.
	const double needed = ::MapGeneration::Numeric::ceil((toRadius - fromRadius) / style.step);
	if (needed > 65536)
		return {};
	const int segments = int(needed);
	std::vector<StrokePoint> path;
	path.reserve(segments + 1);
	double turn = 0, angle = heading;
	for (int k = 0; k <= segments; ++k)
	{
		::MapGeneration::generationCheckpoint();
		const double radius = std::min(toRadius, fromRadius + k * style.step);
		const double progress = (radius - fromRadius) / (toRadius - fromRadius);
		if (k)
		{
			// Explicit conversion avoids implementation-defined uniform distributions.
			const double roll = random() / 4294967296.0;
			turn = style.memory * turn + (2 * roll - 1) * style.angularNoise;
			angle = std::clamp(angle + turn, heading - style.maxDrift, heading + style.maxDrift);
		}
		const ShapePoint p =
			stretch.apply(centre.x, centre.y, polarPoint(centre.x, centre.y, radius, angle));
		path.push_back({p.x, p.y, fromHalfWidth + (toHalfWidth - fromHalfWidth) * progress});
	}
	return path;
}

namespace
{
void strokeSegment(std::vector<unsigned char> &mask, const Torus &t, const StrokePoint &a,
				   const StrokePoint &b, unsigned char value)
{
	const double reach = std::max(a.halfWidth, b.halfWidth);
	const int x0 = int(::MapGeneration::Numeric::floor(std::min(a.x, b.x) - reach)),
			  x1 = int(::MapGeneration::Numeric::ceil(std::max(a.x, b.x) + reach));
	const int y0 = int(::MapGeneration::Numeric::floor(std::min(a.y, b.y) - reach)),
			  y1 = int(::MapGeneration::Numeric::ceil(std::max(a.y, b.y) + reach));
	const double ux = b.x - a.x, uy = b.y - a.y, length2 = ux * ux + uy * uy;
	for (int y = y0; y <= y1; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = x0; x <= x1; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const double px = x - a.x, py = y - a.y;
			const double along =
				length2 > 0 ? std::clamp((px * ux + py * uy) / length2, 0.0, 1.0) : 0.0;
			const double dx = px - along * ux, dy = py - along * uy;
			const double halfWidth = a.halfWidth + along * (b.halfWidth - a.halfWidth);
			if (dx * dx + dy * dy < halfWidth * halfWidth)
				mask.at(t.at(x, y)) = value;
		}
	}
}
} // namespace

void strokePath(std::vector<unsigned char> &mask, const Torus &t,
				const std::vector<StrokePoint> &path, unsigned char value, bool closed)
{
	if (path.empty())
		return;
	if (path.size() == 1)
		strokeSegment(mask, t, path.at(0), path.at(0), value);
	for (size_t i = 0; i + 1 < path.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		strokeSegment(mask, t, path.at(i), path.at(i + 1), value);
	}
	if (closed && path.size() > 2)
		strokeSegment(mask, t, path.back(), path.front(), value);
}

void tracePath(std::vector<unsigned char> &mask, const Torus &t,
			   const std::vector<StrokePoint> &path, unsigned char value)
{
	for (size_t i = 0; i < path.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		int x0 = int(::MapGeneration::Numeric::lround(path.at(i).x)),
			y0 = int(::MapGeneration::Numeric::lround(path.at(i).y));
		if (i + 1 == path.size())
		{
			mask.at(t.at(x0, y0)) = value;
			break;
		}
		const int x1 = int(::MapGeneration::Numeric::lround(path.at(i + 1).x)),
				  y1 = int(::MapGeneration::Numeric::lround(path.at(i + 1).y));
		const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
		const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
		int error = dx + dy;
		while (x0 != x1 || y0 != y1)
		{
			::MapGeneration::generationCheckpoint();
			mask.at(t.at(x0, y0)) = value;
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

std::vector<std::pair<long long, long long>> sealedSegmentTiles(SubtilePoint a, SubtilePoint b)
{
	// A grid walk (Amanatides and Woo) in integers: from the tile holding `a`, step into whichever
	// neighbouring column or row the segment reaches first, comparing the distances to the next
	// boundaries by cross-multiplication. A tie is a corner, where both tiles beside it are taken.
	long long x = subtileTile(a.x), y = subtileTile(a.y);
	const long long endX = subtileTile(b.x), endY = subtileTile(b.y);
	const long long dx = b.x - a.x, dy = b.y - a.y;
	const int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
	const long long adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
	std::vector<std::pair<long long, long long>> tiles{{x, y}};
	const long long limit = (endX > x ? endX - x : x - endX) + (endY > y ? endY - y : y - endY);
	for (long long step = 0; step < limit && (x != endX || y != endY); ++step)
	{
		::MapGeneration::generationCheckpoint();
		// Distance along each axis to the boundary the segment crosses next, in subtile units.
		const long long toX = adx ? (sx > 0 ? (x + 1) * kSubtile - a.x : a.x - x * kSubtile) : 0;
		const long long toY = ady ? (sy > 0 ? (y + 1) * kSubtile - a.y : a.y - y * kSubtile) : 0;
		// Parameters toX / adx and toY / ady; an axis the segment never moves along never wins.
		const bool xFirst = x != endX && (y == endY || !ady || (adx && toX * ady < toY * adx));
		const bool yFirst = y != endY && (x == endX || !adx || (ady && toY * adx < toX * ady));
		if (xFirst)
			x += sx;
		else if (yFirst)
			y += sy;
		else
		{
			tiles.push_back({x + sx, y});
			tiles.push_back({x, y + sy});
			x += sx;
			y += sy;
			++step;
		}
		tiles.push_back({x, y});
	}
	return tiles;
}

void traceSealedPath(std::vector<unsigned char> &mask, const Torus &t,
					 const std::vector<SubtilePoint> &points, unsigned char value, bool closed)
{
	const size_t n = points.size();
	if (n == 1)
		mask.at(t.at(int(subtileTile(points.at(0).x)), int(subtileTile(points.at(0).y)))) = value;
	for (size_t k = 0; k + 1 < n || (closed && n > 2 && k < n); ++k)
	{
		::MapGeneration::generationCheckpoint();
		for (const auto &tile : sealedSegmentTiles(points.at(k), points.at((k + 1) % n)))
		{
			::MapGeneration::generationCheckpoint();
			mask.at(t.at(int(tile.first), int(tile.second))) = value;
		}
	}
}

void fillPolygon(std::vector<unsigned char> &mask, const Torus &t,
				 const std::vector<SubtilePoint> &outline, unsigned char value)
{
	forEachTileInPolygon(t, outline, [&](int tile) { mask.at(tile) = value; });
}

std::vector<StrokePoint> bezierPath(ShapePoint from, ShapePoint control, ShapePoint to,
									double fromHalfWidth, double toHalfWidth, int segments)
{
	std::vector<StrokePoint> path;
	segments = std::max(1, segments);
	for (int k = 0; k <= segments; ++k)
	{
		::MapGeneration::generationCheckpoint();
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
	const double ux = ::MapGeneration::Numeric::cos(heading),
				 uy = ::MapGeneration::Numeric::sin(heading);
	const ShapePoint to{from.x + length * ux, from.y + length * uy};
	// A quadratic Bezier's middle sits halfway between the chord's middle and its control point,
	// so the control goes twice the bow off the chord, to the left of the heading (-uy, ux).
	const double off = 2 * bend * length;
	const ShapePoint control{(from.x + to.x) / 2 - off * uy, (from.y + to.y) / 2 + off * ux};
	return bezierPath(from, control, to, fromHalfWidth, toHalfWidth, segments);
}

std::vector<StrokePoint> wanderingPath(const Torus &t, ShapePoint from, ShapePoint to,
									   double halfWidth, double wander, double widthJitter,
									   std::mt19937 &random)
{
	const auto nearest = [](double d, int period)
	{
		d = ::MapGeneration::Numeric::fmod(d, double(period));
		if (d > period / 2.0)
			d -= period;
		else if (d < -period / 2.0)
			d += period;
		return d;
	};
	const double dx = nearest(to.x - from.x, t.w), dy = nearest(to.y - from.y, t.h);
	const double length = ::MapGeneration::Numeric::hypot(dx, dy);
	// A draw in [-1, 1) straight from the generator's bits: std::uniform_real_distribution differs
	// between standard libraries.
	const auto unit = [](std::mt19937 &r) { return (double(r()) - 2147483648.0) / 2147483648.0; };
	// Three harmonics of a sine that vanishes at both ends for the sideways push (weaker as they get
	// finer), and three phases for the width.
	double bend[3], phase[3];
	for (double &b : bend)
	{
		::MapGeneration::generationCheckpoint();
		b = unit(random);
	}
	for (double &p : phase)
	{
		::MapGeneration::generationCheckpoint();
		p = unit(random) * kPi;
	}
	const int segments = std::max(1, int(::MapGeneration::Numeric::ceil(length)));
	const double nx = length > 0 ? -dy / length : 0, ny = length > 0 ? dx / length : 0;
	std::vector<StrokePoint> path;
	path.reserve(size_t(segments) + 1);
	for (int i = 0; i <= segments; ++i)
	{
		::MapGeneration::generationCheckpoint();
		const double s = double(i) / segments;
		double side = 0, swell = 0;
		for (int h = 0; h < 3; ++h)
		{
			::MapGeneration::generationCheckpoint();
			side += bend[h] * ::MapGeneration::Numeric::sin(kPi * (h + 1) * s) / (h + 1);
			swell += ::MapGeneration::Numeric::sin(2 * kPi * (h + 1) * s + phase[h]) / 3;
		}
		const double offset = wander * side / (1 + 1 / 2.0 + 1 / 3.0);
		path.push_back({from.x + dx * s + nx * offset, from.y + dy * s + ny * offset,
						std::max(0.5, halfWidth * (1 + widthJitter * swell))});
	}
	return path;
}

void carveCorridor(std::vector<unsigned char> &mask, const Torus &t, ShapePoint from, ShapePoint to,
				   double halfWidth, double wander, double widthJitter, std::mt19937 &random,
				   unsigned char value)
{
	strokePath(mask, t, wanderingPath(t, from, to, halfWidth, wander, widthJitter, random), value);
}

namespace
{
// The distance from a point to a path's centre line, less the path's half width at the nearest
// point on it.
double gapToPath(double x, double y, const std::vector<StrokePoint> &path)
{
	double best = INFINITY;
	if (path.size() == 1)
		return ::MapGeneration::Numeric::hypot(x - path.at(0).x, y - path.at(0).y) -
			   path.at(0).halfWidth;
	for (size_t i = 0; i + 1 < path.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		const StrokePoint &a = path.at(i), &b = path.at(i + 1);
		const double ux = b.x - a.x, uy = b.y - a.y, length2 = ux * ux + uy * uy;
		const double along =
			length2 > 0 ? std::clamp(((x - a.x) * ux + (y - a.y) * uy) / length2, 0.0, 1.0) : 0.0;
		const double gap =
			::MapGeneration::Numeric::hypot(x - a.x - along * ux, y - a.y - along * uy) -
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
			::MapGeneration::generationCheckpoint();
			const StrokePoint &p = path.at(i), &q = path.at(i + 1);
			const int steps =
				std::max(1, int(::MapGeneration::Numeric::ceil(
								::MapGeneration::Numeric::hypot(q.x - p.x, q.y - p.y))));
			for (int k = 1; k <= steps; ++k)
			{
				::MapGeneration::generationCheckpoint();
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
		{
			::MapGeneration::generationCheckpoint();
			if (::MapGeneration::Numeric::hypot(p.x - a.front().x, p.y - a.front().y) >= skip)
				measured.push_back(p);
		}
	if (measured.empty() || b.empty())
		return INFINITY;
	double best = INFINITY;
	for (const StrokePoint &p : measured)
	{
		::MapGeneration::generationCheckpoint();
		best = std::min(best, gapToPath(p.x, p.y, b) - p.halfWidth);
	}
	for (const StrokePoint &p : dense(b))
	{
		::MapGeneration::generationCheckpoint();
		best = std::min(best, gapToPath(p.x, p.y, measured) - p.halfWidth);
	}
	return best;
}

PathBounds pathBounds(const std::vector<StrokePoint> &path)
{
	if (path.empty())
		return {0, 0, 0};
	double x0 = path.at(0).x, x1 = x0, y0 = path.at(0).y, y1 = y0;
	for (const StrokePoint &p : path)
	{
		::MapGeneration::generationCheckpoint();
		x0 = std::min(x0, p.x);
		x1 = std::max(x1, p.x);
		y0 = std::min(y0, p.y);
		y1 = std::max(y1, p.y);
	}
	PathBounds bounds{(x0 + x1) / 2, (y0 + y1) / 2, 0};
	for (const StrokePoint &p : path)
	{
		::MapGeneration::generationCheckpoint();
		bounds.radius =
			std::max(bounds.radius,
					 ::MapGeneration::Numeric::hypot(p.x - bounds.x, p.y - bounds.y) + p.halfWidth);
	}
	return bounds;
}

Zigzag zigzagPath(const AxisFrame &frame, double start, double firstLeg, double pitch, int legs,
				  double span, double finish, double halfWidth)
{
	Zigzag zigzag;
	double side = span;
	const auto point = [&](double along, double across)
	{
		const ShapePoint p = frame.at(along, across);
		zigzag.path.push_back({p.x, p.y, halfWidth});
	};
	point(start, side);
	for (int j = 0; j < legs; ++j)
	{
		::MapGeneration::generationCheckpoint();
		const double along = firstLeg - j * pitch;
		point(along, side);
		const ShapePoint a = frame.at(along, side - (side > 0 ? halfWidth : -halfWidth));
		side = -side;
		point(along, side);
		const ShapePoint b = frame.at(along, side - (side > 0 ? halfWidth : -halfWidth));
		zigzag.legs.push_back({{a.x, a.y, halfWidth}, {b.x, b.y, halfWidth}});
	}
	point(finish, side);
	zigzag.finishAcross = side;
	return zigzag;
}

std::vector<StrokePoint> splinePath(const std::vector<ShapePoint> &waypoints, double step)
{
	std::vector<StrokePoint> path;
	if (waypoints.empty())
		return path;
	std::vector<ShapePoint> knots{waypoints.front()};
	knots.insert(knots.end(), waypoints.begin(), waypoints.end());
	knots.push_back(waypoints.back());
	for (size_t w = 1; w + 2 < knots.size(); ++w)
	{
		::MapGeneration::generationCheckpoint();
		const ShapePoint &p0 = knots.at(w - 1), &p1 = knots.at(w), &p2 = knots.at(w + 1),
						 &p3 = knots.at(w + 2);
		const int samples =
			std::max(2, int(::MapGeneration::Numeric::hypot(p2.x - p1.x, p2.y - p1.y) / step));
		for (int q = (w == 1 ? 0 : 1); q <= samples; ++q)
		{
			::MapGeneration::generationCheckpoint();
			const double u = double(q) / samples, u2 = u * u, u3 = u2 * u;
			const auto blend = [&](double a, double b, double c, double d)
			{
				return 0.5 * (2 * b + (c - a) * u + (2 * a - 5 * b + 4 * c - d) * u2 +
							  (3 * b - a - 3 * c + d) * u3);
			};
			path.push_back({blend(p0.x, p1.x, p2.x, p3.x), blend(p0.y, p1.y, p2.y, p3.y), 0});
		}
	}
	return path;
}
} // namespace MapGeneration
