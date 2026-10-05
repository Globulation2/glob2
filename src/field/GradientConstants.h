// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// Pathfinding gradients are Uint16 fields. A goal cell holds GRADIENT_AT_GOAL and every
// other reachable cell GRADIENT_AT_GOAL - cost, where cost is the cheapest path to a goal
// in tenths of a land step: GRADIENT_STEP per cardinal step, GRADIENT_DIAGONAL_STEP per
// diagonal step (octile distance), and water at the rate of the unit's swim class. A unit
// walks toward the neighbour whose value minus the step to reach it is highest.
//   GRADIENT_FORBIDDEN        (0): obstacle / impassable; never enter.
//   GRADIENT_UNREACHABLE      (1): passable cell with no known path to a goal.
//   GRADIENT_FORBIDDEN_BORDER    : forbidden-zone interior cell that borders a free cell;
//                                  seeded one step below the goal so the forbidden gradient
//                                  tapers into the forbidden zone.
//   GRADIENT_AT_GOAL     (0xFFFF): goal cell itself.
// The AIs' own Uint8 helper maps (Map::updateGlobalGradient(Uint8*)) use the same 0 / 1 /
// max-of-type sentinels with one unit per step.
constexpr int GRADIENT_STEP          = 10;
constexpr int GRADIENT_DIAGONAL_STEP = 14;
constexpr int GRADIENT_SLOWEST_SWIM_STEP = 30;
constexpr std::uint16_t GRADIENT_FORBIDDEN        = 0;
constexpr std::uint16_t GRADIENT_UNREACHABLE      = 1;
constexpr std::uint16_t GRADIENT_AT_GOAL          = 0xFFFF;
constexpr std::uint16_t GRADIENT_FORBIDDEN_BORDER = GRADIENT_AT_GOAL - GRADIENT_STEP;

// Weighted cost rounded to whole land-step equivalents, for a reachable value.
// This is not a geometric tile count: water and diagonal steps change the cost.
inline int gradientTiles(std::uint16_t g)
{
	return (GRADIENT_AT_GOAL - g + GRADIENT_STEP / 2) / GRADIENT_STEP;
}
