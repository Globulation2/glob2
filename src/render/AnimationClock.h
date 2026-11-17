// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

//! A map presentation time: whole AnimationClock steps plus the fraction of
//! the next step already elapsed. Every consumer of one frame reads the same
//! value. Integer consumers (flipbook frame selection, HD streaming windows,
//! page composition, sprite overlays, clouds) use `steps`; interpolating
//! consumers (the direct pass's crossfade and surf, the portable renderer's
//! keyframe crossfade) use the whole value, so they move by exactly the time
//! elapsed every frame. Converts from a whole step count.
struct AnimationTime
{
	int steps = 0;
	float fraction = 0; //!< In [0, 1).

	constexpr AnimationTime() = default;
	constexpr AnimationTime(int wholeSteps, float stepFraction = 0) : steps(wholeSteps), fraction(stepFraction) {}

	double value() const { return double(steps) + double(fraction); }

	//! The time modulo `period` steps, for float consumers (shaders): the
	//! integer part wraps exactly, so precision does not degrade over a long
	//! session, and the result is continuous across the wrap for any
	//! animation whose cycle divides the period. A period of zero or less
	//! does not wrap.
	float wrapped(int period) const
	{
		if (period <= 0)
			return float(double(steps) + double(fraction));
		const int whole = steps % period;
		return float(whole < 0 ? whole + period : whole) + fraction;
	}

	//! Which interval of `length` steps the time falls in, and the share of
	//! that interval elapsed, in [0, 1): a crossfade's endpoint and weight.
	struct Cycle
	{
		std::int64_t index = 0;
		float within = 0;
	};
	Cycle cycle(int length) const
	{
		if (length <= 1)
			return {std::int64_t(steps), fraction};
		std::int64_t index = steps / length;
		int offset = steps % length;
		if (offset < 0)
		{
			offset += length;
			--index;
		}
		return {index, std::min(float((double(offset) + double(fraction)) / double(length)), 0x1.fffffep-1f)};
	}
};

//! Turns drawn frames into map presentation time (water, shorelines, lava,
//! clouds, area markers) so that animations play at the same speed whatever
//! the frame rate. Each frame advances by the real time since the previous
//! one, counted in steps of 1/REFERENCE_RATE s, the rate at which the
//! materials' animation_ticks were tuned: advance() returns the whole steps,
//! and fraction() the part of the next step already elapsed, so whole steps
//! plus the fraction (an AnimationTime) move by exactly the elapsed time every
//! frame, however the frame rate relates to the reference rate. A long frame
//! (a hitch, alt-tab, a breakpoint) counts as at most MAX_FRAME_NS, so
//! animations never jump. Pausing freezes both parts.
class AnimationClock
{
public:
	static constexpr std::uint64_t REFERENCE_RATE = 60;
	static constexpr std::uint64_t MAX_FRAME_NS = 100'000'000;
	static constexpr std::uint64_t NS_PER_SECOND = 1'000'000'000;

	//! When positive, every unpaused advance counts exactly this many steps
	//! and no fraction, instead of measuring time, so tests and captures stay
	//! deterministic.
	static inline int fixedSteps = 0;
	//! When positive (and fixedSteps is not), every unpaused advance measures
	//! exactly this many nanoseconds instead of the real time between frames:
	//! deterministic frames at a chosen rate, fractions included, the first
	//! frame after a pause too.
	static inline std::uint64_t simulatedFrameNs = 0;

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
		{
			last = preset;
			remainder = pendingRemainder;
		}
		else if (fixedSteps > 0)
		{
			last = fixedSteps;
			remainder = 0;
		}
		else if (!lastNs && !simulatedFrameNs)
			last = 1;
		else
		{
			const std::uint64_t elapsed = simulatedFrameNs ? simulatedFrameNs : nowNs > lastNs ? nowNs - lastNs : 0;
			remainder += std::min(elapsed, MAX_FRAME_NS) * REFERENCE_RATE;
			last = int(remainder / NS_PER_SECOND);
			remainder %= NS_PER_SECOND;
		}
		lastNs = nowNs ? nowNs : 1;
		return last;
	}
	//! Steps the latest advance counted.
	int steps() const { return last; }
	//! The part of the next step elapsed since the latest whole step, in [0, 1).
	float fraction() const
	{
		// The largest float below one: a remainder a nanosecond short of a
		// step would otherwise round up to a whole step.
		return std::min(float(double(remainder) / double(NS_PER_SECOND)), 0x1.fffffep-1f);
	}
	//! Makes the next advance count exactly these steps and leave this
	//! fraction (a capture repeating the frame its source view just advanced).
	void presetNext(int steps, float stepFraction = 0)
	{
		pending = steps;
		const double clamped = std::clamp(double(stepFraction), 0.0, 1.0);
		pendingRemainder = std::min<std::uint64_t>(std::uint64_t(std::llround(clamped * double(NS_PER_SECOND))),
												   NS_PER_SECOND - 1);
	}
	//! Drops the elapsed fraction (a view pinned to a whole step).
	void clearFraction() { remainder = 0; }

private:
	std::uint64_t lastNs = 0;
	std::uint64_t remainder = 0;  // pending fraction of a step, in ns × REFERENCE_RATE
	std::uint64_t pendingRemainder = 0;
	int last = 0;
	int pending = -1;
};
