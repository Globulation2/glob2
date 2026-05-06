// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include <optional>

// Pure scoring helpers extracted from Map::doesUnitTouchEnemy. Smaller score
// = more attractive target. Both functions are total over their primitive
// inputs — no Map / Game / Building / Unit / Team state is touched, which
// makes them trivial to unit-test.
namespace map_query {

// Sentinel: "no enemy candidate found yet". Any score returned by the
// helpers below is strictly less than this.
inline constexpr int kNoEnemyScore = 256;

// Score an enemy building seen at a neighbor tile.
//   defaultUnitStayRange = true  → nullopt (warriors don't target it)
//   hasShootingRange     = true  → 0    (turret-class — top priority)
//   otherwise                    → 255  (last-resort target)
inline std::optional<int> scoreEnemyBuilding(bool defaultUnitStayRange, bool hasShootingRange)
{
	if (defaultUnitStayRange)
		return std::nullopt;
	return hasShootingRange ? 0 : 255;
}

// Score an enemy unit seen at a neighbor tile.
//   delta = sub-tile crossing fraction (0..255). Higher = closer to crossing
//   into the next tile, i.e. reachable sooner.
//   speed must be > 0.
inline int scoreEnemyUnit(int delta, int speed)
{
	return (256 - delta) / speed;
}

}
