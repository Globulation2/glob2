// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Geometry.h"
#include "Grid.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
namespace MapGeneration
{
// Routes between sites on the torus: which sites are neighbours, where the ground between two of
// them is, and stepping stones along the way. Polder's hamlets (a grass disc half way between
// neighbouring villages) and Caravanserai's caravanserais and route oases are built from these; the
// arithmetic is the short way round the wrap every time, so a route never goes the long way round.

/// The point half way from `a` to `b`, the short way round.
inline ShapePoint midpointAcross(const Torus &t, ShapePoint a, ShapePoint b)
{
	const double dx = t.offsetX(int(a.x), int(b.x)), dy = t.offsetY(int(a.y), int(b.y));
	return {std::fmod(a.x + dx / 2 + t.w, t.w), std::fmod(a.y + dy / 2 + t.h, t.h)};
}

/// The straight-line distance between two sites, the short way round.
inline double siteDistance(const Torus &t, ShapePoint a, ShapePoint b)
{
	return std::hypot(t.offsetX(int(a.x), int(b.x)), t.offsetY(int(a.y), int(b.y)));
}

/// Each site's `count` nearest other sites, nearest first (ties to the lower index), as pairs
/// (site, neighbour) with site < neighbour, each pair once. A colony's neighbours on a lattice: the
/// four (or six) round it, however the lattice is sheared.
inline std::vector<std::pair<int, int>> nearestPairs(const Torus &t,
													 const std::vector<ShapePoint> &sites,
													 int count)
{
	std::vector<std::pair<int, int>> pairs;
	for (size_t a = 0; a < sites.size(); ++a)
	{
		std::vector<std::pair<double, int>> others;
		for (size_t b = 0; b < sites.size(); ++b)
			if (b != a)
				others.push_back({siteDistance(t, sites[a], sites[b]), int(b)});
		std::stable_sort(others.begin(), others.end());
		for (int k = 0; k < count && k < int(others.size()); ++k)
		{
			const std::pair<int, int> pair{std::min(int(a), others[k].second),
										   std::max(int(a), others[k].second)};
			if (std::find(pairs.begin(), pairs.end(), pair) == pairs.end())
				pairs.push_back(pair);
		}
	}
	return pairs;
}

/// Stepping stones along the straight way from `from` to `to` (the short way round): a point every
/// `spacing` tiles starting `fromGap` from `from`, as long as it stays `toGap` short of `to`.
/// Empty when the way is too short for even one. Positions wrap onto the map.
inline std::vector<ShapePoint> waypointsAlong(const Torus &t, ShapePoint from, ShapePoint to,
											  double spacing, double fromGap, double toGap)
{
	std::vector<ShapePoint> points;
	const double dx = t.offsetX(int(from.x), int(to.x)), dy = t.offsetY(int(from.y), int(to.y));
	const double length = std::hypot(dx, dy);
	if (length <= 0 || spacing <= 0)
		return points;
	for (double along = fromGap; along <= length - toGap; along += spacing)
		points.push_back({std::fmod(from.x + dx * along / length + t.w, t.w),
						  std::fmod(from.y + dy * along / length + t.h, t.h)});
	return points;
}

/// The heading (radians) from `from` towards `to`, the short way round, and that heading as the
/// nearest quarter turn: 0 for +x, 1 for +y, 2 for -x, 3 for -y (a stencil's facing, Orbits.h).
inline double headingAcross(const Torus &t, ShapePoint from, ShapePoint to)
{
	return std::atan2(t.offsetY(int(from.y), int(to.y)), t.offsetX(int(from.x), int(to.x)));
}
inline int quarterTurn(double heading)
{
	const int turn = int(std::lround(heading / (kPi / 2)));
	return ((turn % 4) + 4) % 4;
}
} // namespace MapGeneration
