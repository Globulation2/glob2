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
