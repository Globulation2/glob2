// SPDX-License-Identifier: GPL-3.0-or-later
// Compile-time checks of the built-in terrain tables. They run here once rather
// than in every translation unit that includes TerrainProperties.h (the pair
// check alone evaluates TERRAIN_COUNT squared combinations).
#include "TerrainProperties.h"
#include "field/GradientBucket.h"
#include "field/TerrainHazardCost.h"
#include <algorithm>
#include "TerrainBuiltins.h"

static_assert([] { for (const auto& p : TERRAIN_PROPERTIES) if (!validTerrainProperties(p)) return false; return true; }());
// Uniform terrain profiles remain unchanged.
static_assert(TERRAIN_PROPERTIES[ICE].groundSpeedQ8 == 128 && TERRAIN_PROPERTIES[ICE].groundHealthQ8 == -8 &&
	TERRAIN_PROPERTIES[TRAIL].groundSpeedQ8 == 512 && TERRAIN_PROPERTIES[TRAIL].buildable &&
	TERRAIN_PROPERTIES[SAND].inhibitionQ8 == 256 && TERRAIN_PROPERTIES[SAND].shoreSupportQ8 == 256 &&
	TERRAIN_PROPERTIES[WATER].swimmable && TERRAIN_PROPERTIES[WATER].fertilityQ8 == 256 &&
	TERRAIN_PROPERTIES[GRASS].buildable && TERRAIN_PROPERTIES[GRASS].farmMaterial == materialIndex(MaterialId::Food));

// Uniform built-ins keep their profiles; mixed shores now blend numeric
// ecology modifiers rather than inheriting the retired fixed shore profile.
static_assert([] {
	const auto& g = TERRAIN_PROPERTIES[GRASS];
	const auto& s = TERRAIN_PROPERTIES[SAND];
	const auto& w = TERRAIN_PROPERTIES[WATER];
	const auto beach = combineCornerRules(s, s, w, w);
	const auto gardenEdge = combineCornerRules(g, g, s, s);
	return beach.walkable && !beach.swimmable && !beach.buildable && beach.shoreline &&
		beach.inhibitionQ8 == 128 && beach.shoreSupportQ8 == 128 && beach.fertilityQ8 == 128 &&
		gardenEdge.inhibitionQ8 == 128 && gardenEdge.shoreSupportQ8 == 128;
}());
// Grass directly against water is legal; an even split favors walking.
static_assert(combineCornerRules(TERRAIN_PROPERTIES[GRASS], TERRAIN_PROPERTIES[GRASS],
	TERRAIN_PROPERTIES[WATER], TERRAIN_PROPERTIES[WATER]).walkable);
static_assert([] {
	for (unsigned a = 0; a < TERRAIN_COUNT; ++a)
		for (unsigned b = 0; b < TERRAIN_COUNT; ++b)
			if (!validTerrainProperties(combineCornerRules(TERRAIN_PROPERTIES[a], TERRAIN_PROPERTIES[a],
														   TERRAIN_PROPERTIES[b], TERRAIN_PROPERTIES[b])))
				return false;
	return true;
}(), "Every built-in pair must combine into valid rules");

// The bucket ring must exceed every built-in terrain edge (see GradientBucket::COUNT).
static_assert(GradientBucket::COUNT == [] {
        unsigned largest=GRADIENT_SLOWEST_SWIM_STEP*GRADIENT_DIAGONAL_STEP/GRADIENT_STEP;
        for(const auto& p:TERRAIN_PROPERTIES)
        {
            const unsigned base=p.swimmable?GRADIENT_SLOWEST_SWIM_STEP:GRADIENT_STEP;
            const unsigned cardinal=gradient_kernel::hazardRouteCost(std::max(1u,(base*256+p.groundSpeedQ8/2)/p.groundSpeedQ8),p.groundHealthQ8);
            largest=std::max(largest,cardinal*GRADIENT_DIAGONAL_STEP/GRADIENT_STEP);
        }
        unsigned count=1;
        while(count<=largest) count*=2;
        return count;
    }(), "GradientBucket::COUNT must be the power of two above the largest built-in terrain edge");
