// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Regions.h"
#include <string>
namespace MapGeneration
{
// The home mask constrains the whole building footprint and worker positions, not
// just the building anchor. Resources remain a generator-owned step before or after this.
// Mutates only the disposable candidate; any failure requires discarding it.
bool placeSettlement(Game &, GenerationContext &, int team, const std::vector<unsigned char> &home,
					 MapGeneratorPoint preferredAnchor, const std::string &stream = "settlements");

/// A defence tower a colony starts with: the completed tower of `level` (0 to 2, shooting 5, 7 or 9
/// tiles) whose 2x2 footprint lies wholly on `allowed` ground the tower can stand on, nearest the
/// point (x, y) within `within` tiles of it, stocked with as many bullets as it holds (unless
/// `stocked` is false) so it defends from the first tick. Nearest is measured from the footprint's middle, and ties go to the first
/// footprint found in row order, so a site designed in a wedge lands the same way for every colony.
/// Returns the footprint's top-left tile, or -1 when no footprint fits.
int placeTower(Game &, int team, int level, double x, double y, int within,
			   const std::vector<unsigned char> &allowed, bool stocked = true);
} // namespace MapGeneration
