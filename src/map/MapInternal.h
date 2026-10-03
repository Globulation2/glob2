// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Private shared definitions for Map, gradient and pathfinding implementation
// files. Not part of the public Map interface.

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

#define UPDATE_MAX(max,value) { if (value>(max)) (max)=value; }

// use deltaOne for first perpendicular direction
extern const int deltaOne[8][2];
// use tabClose for original circular direction
extern const int tabClose[8][2];

// helper to fill vectors
template <typename T>
inline void fill(std::vector<T>& vec, const T& value) {
	std::fill(vec.begin(), vec.end(), value);
}

#include "field/GradientConstants.h"

// Guard-area balancing (the "guard-area-balancing" experiment). A painted guard
// tile is seeded GUARD_CROWD_COST_PER_WARRIOR below the goal for each of the
// team's warriors within GUARD_CROWD_RADIUS tiles (Chebyshev), scaled by
// GUARD_CROWD_REFERENCE_AREA over the painted tiles within the same radius and
// capped at GUARD_CROWD_COST_MAX. A warrior on the paint follows the field out of
// an over-full area one action in 2^GUARD_LEAVE_CHANCE_SHIFT. Mechanism and
// tuning: docs/features/guard-area-balancing.md.
constexpr int GUARD_CROWD_RADIUS           = 8;
constexpr int GUARD_CROWD_COST_PER_WARRIOR = 4 * GRADIENT_STEP;
constexpr int GUARD_CROWD_REFERENCE_AREA   = 25;
constexpr int GUARD_CROWD_COST_MAX         = 400 * GRADIENT_STEP;
constexpr int GUARD_LEAVE_CHANCE_SHIFT     = 6;
// Seeds stay above the unreachable sentinel.
static_assert(GUARD_CROWD_COST_MAX < GRADIENT_AT_GOAL - GRADIENT_UNREACHABLE - 1);
// A leaver keeps counting toward its area for longer than the crowding it freed
// is worth, so it does not turn back.
static_assert(GUARD_CROWD_RADIUS * GRADIENT_STEP > GUARD_CROWD_COST_PER_WARRIOR);

// Sentinel for Map::immobileUnits[]: byte stores the team number of the immobile
// unit on the tile, or IMMOBILE_UNIT_NONE if no immobile unit is present.
// Team::MAX_COUNT is well under 255, so the team-number range never collides.
constexpr std::uint8_t IMMOBILE_UNIT_NONE = 255;

// Map::doesUnitTouchEnemy scoring sentinels. The "bestTime" is in 0..255 for any
// real candidate; 256 acts as a "no candidate yet" sentinel above the valid range.
//   ENEMY_TOUCH_BEST_TIME_NONE        (256): initial value / "no candidate".
//   ENEMY_TOUCH_SCORE_SHOOTER         (0)  : highest priority — turret/shooter found.
//   ENEMY_TOUCH_SCORE_BUILDING_FALLBACK(255): non-shooter enemy building fallback.
constexpr int ENEMY_TOUCH_BEST_TIME_NONE         = 256;
constexpr int ENEMY_TOUCH_SCORE_SHOOTER          = 0;
constexpr int ENEMY_TOUCH_SCORE_BUILDING_FALLBACK = 255;

// exploredArea[team][] cell values. The byte counts down each tick (in MapStep);
// EXPLORED_FRESH is the max stamp written when a unit/building reveals a tile,
// EXPLORED_BY_BUILDING_MIN is the floor a stationary building keeps a tile at.
constexpr std::uint8_t EXPLORED_FRESH           = 255;
constexpr std::uint8_t EXPLORED_BY_BUILDING_MIN = 2;

// Saved games written at this VERSION_MINOR or later carry exploredArea; map
// files and older saves reseed it from the discovery map on load.
constexpr int EXPLORED_AREA_SAVED_VERSION_MINOR = 88;

// Initial Resource::amount when a fresh resource is seeded onto a tile.
constexpr int RESOURCE_INITIAL_AMOUNT = 1;

// Wheat growth probability denominator: wheat grows on 1-in-WHEAT_GROWTH_DIVISOR
// random rolls. Comment in Map::growResources says "Growth rate of wheat is 1/3".
constexpr int WHEAT_GROWTH_DIVISOR = 3;

// Spiral outward from (startX, startY) for `steps` cells in each of E, S, W, N (in order),
// returning true on the first non-zero gradient cell encountered. The grid stride is
// (1 << wDec) and x/y wrap modulo (wMask + 1) and (hMask + 1) — both must be powers of two.
// Used to test reachability of building footprints on the toroidal map.
inline bool spiralFindNonZero(const std::uint16_t* gradient, int startX, int startY, int steps,
                              int wMask, int hMask, int wDec)
{
	int x = startX, y = startY;
	static constexpr int dxs[4] = { 1, 0, -1, 0 };
	static constexpr int dys[4] = { 0, 1, 0, -1 };
	for (int ai = 0; ai < 4; ai++) {
		for (int mi = 0; mi < steps; mi++) {
			assert(x >= 0);
			assert(y >= 0);
			if (gradient[(y << wDec) | x] != 0)
				return true;
			x = (x + dxs[ai]) & wMask;
			y = (y + dys[ai]) & hMask;
		}
	}
	return false;
}

