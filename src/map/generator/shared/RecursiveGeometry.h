// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Geometry.h"
#include <functional>
#include <string>
#include <vector>
namespace MapGeneration
{
enum class RegionStop
{
	None,
	Depth,
	Size,
	Home,
	Spacing,
	Caller,
	Limit
};
struct RecursiveRegion
{
	int id, parent, depth;
	RegionBounds bounds;
	std::vector<int> children;
	RegionStop stop = RegionStop::None;
	bool terminal() const { return children.empty(); }
};
struct RegionTree
{
	std::vector<RecursiveRegion> regions;
	int actualDepth = 0;
	std::string failure;
};
/// Breadth-first IDs depend only on geometry and stopping, never on colony assignment. The
/// callback can reserve a home or give a map-specific stop reason. A terminal still covers its
/// whole rectangle: stopping does not discard land. Limits are explicit errors, never partial success.
RegionTree partitionRegions(RegionBounds, int divisions, int maximumDepth, int minimumSide,
							const std::function<RegionStop(const RecursiveRegion &)> &stop = {},
							int maximumNodes = 16384);
struct RecursiveSegment
{
	int id, level, parentRegion;
	ShapePoint from, to;
};
struct HilbertPath
{
	RegionTree tree;
	std::vector<ShapePoint> points;
	std::vector<RecursiveSegment> segments;
	int actualOrder = 0;
	RegionStop stop = RegionStop::Depth;
	std::string failure;
};
/// Uniform order is reduced until cell-centre spacing on BOTH axes meets minimumSpacing.
/// orientation 0..7 is a square rotation/reflection BEFORE rectangular scaling. Widths are
/// deliberately absent: callers stroke these unwrapped points with channel widths in tile units.
HilbertPath hilbertPath(RegionBounds, int maximumOrder, int minimumSpacing, int orientation = 0);
} // namespace MapGeneration
