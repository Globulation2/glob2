// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vector>
namespace MapGeneration
{
using RegionGraph = std::vector<std::vector<int>>;
// Distances in edges, -1 for unreachable vertices; supports multiple sources.
std::vector<int> graphDistances(const RegionGraph &, const std::vector<int> &sources);
// Explicit adjacency policy: geometry connectivity is not necessarily unit pathfinding.
enum class GridNeighbors
{
	Cardinal,
	Eight
};
std::vector<int> connectedRegions(const std::vector<unsigned char> &passable, int width, int height,
								  bool wrap, GridNeighbors neighbors = GridNeighbors::Cardinal);

/// Labels carried by connected components. Negative component IDs are impassable;
/// negative labels are unclaimed ground which may still connect two labelled areas.
/// Component IDs must be dense, as returned by connectedRegions. A component may touch
/// several tiles with the same label, but touching different labels is a partition leak.
/// This tests future crop spread just as well as walking: the caller chooses the mask
/// and neighbor policy before assigning labels to its components.
struct ComponentLabels
{
	std::vector<int> owners; // -1 for a component touching no nonnegative label
	int conflictTile = -1;   // first conflicting tile in row-major order, or -1
};
ComponentLabels labelComponents(const std::vector<int> &components, const std::vector<int> &labels);
// Region IDs may be sparse; caller supplies their ordered IDs. Ignores other labels.
RegionGraph regionAdjacency(const std::vector<int> &labels, int width, int height,
							const std::vector<int> &regionIds, bool wrap);
} // namespace MapGeneration
