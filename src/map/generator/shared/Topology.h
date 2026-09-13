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
// Region IDs may be sparse; caller supplies their ordered IDs. Ignores other labels.
RegionGraph regionAdjacency(const std::vector<int> &labels, int width, int height,
							const std::vector<int> &regionIds, bool wrap);
} // namespace MapGeneration
