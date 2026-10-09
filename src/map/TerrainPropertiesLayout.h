// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "resource/Material.h"
#include <cstdint>

// Q8 factors use 256 for one. Health is signed HP per tick in the same scale.
// Keep presentation out of this table: hot simulation queries only need these
// immutable fixed-layout values. TerrainGroup.h supplies the compile-time
// built-in profiles; runtime registries preserve this layout. The legacy resource
// mask is retained only for import adapters.
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
	std::uint8_t farmMaterial = 255;
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
		(p.farmMaterial == 255 || p.farmMaterial < MaterialCount);
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
		a.allowedResources == b.allowedResources && a.farmMaterial == b.farmMaterial;
}

// A cell's rules come from its four corner terrains. Corners with equal
// profiles keep that exact profile, so members of one terrain group still
// combine into their group's mechanic. Otherwise the cell is a transition:
// walkable when any corner is, never swimmable or buildable, and otherwise as
// permissive as its weakest corner. Ground speed and health only consider the
// walkable corners (all of them when none is walkable), since a unit crossing
// the cell walks on those. Grass with sand, or sand with water, therefore gives
// the classic shore: walkable, a shoreline, and nothing else.
constexpr TerrainProperties combineCornerRules(const TerrainProperties& a, const TerrainProperties& b,
	const TerrainProperties& c, const TerrainProperties& d)
{
	const TerrainProperties* corners[] = {&a, &b, &c, &d};
	bool uniform = true;
	bool anyWalkable = false;
	for (const auto* p : corners)
	{
		uniform = uniform && sameTerrainProperties(*p, a);
		anyWalkable = anyWalkable || p->walkable;
	}
	if (uniform) return a;
	TerrainProperties result;
	result.walkable = anyWalkable;
	result.swimmable = result.buildable = false;
	result.projectileBlocks = result.shoreline = false;
	result.flyable = result.resourcesGrow = result.nonGrowingResources = result.fertilitySource = true;
	result.farmMaterial = 255;
	result.groundSpeedQ8 = result.airSpeedQ8 = 1024;
	result.groundHealthQ8 = result.airHealthQ8 = 1024;
	result.growthQ8 = result.inhibitionQ8 = result.shoreSupportQ8 = 1024;
	result.fertilityQ8 = 1024;
	result.allowedResources = 0xff;
	auto lower = [](auto& value, auto candidate) { if (candidate < value) value = candidate; };
	for (const auto* p : corners)
	{
		result.flyable = result.flyable && p->flyable;
		result.resourcesGrow = result.resourcesGrow && p->resourcesGrow;
		result.nonGrowingResources = result.nonGrowingResources && p->nonGrowingResources;
		result.fertilitySource = result.fertilitySource && p->fertilitySource;
		result.projectileBlocks = result.projectileBlocks || p->projectileBlocks;
		result.shoreline = result.shoreline || p->shoreline;
		if (p->walkable || !anyWalkable)
		{
			lower(result.groundSpeedQ8, p->groundSpeedQ8);
			lower(result.groundHealthQ8, p->groundHealthQ8);
		}
		lower(result.airSpeedQ8, p->airSpeedQ8);
		lower(result.airHealthQ8, p->airHealthQ8);
		lower(result.growthQ8, p->growthQ8);
		lower(result.fertilityQ8, p->fertilityQ8);
		lower(result.inhibitionQ8, p->inhibitionQ8);
		lower(result.shoreSupportQ8, p->shoreSupportQ8);
		result.allowedResources = std::uint16_t(result.allowedResources & p->allowedResources);
	}
	return result;
}
