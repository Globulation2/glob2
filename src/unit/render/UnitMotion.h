// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "UnitConsts.h"
#include "UnitTiming.h"
#include "render/scene/Scene.h"

#include <algorithm>

// Smooth unit motion (the experimental unitInterpolation graphics setting).
// The simulation advances a unit's delta by stepSpeed once per tick, and the
// drawn position and the animation frame (delta >> 3, 32 frames) follow delta.
// Between ticks, drawing can advance delta by the fraction of the tick interval
// that has passed since the drawn PresentationFrame's tick. It stops at the end of the
// current action, so the next tick never draws behind the previous frame while
// the unit keeps going. Presentation only: the simulation never sees it.

//! Fraction of the tick interval elapsed at `now` since the scene's tick, 0..1;
//! 0 when the simulation runs uncapped.
inline float unitMotionFraction(const PresentationFrame &scene, Uint64 now)
{
	if (scene.tickInterval == 0 || now <= scene.tickTime)
		return 0.f;
	return std::min(1.f, float(now - scene.tickTime) / float(scene.tickInterval));
}

//! The delta to draw for a unit; its own delta when motion is 0.
inline int drawnUnitDelta(const SnapshotUnit &unit, float motion)
{
	if (motion <= 0.f)
		return unit.delta;
	return std::min(UNIT_DELTA_MAX, unit.delta + int(motion * float(unitActionStepSpeed(unit.speed,unit.action,unit.dx,unit.dy))));
}
