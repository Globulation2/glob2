// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Geometry.h"
#include "Grid.h"
#include "RecursiveGeometry.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace MapGeneration
{
struct RegionEdge
{
	int from, to, length;
};
struct CrossingCandidate
{
	int id, fromRegion, toRegion, level, parentRegion, length;
	ShapePoint from, to;
};
struct CrossingProposals
{
	std::vector<CrossingCandidate> candidates;
	std::string failure;
};
/// Propose perpendicular crossings at caller-selected fractions along straight segments.
/// halfSpan and endClearance are tile units, independent of rectangular scaling. Samples
/// too close to a segment end are omitted (rounded bends need room); no raster legality
/// is implied. Callers still filter protected masks and check endpoints on finished land.
/// Fractions have stable caller-defined slots: ID = segment.id + slot*(maximumId+1), so
/// slot zero preserves segment IDs. No wrapping shortens the original segment. Limits:
/// 128 segments, 16 fractions, nonnegative unique segment IDs up to one million.
CrossingProposals transverseCrossings(const std::vector<RecursiveSegment> &,
									  const std::vector<double> &fractions, double halfSpan,
									  double endClearance);
struct CrossingGraph
{
	std::vector<RegionEdge> edges;
	int regions = 0;
	std::string failure;
};
/// Build the selector's coarse graph from actual eight-connected raster travel.
/// Assign two endpoint nodes per candidate (overwriting fromRegion/toRegion only on
/// success); preserve candidate identity and hierarchy. Includes every torus seam and
/// end-around route in the supplied passable mask. Endpoints must themselves be open.
/// Coordinates are unwrapped, floored to tiles, and wrapped. At most 128 candidates
/// keep the resulting graph within the selector's 256-node bound. No terrain edits.
CrossingGraph crossingEndpointGraph(const Torus &, const std::vector<unsigned char> &passable,
									std::vector<CrossingCandidate> &);

struct CrossingSelection
{
	std::vector<CrossingCandidate> selected;
	std::vector<long long> benefits; // graph-distance reduction when each was selected
	int mandatory = 0, local = 0, major = 0;
	int localShortfall = 0, majorShortfall = 0;
	std::string failure;
};
/// Connect required nodes first, outside optional budgets. Then greedily buy the greatest
/// all-pairs travel reduction, recomputing after every purchase (two similar bridges should
/// not both receive credit for the same detour). level==0 uses the global major budget;
/// higher levels use localPerParent for each distinct parent. Zero optional budgets never
/// remove connectivity. Required connections still obey geometric separation and may fail.
///
/// Candidate coordinates are unwrapped tile units; separation compares their midpoints on
/// the torus. The caller must supply legal, wide-enough passages and a graph that includes
/// end-around and seam routes. Graph estimates do not prove finished-world walkability.
/// Optional compatible(a,b) adds caller-specific geometric exclusions, such as keeping
/// BOTH ends of an opposing bridge pair apart. It must be pure, symmetric and deterministic;
/// required crossings obey it too. The built-in midpoint separation is always also enforced.
CrossingSelection
selectCrossings(const Torus &, int regions, const std::vector<RegionEdge> &,
				const std::vector<int> &required, const std::vector<CrossingCandidate> &,
				int localPerParent, int majorBudget, int separation, std::uint32_t seed,
				const std::function<bool(const CrossingCandidate &, const CrossingCandidate &)>
					&compatible = {});
} // namespace MapGeneration
