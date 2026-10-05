// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "UnitConsts.h"

// Per-tick delta advance. Only diagonal travel takes longer; work and attacks
// keep their action speed even when the unit faces a diagonal direction.
// 181/256 approximates 1/sqrt(2), rounded down with integer arithmetic so all
// peers use the same result. Keep this quantization for replay compatibility.
inline constexpr int unitActionStepSpeed(int speed, int action, int dx, int dy)
{
	if (dx != 0 && dy != 0 && (action == WALK || action == SWIM || action == FLY))
		return (speed * 181) >> 8;
	return speed;
}

// Movement advances at most one cell per tick. Keep the action phase byte in
// range, and retain enough progress for diagonal movement at the slowest rate.
// A zero-displacement action retains its normal cadence on every terrain.
// Shipped movement speeds are <=30, so the supported 4x factor never saturates;
// unusually high imported/custom speeds retain the one-cell-per-tick bound.
inline constexpr int unitTerrainMovementSpeed(int speed, unsigned multiplierQ8, bool moving = true)
{
    if (!moving || multiplierQ8 == 256 || speed <= 0) return speed;
    const auto scaled = (static_cast<unsigned long long>(speed) * multiplierQ8) / 256;
    return scaled < 2 ? 2 : scaled > UNIT_DELTA_MAX ? UNIT_DELTA_MAX : int(scaled);
}
