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

