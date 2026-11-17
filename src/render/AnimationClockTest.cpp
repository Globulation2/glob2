// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for AnimationClock: map animations advance by real time at the
// reference rate whatever the frame rate, never jump after a hitch or a pause,
// and stay deterministic when a fixed step is requested.

#include "Glob2Test.h"

#include "AnimationClock.h"

#include <cmath>
#include <cstdint>
#include <vector>

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
	std::uint64_t savedSimulated = AnimationClock::simulatedFrameNs;
	RealTime() { AnimationClock::fixedSteps = 0; AnimationClock::simulatedFrameNs = 0; }
	~RealTime() { AnimationClock::fixedSteps = saved; AnimationClock::simulatedFrameNs = savedSimulated; }
};

// Frame intervals of a display: steady, or with a fixed irregular jitter of up
// to ±jitter milliseconds around the mean.
std::vector<std::uint64_t> intervals(double milliseconds, double jitter, int frames)
{
	static constexpr double pattern[] = {1, -1, 0.5, -0.25, 1, -0.75, -1, 0.25};
	std::vector<std::uint64_t> out;
	for (int frame = 0; frame < frames; ++frame)
		out.push_back(std::uint64_t(std::llround((milliseconds + jitter * pattern[frame % 8]) * 1e6)));
	return out;
}

// The presentation time a view shows after each frame, accumulated as
// MapRenderState::advanceAnimation does: whole steps plus the clock's fraction.
std::vector<AnimationTime> presentationTimes(AnimationClock &clock, const std::vector<std::uint64_t> &frames)
{
	std::uint64_t now = 5'000'000'000ULL;
	int steps = clock.advanceAt(false, now);
	std::vector<AnimationTime> out{{steps, clock.fraction()}};
	for (auto interval : frames)
	{
		now += interval;
		steps += clock.advanceAt(false, now);
		out.push_back({steps, clock.fraction()});
	}
	return out;
}

struct Rate
{
	const char *name;
	double milliseconds, jitter;
};
constexpr Rate Rates[] = {{"30 Hz", 1000.0 / 30, 0},	{"60 Hz", 1000.0 / 60, 0},
						  {"60 Hz +-2 ms", 1000.0 / 60, 2}, {"120 Hz", 1000.0 / 120, 0},
						  {"144 Hz", 1000.0 / 144, 0},	  {"144 Hz +-1 ms", 1000.0 / 144, 1}};
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

	// Smoothness guards (docs/assets/terrain-rendering.md, "Animation clock"):
	// the presentation time a frame shows advances by exactly the real time
	// elapsed, so terrain animation never holds a frame or jumps two.
	TEST_CASE("Presentation time advances by exactly the elapsed time every frame at every rate")
	{
		RealTime realTime;
		for (const auto &rate : Rates)
		{
			CAPTURE(rate.name);
			AnimationClock clock;
			const auto frames = intervals(rate.milliseconds, rate.jitter, 240);
			const auto times = presentationTimes(clock, frames);
			for (std::size_t i = 1; i < times.size(); ++i)
			{
				CAPTURE(i);
				const double expected = double(frames[i - 1]) * AnimationClock::REFERENCE_RATE / 1e9;
				const double advanced = times[i].value() - times[i - 1].value();
				CHECK(advanced == doctest::Approx(expected).epsilon(1e-5));
				CHECK(times[i].fraction >= 0.f);
				CHECK(times[i].fraction < 1.f);
			}
		}
	}

	TEST_CASE("Simulated frame intervals advance the presentation time deterministically")
	{
		RealTime realTime;
		AnimationClock::simulatedFrameNs = 6'944'444; // 144 Hz
		AnimationClock clock;
		// The timestamps are ignored: each frame measures the simulated interval.
		const auto times = presentationTimes(clock, std::vector<std::uint64_t>(144, 1));
		for (std::size_t i = 1; i < times.size(); ++i)
			CHECK(times[i].value() - times[i - 1].value() == doctest::Approx(6'944'444 * 60 / 1e9).epsilon(1e-5));
		CHECK(times.back().value() - times.front().value() == doctest::Approx(60).epsilon(1e-4));
	}

	TEST_CASE("Pauses freeze the presentation time and presets carry its fraction")
	{
		RealTime realTime;
		AnimationClock clock;
		clock.advanceAt(false, 1'000'000'000ULL);
		CHECK(clock.advanceAt(false, 1'025'000'000ULL) == 1); // 1.5 steps
		CHECK(clock.fraction() == doctest::Approx(0.5));
		CHECK(clock.advanceAt(true, 1'030'000'000ULL) == 0);
		CHECK(clock.fraction() == doctest::Approx(0.5));
		AnimationClock capture;
		capture.presetNext(clock.steps() + 1, clock.fraction());
		CHECK(capture.advanceAt(false, 7) == 1);
		CHECK(capture.fraction() == doctest::Approx(0.5));
		capture.clearFraction();
		CHECK(capture.fraction() == 0.f);
		AnimationClock::fixedSteps = 1;
		CHECK(clock.advanceAt(false, 1'040'000'000ULL) == 1);
		CHECK(clock.fraction() == 0.f); // fixed steps stay on whole steps
	}

	TEST_CASE("A crossfade's weight moves by the elapsed share of its interval every frame")
	{
		// AnimationTime::cycle is the portable renderer's keyframe and
		// crossfade weight, and the direct pass's flipbook endpoint and weight.
		RealTime realTime;
		for (const auto &rate : Rates)
			for (int interval : {6, 16})
			{
				CAPTURE(rate.name);
				CAPTURE(interval);
				AnimationClock clock;
				const auto frames = intervals(rate.milliseconds, rate.jitter, 240);
				const auto times = presentationTimes(clock, frames);
				for (std::size_t i = 1; i < times.size(); ++i)
				{
					CAPTURE(i);
					const auto a = times[i - 1].cycle(interval), b = times[i].cycle(interval);
					const double elapsed = double(frames[i - 1]) * AnimationClock::REFERENCE_RATE / 1e9 / interval;
					// The weight moves by the elapsed share, carrying into the
					// next interval exactly when the endpoint does.
					const double moved = double(b.index - a.index) + double(b.within) - double(a.within);
					CHECK(moved == doctest::Approx(elapsed).epsilon(1e-4));
					CHECK(b.within >= 0.f);
					CHECK(b.within < 1.f);
				}
			}
	}

	TEST_CASE("Wrapped presentation time stays continuous and precise however long a session runs")
	{
		constexpr int period = 4800; // a catalogue's common period (TerrainVisual::animationPeriod)
		const auto check = [&](AnimationTime earlier, AnimationTime later)
		{
			// Every animation whose cycle divides the period sees the same
			// advance in the wrapped time as in the unwrapped time.
			const double unwrapped = later.value() - earlier.value();
			double wrapped = double(later.wrapped(period)) - double(earlier.wrapped(period));
			if (wrapped < 0) wrapped += period;
			CHECK(wrapped == doctest::Approx(unwrapped).epsilon(1e-3));
			CHECK(later.wrapped(period) >= 0.f);
			CHECK(later.wrapped(period) < float(period));
		};
		check({period - 1, 0.9f}, {period, 0.1f});
		check({2 * period - 1, 0.5f}, {2 * period + 1, 0.25f});
		// Thirty days at 60 steps a second: far beyond float precision unwrapped.
		const int month = 60 * 60 * 60 * 24 * 30;
		for (float fraction : {0.f, 0.1f, 0.5f, 0.9f})
		{
			CAPTURE(fraction);
			const AnimationTime t{month, fraction};
			CHECK(double(t.wrapped(period)) == doctest::Approx(double(month % period) + fraction).epsilon(1e-6));
			check(t, {month, fraction + 0.05f});
			check(t, {month + 1, fraction});
		}
		// One 144 Hz frame resolves to a hundredth of itself at the period's end.
		const AnimationTime late{period - 1, 0.25f}, next{period - 1, 0.25f + 60.f / 144.f};
		CHECK(double(next.wrapped(period)) - double(late.wrapped(period)) ==
			  doctest::Approx(60.0 / 144.0).epsilon(1e-2));
		CHECK(AnimationTime{12345, 0.5f}.wrapped(0) == 12345.5f); // no period: not wrapped
		CHECK(AnimationTime{-1, 0.5f}.wrapped(period) == doctest::Approx(period - 0.5));
		CHECK(AnimationTime{-1, 0.5f}.cycle(6).index == -1);
		CHECK(AnimationTime{-1, 0.5f}.cycle(6).within == doctest::Approx(5.5 / 6));
	}
}
