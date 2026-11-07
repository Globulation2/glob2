// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TerrainPropertiesLayout.h"

// Fertility sources split into two kinds that AI heuristics must not confuse.
// Irrigating water (Water, Deep water and their blends) is a swimmable source:
// unbuildable, and it waters a wide crop belt that buildings should leave free.
// Fertile ground (Loam, Moss, Spring meadow) is walkable, buildable crop land
// whose contribution is much weaker (fertilityQ8 48 against water's 256). Judge
// its crops by the engine's fertility field, not by a distance to it.
//
// On maps without Fertile ground every fertility source is irrigating water, so
// heuristics that switch from terrainProvidesFertility to terrainIsIrrigatingWater
// behave identically there.
constexpr bool terrainIsIrrigatingWater(const TerrainProperties& p)
{
	return terrainProvidesFertility(p) && p.swimmable;
}

constexpr bool terrainIsFertileGround(const TerrainProperties& p)
{
	return terrainProvidesFertility(p) && !p.swimmable;
}
