// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Settings.h"
#include <ScrollPhysics.h>

// Copies the touch scroll preferences into the physics presets; libgag cannot
// read Settings. Call after loading preferences and whenever a slider or the
// reduced-motion toggle changes.
inline void applyScrollTuning(const Settings &settings, bool reducedMotion)
{
	auto &tuning = GAGCore::scrollTuning();
	tuning.momentum = settings.touchScrollMomentum;
	tuning.bounce = settings.touchScrollBounce;
	tuning.mapMomentum = settings.mapScrollMomentum;
	tuning.reducedMotion = reducedMotion;
}
