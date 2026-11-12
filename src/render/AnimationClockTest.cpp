// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for AnimationClock: map animations advance by real time at the
// reference rate whatever the frame rate, never jump after a hitch or a pause,
// and stay deterministic when a fixed step is requested.

#include "Glob2Test.h"

#include "AnimationClock.h"

#include <cstdint>

namespace
{
// Draws `seconds` of frames at `fps` and returns the steps they advanced.
long long stepsOver(int fps, int seconds)
{
	AnimationClock clock;
	std::uint64_t now = 5'000'000'000ULL;
	long long total = clock.advanceAt(false, now) - 1;  // the first frame only starts the clock
	for (int frame = 1; frame <= fps * seconds; ++frame)
		total += clock.advanceAt(false, now + std::uint64_t(frame) * AnimationClock::NS_PER_SECOND / fps);
	return total;
}

struct RealTime
{
	int saved = AnimationClock::fixedSteps;
	RealTime() { AnimationClock::fixedSteps = 0; }
	~RealTime() { AnimationClock::fixedSteps = saved; }
};
} // namespace

TEST_SUITE("AnimationClock")
{
	TEST_CASE("Animations play at the reference rate at every frame rate")
	{
		RealTime realTime;
		for (int fps : {25, 30, 60, 90, 120, 144, 165, 240})
		{
			CAPTURE(fps);
			CHECK_EQ(stepsOver(fps, 10), 600);
		}
	}

	TEST_CASE("The first frame advances one step and pauses freeze without a jump on resume")
	{
		RealTime realTime;
		AnimationClock clock;
		CHECK_EQ(clock.advanceAt(false, 1'000'000'000ULL), 1);
		CHECK_EQ(clock.advanceAt(true, 1'500'000'000ULL), 0);
		CHECK_EQ(clock.steps(), 0);
		CHECK_EQ(clock.advanceAt(true, 9'000'000'000ULL), 0);
		CHECK_EQ(clock.advanceAt(false, 9'016'000'000ULL), 1);  // resuming restarts the clock
		CHECK_EQ(clock.advanceAt(false, 9'049'500'000ULL), 2);
	}

	TEST_CASE("A long frame advances at most the frame limit")
	{
		RealTime realTime;
		AnimationClock clock;
		clock.advanceAt(false, 1'000'000'000ULL);
		CHECK_EQ(clock.advanceAt(false, 31'000'000'000ULL), 6);
		CHECK_EQ(clock.advanceAt(false, 30'000'000'000ULL), 0);  // a clock going backwards advances nothing
	}

	TEST_CASE("Fixed steps and presets override the measured time")
	{
		RealTime realTime;
		AnimationClock::fixedSteps = 1;
		AnimationClock clock;
		CHECK_EQ(clock.advanceAt(false, 1'000'000'000ULL), 1);
		CHECK_EQ(clock.advanceAt(false, 9'000'000'000ULL), 1);
		clock.presetNext(3);
		CHECK_EQ(clock.advanceAt(false, 9'000'000'001ULL), 3);
		CHECK_EQ(clock.steps(), 3);
		CHECK_EQ(clock.advanceAt(false, 9'000'000'002ULL), 1);
		clock.presetNext(3);
		CHECK_EQ(clock.advanceAt(true, 9'000'000'003ULL), 0);  // paused captures do not advance
	}
}
