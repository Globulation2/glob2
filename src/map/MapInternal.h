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
// use tabMiniFar for all miniGrad far points
extern const int tabFar[16][2];

// helper to fill vectors
template <typename T>
inline void fill(std::vector<T>& vec, const T& value) {
	std::fill(vec.begin(), vec.end(), value);
}

// Local working grid: a building's local gradient and the clearing-flag local-resources
// gradient both live in a 32x32 buffer centered on the building. Index = y << SHIFT | x.
constexpr int LOCAL_GRID_W      = 32;
constexpr int LOCAL_GRID_SHIFT  = 5;                            // 1 << SHIFT == W
constexpr int LOCAL_GRID_AREA   = LOCAL_GRID_W * LOCAL_GRID_W;  // 1024
constexpr int LOCAL_GRID_CENTER = LOCAL_GRID_W / 2 - 1;         // 15
static_assert(1 << LOCAL_GRID_SHIFT == LOCAL_GRID_W);

// Helper for updateLocalGradient and the local-gradient pathfinders.
inline int clip_0_31(int x) { return (x < 0) ? 0 : (x > LOCAL_GRID_W - 1) ? LOCAL_GRID_W - 1 : x; }

// Gradient sentinel values. Gradients propagate from goal cells (set to GRADIENT_AT_GOAL)
// outward, decreasing by 1 per step. A unit at (x, y) walks toward whichever neighbor has
// the highest gradient value.
//   GRADIENT_FORBIDDEN        (0): obstacle / impassable — never enter.
//   GRADIENT_UNREACHABLE      (1): reachable cell with no path to any goal yet.
//   GRADIENT_FORBIDDEN_BORDER (254): forbidden-zone interior cell that borders a free cell;
//                                    used as a fade-in source for the forbidden gradient so
//                                    the gradient tapers into the forbidden zone.
//   GRADIENT_AT_GOAL          (255): goal cell itself; distance to goal is GRADIENT_AT_GOAL - g.
constexpr std::uint8_t GRADIENT_FORBIDDEN        = 0;
constexpr std::uint8_t GRADIENT_UNREACHABLE      = 1;
constexpr std::uint8_t GRADIENT_FORBIDDEN_BORDER = 254;
constexpr std::uint8_t GRADIENT_AT_GOAL          = 255;

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

// Initial Ressource::amount when a fresh resource is seeded onto a tile.
constexpr int RESSOURCE_INITIAL_AMOUNT = 1;

// Corn growth probability denominator: corn grows on 1-in-CORN_GROWTH_DIVISOR
// random rolls. Comment in Map::growRessources says "Growth rate of corn is 1/3".
constexpr int CORN_GROWTH_DIVISOR = 3;

// Chamfer-dilate a LOCAL_GRID_W * LOCAL_GRID_W gradient buffer in-place. Each free cell is
// raised to max(self, max(neighbor) - 1); 0 (obstacle) and 255 (source) are preserved.
// Used by both Map::updateLocalGradient and Map::updateLocalRessources.
void propagateLocalGradient32(std::uint8_t* gradient);

// Spiral outward from (startX, startY) for `steps` cells in each of E, S, W, N (in order),
// returning true on the first non-zero gradient cell encountered. The grid stride is
// (1 << wDec) and x/y wrap modulo (wMask + 1) and (hMask + 1) — both must be powers of two.
// Used to test reachability of building footprints in both the toroidal full map and the
// 32x32 local grid (the local grid never wraps in practice, since spirals are short).
inline bool spiralFindNonZero(const std::uint8_t* gradient, int startX, int startY, int steps,
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

