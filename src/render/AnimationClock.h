// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cstdint>

//! Turns drawn frames into steps of map presentation animation (water,
//! shorelines, lava, clouds, area markers) so that they play at the same speed
//! whatever the frame rate. Each frame advances by the real time since the
//! previous one, counted in steps of 1/REFERENCE_RATE s, the rate at which the
//! materials' animation_ticks were tuned. A long frame (a hitch, alt-tab, a
//! breakpoint) counts as at most MAX_FRAME_NS, so animations never jump. The
//! fractional remainder carries between frames, so rates that do not divide
//! the reference rate (25, 144 fps) still average exactly the reference speed.
class AnimationClock
{
public:
	static constexpr std::uint64_t REFERENCE_RATE = 60;
	static constexpr std::uint64_t MAX_FRAME_NS = 100'000'000;
	static constexpr std::uint64_t NS_PER_SECOND = 1'000'000'000;

	//! When positive, every unpaused advance counts exactly this many steps
	//! instead of measuring time, so tests and captures stay deterministic.
	static inline int fixedSteps = 0;

	//! Steps this frame advances; 0 while paused. The first frame advances one
	//! step. Pausing forgets the last timestamp, so resuming does not jump.
	int advance(bool paused) { return advanceAt(paused, SDL_GetTicksNS()); }
	int advanceAt(bool paused, std::uint64_t nowNs)
	{
		const int preset = pending;
		pending = -1;
		if (paused)
		{
			lastNs = 0;
			return last = 0;
		}
		if (preset >= 0)
			last = preset;
		else if (fixedSteps > 0)
			last = fixedSteps;
		else if (!lastNs)
			last = 1;
		else
		{
			remainder += std::min(nowNs > lastNs ? nowNs - lastNs : 0, MAX_FRAME_NS) * REFERENCE_RATE;
			last = int(remainder / NS_PER_SECOND);
			remainder %= NS_PER_SECOND;
		}
		lastNs = nowNs ? nowNs : 1;
		return last;
	}
	//! Steps the latest advance counted.
	int steps() const { return last; }
	//! Makes the next advance count exactly these steps (a capture repeating
	//! the frame its source view just advanced).
	void presetNext(int steps) { pending = steps; }

private:
	std::uint64_t lastNs = 0;
	std::uint64_t remainder = 0;  // pending fraction of a step, in ns × REFERENCE_RATE
	int last = 0;
	int pending = -1;
};
