// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD speed chevrons' presets and the smoothed tick-rate readout. Time is an
// explicit millisecond argument, so every case is exact.
#include "Glob2Test.h"

#include "gui/GameSpeedControl.h"
#include <vector>

namespace
{
// Runs a simulation ticking every `interval` ms from `start` to `end`, sampled
// by a 60 Hz frame loop as the HUD does.
void run(TickRateMeter &meter, std::uint32_t &ticks, std::uint32_t &lastTick, std::uint32_t start,
		 std::uint32_t end, std::uint32_t interval)
{
	for (std::uint32_t now = start; now != end; ++now)
	{
		if (interval && (!ticks || now - lastTick >= interval))
		{
			++ticks;
			lastTick = now;
		}
		if (now % 16 == 0)
			meter.sample(now, lastTick, ticks);
	}
}
} // namespace

TEST_SUITE("GameSpeedControl")
{
	TEST_CASE("chevrons light and step through 1x, 2x, 4x, 8x and maximum")
	{
		using namespace GameSpeedControl;
		const int expectedLit[] = {0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5};
		for (int speed = -3; speed <= 10; ++speed)
			CHECK(lit(speed) == expectedLit[speed + 3]);
		int speed = -3;
		for (int expected : {0, 3, 5, 7, 10, 0, 3})
			CHECK((speed = faster(speed)) == expected);
		CHECK(faster(4) == 5);
		CHECK(faster(8) == 10);
		CHECK(faster(10) == 0);
		speed = 10;
		for (int expected : {7, 5, 3, 0, 0})
			CHECK((speed = slower(speed)) == expected);
		CHECK(slower(4) == 3);
		CHECK(slower(-2) == -2);
	}

	TEST_CASE("the rate has one decimal below 25")
	{
		CHECK(TickRateMeter::format(25) == "25");
		CHECK(TickRateMeter::format(24.96) == "25");
		CHECK(TickRateMeter::format(24.94) == "24.9");
		CHECK(TickRateMeter::format(6.25) == "6.3");
		CHECK(TickRateMeter::format(0) == "0.0");
		CHECK(TickRateMeter::format(62.5) == "63");
		CHECK(TickRateMeter::format(124.6) == "125");
	}

	TEST_CASE("a steady simulation reads its exact rate once a second has passed")
	{
		TickRateMeter meter;
		std::uint32_t ticks = 0, lastTick = 0;
		CHECK(!meter.rate());
		run(meter, ticks, lastTick, 5000, 5900, 40);
		CHECK(!meter.rate());
		run(meter, ticks, lastTick, 5900, 15000, 40);
		REQUIRE(meter.rate());
		CHECK(*meter.rate() == doctest::Approx(25));
		CHECK(TickRateMeter::format(*meter.rate()) == "25");
	}

	TEST_CASE("the readout changes at most once per second and averages three")
	{
		TickRateMeter meter;
		std::uint32_t ticks = 0, lastTick = 0;
		run(meter, ticks, lastTick, 0, 8000, 40);
		// The simulation drops to 12.5 ticks per second.
		std::vector<double> shown{*meter.rate()};
		for (std::uint32_t now = 8000; now < 13000; now += 100)
		{
			run(meter, ticks, lastTick, now, now + 100, 80);
			if (shown.back() != *meter.rate())
				shown.push_back(*meter.rate());
		}
		// 5 seconds allow at most 5 refreshes after the initial value; the window
		// blends the two rates before settling on the new one.
		CHECK(shown.size() <= 6);
		CHECK(shown.front() == doctest::Approx(25));
		CHECK(shown[1] < 25);
		CHECK(shown[1] > 12.5);
		CHECK(shown.back() == doctest::Approx(12.5));
	}

	TEST_CASE("a stalled simulation decays to zero and recovers")
	{
		TickRateMeter meter;
		std::uint32_t ticks = 0, lastTick = 0;
		run(meter, ticks, lastTick, 0, 6000, 40);
		run(meter, ticks, lastTick, 6000, 8000, 0);
		CHECK(*meter.rate() < 25);
		run(meter, ticks, lastTick, 8000, 12000, 0);
		CHECK(*meter.rate() == 0);
		run(meter, ticks, lastTick, 12000, 18000, 40);
		CHECK(*meter.rate() == doctest::Approx(25));
	}

	TEST_CASE("the millisecond clock may wrap")
	{
		TickRateMeter meter;
		std::uint32_t ticks = 0, lastTick = 0;
		const std::uint32_t start = 0xffffffffu - 4999;
		run(meter, ticks, lastTick, start, start + 10000, 40);
		REQUIRE(meter.rate());
		CHECK(*meter.rate() == doctest::Approx(25));
	}
}
