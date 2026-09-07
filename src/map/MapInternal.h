// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Private shared definitions for the Map.cpp family of translation units.
// Not intended for inclusion outside Map*.cpp.

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

// Pathfinding gradients are Uint16 fields. A goal cell holds GRADIENT_AT_GOAL and every
// other reachable cell GRADIENT_AT_GOAL - cost, where cost is the cheapest path to a goal
// in tenths of a land step: GRADIENT_STEP per cardinal step, GRADIENT_DIAGONAL_STEP per
// diagonal step (octile distance), and water at the rate of the unit's swim class. A unit
// walks toward the neighbour whose value minus the step to reach it is highest.
//   GRADIENT_FORBIDDEN        (0): obstacle / impassable — never enter.
//   GRADIENT_UNREACHABLE      (1): reachable cell with no path to any goal.
//   GRADIENT_FORBIDDEN_BORDER    : forbidden-zone interior cell that borders a free cell;
//                                  seeded one step below the goal so the forbidden gradient
//                                  tapers into the forbidden zone.
//   GRADIENT_AT_GOAL     (0xFFFF): goal cell itself.
// The AIs' own Uint8 helper maps (Map::updateGlobalGradient(Uint8*)) use the same 0 / 1 /
// max-of-type sentinels with one unit per step.
constexpr int GRADIENT_STEP          = 10;
constexpr int GRADIENT_DIAGONAL_STEP = 14;
constexpr std::uint16_t GRADIENT_FORBIDDEN        = 0;
constexpr std::uint16_t GRADIENT_UNREACHABLE      = 1;
constexpr std::uint16_t GRADIENT_AT_GOAL          = 0xFFFF;
constexpr std::uint16_t GRADIENT_FORBIDDEN_BORDER = GRADIENT_AT_GOAL - GRADIENT_STEP;

// Distance to the goal in whole tiles, for a reachable gradient value.
inline int gradientTiles(std::uint16_t g)
{
	return (GRADIENT_AT_GOAL - g + GRADIENT_STEP / 2) / GRADIENT_STEP;
}

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

// Corn growth probability denominator: corn grows on 1-in-CORN_GROWTH_DIVISOR
// random rolls. Comment in Map::growResources says "Growth rate of corn is 1/3".
constexpr int CORN_GROWTH_DIVISOR = 3;

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

