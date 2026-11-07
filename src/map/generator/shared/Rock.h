// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "Sketch.h"
#include <string>
#include <vector>
class Map;
namespace MapGeneration
{
/// Rock terrain for walls a design draws as tiles (ranges, bluffs, mesas, crater rims): a vertex is
/// rock only where all four tiles touching it are rock tiles. A tile takes its rules from its four
/// corners and two blocked corners block it, so the blocked region is then the drawn tiles (a
/// straight edge's outer row has two rock corners, the row beyond it none) less the wall's convex
/// corner tiles, which keep one rock corner and walk; a tile left out of the wall (a quarry face)
/// keeps four open corners. Walls narrower than two tiles leave no rock vertex at all: draw cores at
/// least two tiles thick.
std::vector<unsigned char> rockVertices(const Torus &, const std::vector<unsigned char> &rockTiles);

/// The wall tiles of a quarry face near `centre`: tiles of `wall` within `radius` (Chebyshev, on
/// the torus) of `centre` and within `depth` steps of a tile `open` allows. Mark them as the
/// quarry (stone on grass) and leave them out of the rock. Stone stands only on pure grass, so the
/// ring before the face must stay grass: see quarryApproach.
std::vector<int> quarryNear(const Torus &, const std::vector<unsigned char> &wall,
							const std::vector<unsigned char> &open, int centre, int radius, int depth);

/// The tiles round a quarry that must keep plain grass corners (a tile's corners are the vertices of
/// itself and the tiles right and below it, so any other terrain beside a quarry tile spoils it).
std::vector<unsigned char> quarryApproach(const Torus &, const std::vector<unsigned char> &quarry);

/// A talus apron round rock: tiles not in `rock` within `inner` steps of it, and within `outer`
/// steps where `fray` (0 to 65535, smooth noise) passes `cut`, that `eligible` allows. Paint it as
/// rough ground (SCREE): slow, unbuildable, nothing grows, and it does not inhibit growth.
std::vector<unsigned char> talusApron(const Torus &, const std::vector<unsigned char> &rock,
									  const std::vector<unsigned char> &eligible,
									  const std::vector<int> &fray, int inner, int outer, int cut);

/// The water vertices at least `steps` four-connected steps from every non-water vertex: the core
/// to paint as deep water (slower swimming, no algae), leaving a shallow margin of plain water so
/// beaches, algae and ford checks keep working. `steps` below 2 is raised to 2.
std::vector<unsigned char> deepCore(const Torus &, const std::vector<unsigned char> &water, int steps);

/// A finished map's quarries: every tile of `quarry` that supports stone holds a permanent stone
/// deposit, and the faces owned by colonies (`owner` >= 0) hold the same stone each, at least
/// `least`. Returns the first fault, or "".
std::string quarryFault(const Map &, const Torus &, const std::vector<unsigned char> &quarry,
						const std::vector<int> &owner, int colonies, int least);
} // namespace MapGeneration
