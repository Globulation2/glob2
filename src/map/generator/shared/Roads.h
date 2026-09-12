// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include <vector>
class Map;
namespace MapGeneration
{
/// The cheapest walk from any source tile to any goal tile, eight-connected on the torus:
/// stepping onto a `costly` tile costs one, any other open tile nothing, and `blocked` tiles
/// are impassable. Returns the tiles of the walk from the goal it reached back to its source, or
/// nothing when no goal can be reached. A 0-1 breadth-first search, so ties go to the walk found
/// first in neighbour order.
std::vector<int> cheapestRoute(const Torus &, const std::vector<int> &sources,
							   const std::vector<unsigned char> &goal,
							   const std::vector<unsigned char> &blocked,
							   const std::vector<unsigned char> &costly);

/// Deposits may land anywhere, and a band of them could close a colony off from where it must
/// be able to walk. This keeps one way open: the cheapest walk from the sources to the goal
/// (deposits cost one, open ground nothing; water, buildings and `alsoBlocked` are impassable),
/// with only the deposits on it cleared. Almost always nothing is in the way and nothing is
/// cleared. False when no walk exists at all.
bool openRoad(Map &, const Torus &, const std::vector<int> &sources,
			  const std::vector<unsigned char> &goal,
			  const std::vector<unsigned char> *alsoBlocked = nullptr);
} // namespace MapGeneration
