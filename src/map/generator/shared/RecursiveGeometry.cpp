// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecursiveGeometry.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
namespace MapGeneration
{
namespace
{
// Validate differences in a wider type before calling width()/height(): callers
// may supply arbitrary unwrapped coordinates, including opposite integer limits.
// Valid endpoints already bound the subsequent boundary additions; only the
// extent products need the explicit 1M limit.
bool validBounds(RegionBounds b)
{
	const int64_t width = int64_t(b.x1) - b.x0;
	const int64_t height = int64_t(b.y1) - b.y0;
	return width > 0 && height > 0 && width <= 1048576 && height <= 1048576;
}
} // namespace

RegionTree partitionRegions(RegionBounds bounds, int divisions, int maximumDepth, int minimumSide,
							const std::function<RegionStop(const RecursiveRegion &)> &stop,
							int maximumNodes)
{
	RegionTree result;
	// These public helpers can be called outside the catalog, so reject unsafe shifts and
	// allocations here as well. 1M extents keep all intermediate products bounded.
	if ((divisions != 2 && divisions != 3) || maximumDepth < 0 || maximumDepth > 10 ||
		minimumSide < 1 || maximumNodes < 1 || maximumNodes > 65536 || !validBounds(bounds))
	{
		result.failure = "Invalid recursive bounds, division, depth, size or node limit.";
		return result;
	}
	result.regions.push_back({0, -1, 0, bounds, {}});
	for (size_t head = 0; head < result.regions.size(); ++head)
	{
		// Copy before appending: vector growth must not invalidate the parent being subdivided.
		const RecursiveRegion region = result.regions[head];
		RegionStop reason = stop ? stop(region) : RegionStop::None;
		if (reason == RegionStop::None && region.depth >= maximumDepth)
			reason = RegionStop::Depth;
		if (reason == RegionStop::None &&
			(std::min(region.bounds.width(), region.bounds.height()) / divisions < minimumSide))
			reason = RegionStop::Size;
		if (reason == RegionStop::None &&
			int(result.regions.size()) + divisions * divisions > maximumNodes)
		{
			reason = RegionStop::Limit;
			result.failure = "Recursive partition exceeds the node limit.";
		}
		result.regions[head].stop = reason;
		if (reason != RegionStop::None)
			continue;
		int xs[4], ys[4];
		for (int k = 0; k <= divisions; ++k)
		{
			xs[k] = region.bounds.x0 + region.bounds.width() * k / divisions;
			ys[k] = region.bounds.y0 + region.bounds.height() * k / divisions;
		}
		for (int y = 0; y < divisions; ++y)
			for (int x = 0; x < divisions; ++x)
			{
				const int id = int(result.regions.size());
				result.regions[head].children.push_back(id);
				result.regions.push_back(
					{id, region.id, region.depth + 1, {xs[x], ys[y], xs[x + 1], ys[y + 1]}, {}});
			}
		result.actualDepth = std::max(result.actualDepth, region.depth + 1);
	}
	return result;
}

HilbertPath hilbertPath(RegionBounds bounds, int maximumOrder, int minimumSpacing, int orientation)
{
	HilbertPath result;
	if (maximumOrder < 1 || maximumOrder > 7 || minimumSpacing < 1 || orientation < 0 ||
		orientation > 7 || !validBounds(bounds))
	{
		result.failure = "Invalid Hilbert bounds, order, spacing or orientation.";
		return result;
	}
	int order = maximumOrder;
	while (order > 0 && std::min(bounds.width(), bounds.height()) / (1 << order) < minimumSpacing)
		--order;
	result.actualOrder = order;
	result.stop = order < maximumOrder ? RegionStop::Spacing : RegionStop::Depth;
	if (!order)
	{
		result.failure =
			"No Hilbert fold fits the requested bank spacing; enlarge the shorter map axis.";
		return result;
	}
	result.tree = partitionRegions(bounds, 2, order, 1, {}, 32768);
	if (!result.tree.failure.empty())
	{
		result.failure = result.tree.failure;
		return result;
	}
	const int side = 1 << order;

	// Decode base-four Hilbert digits from the smallest square outward. Swapping x/y and
	// reflecting BOTH axes in the entering quadrant is what joins consecutive child curves;
	// independently rotating child rectangles would break entry/exit continuity.
	std::vector<int> owners;
	for (int d = 0; d < side * side; ++d)
	{
		int x = 0, y = 0, digits = d;
		for (int scale = 1; scale < side; scale *= 2)
		{
			const int rx = (digits / 2) & 1, ry = (digits ^ rx) & 1;
			if (!ry)
			{
				if (rx)
				{
					x = scale - 1 - x;
					y = scale - 1 - y;
				}
				std::swap(x, y);
			}
			x += scale * rx;
			y += scale * ry;
			digits /= 4;
		}
		if (orientation & 4)
			x = side - 1 - x;
		for (int turn = 0; turn < (orientation & 3); ++turn)
		{
			const int oldX = x;
			x = side - 1 - y;
			y = oldX;
		}
		ShapePoint p{bounds.x0 + (x + 0.5) * bounds.width() / side,
					 bounds.y0 + (y + 0.5) * bounds.height() / side};
		result.points.push_back(p);
		// Descend the tree instead of scanning every leaf for every point (quadratic
		// at high order). floor is important for valid unwrapped negative coordinates.
		int owner = 0;
		while (!result.tree.regions[owner].terminal())
			for (int child : result.tree.regions[owner].children)
				if (result.tree.regions[child].bounds.contains(int(std::floor(p.x)),
															   int(std::floor(p.y))))
				{
					owner = child;
					break;
				}
		owners.push_back(owner);
		if (d == 0)
			continue;
		// Segment hierarchy is its endpoints' lowest common ancestor. This preserves the
		// distinction between a local fold and a join across a major recursive district.
		int a = owners[d - 1], b = owner;
		while (a != b)
		{
			a = result.tree.regions[a].parent;
			b = result.tree.regions[b].parent;
		}
		result.segments.push_back(
			{d - 1, result.tree.regions[a].depth, a, result.points[d - 1], p});
	}
	return result;
}
} // namespace MapGeneration
