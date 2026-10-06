// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// Q8 factors use 256 for one. Health is signed HP per tick in the same scale.
// Keep presentation out of this table: hot simulation queries only need these
// immutable fixed-layout values. TerrainGroup.h supplies the compile-time
// built-in profiles; runtime registries preserve this layout. Resource masks use
// the resource wire IDs.
struct TerrainProperties
{
	bool walkable = false, swimmable = false, flyable = true;
	bool resourcesGrow = false, fertilitySource = false, nonGrowingResources = false;
	bool buildable = false, projectileBlocks = false, shoreline = false;
	std::uint16_t groundSpeedQ8 = 256, airSpeedQ8 = 256;
	std::int16_t groundHealthQ8 = 0, airHealthQ8 = 0;
	std::uint16_t growthQ8 = 256;
	std::int16_t fertilityQ8 = 0;
	std::uint16_t inhibitionQ8 = 0, shoreSupportQ8 = 0;
	std::uint16_t allowedResources = 0;
	std::uint8_t farmCrop = 255;
};

// Irrigation heuristics need a beneficial source, not merely an enabled
// (possibly zero or negative) contribution to the signed ecology field.
constexpr bool terrainProvidesFertility(const TerrainProperties& p)
{
    return p.fertilitySource && p.fertilityQ8>0;
}

constexpr bool validTerrainProperties(const TerrainProperties& p)
{
	return !(p.walkable && p.swimmable) && (p.allowedResources & ~0xffu) == 0 &&
		p.groundSpeedQ8 >= 64 &&
		p.groundSpeedQ8 <= 1024 && p.airSpeedQ8 >= 64 && p.airSpeedQ8 <= 1024 &&
		p.growthQ8 <= 1024 && p.inhibitionQ8 <= 1024 && p.shoreSupportQ8 <= 1024 &&
		p.fertilityQ8 >= -1024 && p.fertilityQ8 <= 1024 &&
		(p.farmCrop == 255 || (p.farmCrop < 8 && (p.allowedResources & (1u << p.farmCrop))));
}

// Field-wise comparison; struct padding is neither portable nor comparable.
constexpr bool sameTerrainProperties(const TerrainProperties& a, const TerrainProperties& b)
{
	return a.walkable == b.walkable && a.swimmable == b.swimmable && a.flyable == b.flyable &&
		a.resourcesGrow == b.resourcesGrow && a.fertilitySource == b.fertilitySource &&
		a.nonGrowingResources == b.nonGrowingResources && a.buildable == b.buildable &&
		a.projectileBlocks == b.projectileBlocks && a.shoreline == b.shoreline &&
		a.groundSpeedQ8 == b.groundSpeedQ8 && a.airSpeedQ8 == b.airSpeedQ8 &&
		a.groundHealthQ8 == b.groundHealthQ8 && a.airHealthQ8 == b.airHealthQ8 &&
		a.growthQ8 == b.growthQ8 && a.fertilityQ8 == b.fertilityQ8 &&
		a.inhibitionQ8 == b.inhibitionQ8 && a.shoreSupportQ8 == b.shoreSupportQ8 &&
		a.allowedResources == b.allowedResources && a.farmCrop == b.farmCrop;
}
