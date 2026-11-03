// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <RadialFade.h>
#include <algorithm>
#include <cmath>

// How a swimming unit sits in the water. The swim artwork is seen from above
// with the body lying flat, so the torso stays solid while the outstretched
// limbs fade into the water around it, and each stroke sends a ripple back
// across the surface.
// Presentation only: none of this reads or changes simulation state.

//! How far a swimmer's body reaches from its tile centre, in pixels at map
//! scale: nine tenths of the swim artwork's coverage lies within it.
constexpr float SWIM_REACH_WORKER = 11.f, SWIM_REACH_WARRIOR = 14.f;

//! The torso stays opaque within this fraction of the reach, and the limbs fade
//! to the floor opacity by the reach itself.
constexpr float SWIM_FADE_INNER = 0.4f, SWIM_FADE_FLOOR = 0.3f;

//! Fade for a swimmer of the given reach whose tile has its top-left corner at
//! (px, py).
inline GAGCore::RadialFade swimmerFade(float px, float py, float reach)
{
	return {px + 16, py + 16, reach * SWIM_FADE_INNER, reach, SWIM_FADE_FLOOR};
}

//! The swim clip makes one stroke per 32-phase animation cycle (the unit's
//! delta, as UnitAnimation.h reads it): the arms reach forward by phase 24,
//! sweep out and back from phase 28 and lie along the body by phase 6, then
//! the legs kick out from phase 14. The arm sweep is what pushes the swimmer
//! along, so a ring leaves the hands as it starts, races back and slows as it
//! fades, and is gone within this fraction of the stroke, before the kick.
constexpr int SWIM_STROKE_PHASE = 28;
constexpr float SWIM_RIPPLE_LIFE = 0.55f;
constexpr float SWIM_RIPPLE_INSET = 1.f, SWIM_RIPPLE_GROWTH = 12.f, SWIM_RIPPLE_ALPHA = 140.f;

struct SwimRipple
{
	float radius, alpha;
};

//! The ring around a swimmer of the given reach at an animation delta (0-255);
//! its alpha is zero between the end of one ring and the next stroke.
inline SwimRipple swimRipple(int delta, float reach)
{
	const int phase = ((delta >> 3) - SWIM_STROKE_PHASE + 32) & 31;
	const float age = (float(phase) + float(delta & 7) / 8.f) / 32.f / SWIM_RIPPLE_LIFE;
	if (age >= 1)
		return {reach, 0};
	const float rest = 1 - age;
	return {reach - SWIM_RIPPLE_INSET + SWIM_RIPPLE_GROWTH * (1 - rest * rest), SWIM_RIPPLE_ALPHA * rest};
}

//! The ring is drawn as this many segments. A moving swimmer pushes the water
//! back, so its ring is solid behind it, thins along its sides and vanishes
//! ahead of it; a swimmer treading water ripples all round.
constexpr int SWIM_RIPPLE_SEGMENTS = 24;
constexpr float SWIM_RIPPLE_FRONT_EDGE = -0.2f, SWIM_RIPPLE_BACK_EDGE = 0.6f;

//! Opacity, from 0 to 1, of the ring at the given angle (radians, in screen
//! coordinates with y down) for a swimmer heading along (dx, dy).
inline float swimRippleWeight(float angle, int dx, int dy)
{
	if (!dx && !dy)
		return 1;
	// Cosine between this point of the ring and straight behind the swimmer.
	const float behind = -(std::cos(angle) * float(dx) + std::sin(angle) * float(dy)) / std::hypot(float(dx), float(dy));
	const float t = std::clamp((behind - SWIM_RIPPLE_FRONT_EDGE) / (SWIM_RIPPLE_BACK_EDGE - SWIM_RIPPLE_FRONT_EDGE), 0.f, 1.f);
	return t * t * (3 - 2 * t);
}
