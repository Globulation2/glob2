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
	// Derived, never authored: the cell mixes walking and swimming corners.
	// Algae regrows near coasts (FertilityField's shore growth field).
	bool coast = false;
	std::uint16_t groundSpeedQ8 = 256, airSpeedQ8 = 256;
	std::int16_t groundHealthQ8 = 0, airHealthQ8 = 0;
	std::uint16_t growthQ8 = 256;
	std::int16_t fertilityQ8 = 0;
	std::uint16_t inhibitionQ8 = 0;
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
		p.growthQ8 <= 1024 && p.inhibitionQ8 <= 1024 &&
		p.fertilityQ8 >= -1024 && p.fertilityQ8 <= 1024 &&
		(p.farmMaterial == 255 || p.farmMaterial < MaterialCount);
}

// Field-wise comparison; struct padding is neither portable nor comparable.
constexpr bool sameTerrainProperties(const TerrainProperties& a, const TerrainProperties& b)
{
	return a.walkable == b.walkable && a.swimmable == b.swimmable && a.flyable == b.flyable &&
		a.resourcesGrow == b.resourcesGrow && a.fertilitySource == b.fertilitySource &&
		a.nonGrowingResources == b.nonGrowingResources && a.buildable == b.buildable &&
		a.projectileBlocks == b.projectileBlocks && a.shoreline == b.shoreline && a.coast == b.coast &&
		a.groundSpeedQ8 == b.groundSpeedQ8 && a.airSpeedQ8 == b.airSpeedQ8 &&
		a.groundHealthQ8 == b.groundHealthQ8 && a.airHealthQ8 == b.airHealthQ8 &&
		a.growthQ8 == b.growthQ8 && a.fertilityQ8 == b.fertilityQ8 &&
		a.inhibitionQ8 == b.inhibitionQ8 &&
		a.allowedResources == b.allowedResources && a.farmMaterial == b.farmMaterial;
}

// A cell's rules come from its four corner terrains. Corners with equal
// profiles keep that exact profile, so members of one terrain group still
// combine into their group's mechanic. Otherwise the cell is a transition:
// two or more blocked corners block ground movement. Otherwise the more common
// walking/swimming category wins, with walking winning their tie. Every numeric
// modifier uses the arithmetic mean of all four corners, independently of that
// vote. Equal corner weights give speed, health and ecology gradual transitions
// at 1:3, 2:2 and 3:1 boundaries. Q8 averages round toward zero. Transitions are
// never buildable; permissions still require all corners and blockers any corner.
// A transition with both walking and swimming corners is a shoreline, as if a
// beach stood there: grass straight against water hosts shore resources, not
// land crops, whichever way its movement vote went. It is also a coast, and
// algae regrows only near coasts, beach or bare bank alike.
constexpr TerrainProperties combineCornerRules(const TerrainProperties& a, const TerrainProperties& b,
	const TerrainProperties& c, const TerrainProperties& d)
{
	const TerrainProperties* corners[] = {&a, &b, &c, &d};
	bool uniform = true;
	unsigned walking = 0, swimming = 0, blocked = 0;
	for (const auto* p : corners)
	{
		uniform = uniform && sameTerrainProperties(*p, a);
		if (p->walkable) ++walking;
		else if (p->swimmable) ++swimming;
		else ++blocked;
	}
	if (uniform) return a;
	TerrainProperties result;
	const bool coast = walking > 0 && swimming > 0;
	result.walkable = blocked < 2 && walking >= swimming;
	result.swimmable = blocked < 2 && swimming > walking;
	result.buildable = false;
	result.projectileBlocks = false;
	result.shoreline = result.coast = coast;
	result.flyable = result.resourcesGrow = result.nonGrowingResources = result.fertilitySource = true;
	result.farmMaterial = 255;
	// Promote every term before adding: authored signed health rates can span
	// the full int16 range. Integer division rounds signed fractions toward zero;
	// the result remains within the range of the four inputs.
	const auto mean = [&](auto member)
	{
		return (std::int32_t(a.*member) + std::int32_t(b.*member) +
			std::int32_t(c.*member) + std::int32_t(d.*member)) / 4;
	};
	result.groundSpeedQ8 = mean(&TerrainProperties::groundSpeedQ8);
	result.airSpeedQ8 = mean(&TerrainProperties::airSpeedQ8);
	result.groundHealthQ8 = mean(&TerrainProperties::groundHealthQ8);
	result.airHealthQ8 = mean(&TerrainProperties::airHealthQ8);
	result.growthQ8 = mean(&TerrainProperties::growthQ8);
	result.fertilityQ8 = mean(&TerrainProperties::fertilityQ8);
	result.inhibitionQ8 = mean(&TerrainProperties::inhibitionQ8);
	result.allowedResources = 0xff;
	for (const auto* p : corners)
	{
		result.flyable = result.flyable && p->flyable;
		result.resourcesGrow = result.resourcesGrow && p->resourcesGrow;
		result.nonGrowingResources = result.nonGrowingResources && p->nonGrowingResources;
		result.fertilitySource = result.fertilitySource && p->fertilitySource;
		result.projectileBlocks = result.projectileBlocks || p->projectileBlocks;
		result.shoreline = result.shoreline || p->shoreline;
		result.allowedResources = std::uint16_t(result.allowedResources & p->allowedResources);
	}
	return result;
}
