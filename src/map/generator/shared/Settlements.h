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
/// When `cover` is supplied, every listed tile must be within the actual building
/// type's square firing range from some tile of the candidate footprint. This filters
/// candidates before choosing the nearest, so rounding cannot leave a required gate
/// uncovered. Empty `cover` preserves the ordinary unconstrained placement policy.
/// `supplyStone` also fills the tower's reserve, independently of its magazine.
/// A loaded magazine alone still recruits a worker to fill an empty stone store;
/// maps with several starting towers can opt in to avoid consuming their opening
/// workforce before an inn is stocked. The default retains existing maps' economy.
/// Returns the footprint's top-left tile, or -1 when no footprint fits.
int placeTower(Game &, int team, int level, double x, double y, int within,
			   const std::vector<unsigned char> &allowed, bool stocked = true,
			   const std::vector<MapGeneratorPoint> &cover = {}, bool supplyStone = false);
/// Place a completed starting building using the same bounded footprint search as
/// placeTower. Only the explicitly listed resource kinds start at their type's
/// storage capacity (e.g. wheat in an inn, leaving fruit as a contested prize).
/// Registers the colony's service lists immediately; stock is finite, with no
/// special refill rule. Returns its top-left tile, or -1 without placement if it
/// cannot fit. Intended for a new-world setup, before units have active tasks.
int placeStartingBuilding(Game &, int team, const char *name, int level, double x, double y,
						  int within, const std::vector<unsigned char> &allowed,
						  const std::vector<int> &supplies = {});
} // namespace MapGeneration
