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
} // namespace MapGeneration
