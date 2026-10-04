// SPDX-License-Identifier: GPL-3.0-or-later
// The touch scroll kernel: release velocity, momentum, rubber band and spring.
// Time is an explicit millisecond argument everywhere, so every case is exact.
#include "Glob2Test.h"

#include <ScrollPhysics.h>
#include <cmath>
#include <vector>

using namespace GAGCore;

namespace
{
using Phase = ScrollAxis::Phase;

ScrollPhysicsConfig defaults()
{
	return ScrollPhysicsConfig{};
}

// A finger moving the content by `delta` every `interval` ms, `samples` times.
ScrollAxis flung(ScrollAxis axis, Ticks start, double delta, Ticks interval, int samples)
{
	axis.beginDrag(start);
	Ticks t = start;
	for (int i = 0; i < samples; ++i)
	{
		t += interval;
		axis.drag(t, delta);
	}
	axis.endDrag(t);
	return axis;
}

// Steps until idle, returning the visited offsets (including the final one).
std::vector<double> settle(ScrollAxis &axis, Ticks start, Ticks interval, Ticks limit = 20000)
{
	std::vector<double> path;
	for (Ticks t = start + interval; t <= start + limit; t += interval)
	{
		path.push_back(axis.step(t));
		if (!axis.isAnimating())
			break;
	}
	return path;
}
} // namespace

TEST_SUITE("ScrollPhysics")
{
	TEST_CASE("velocity tracker fits constant and decelerating motion")
	{
		VelocityTracker constant;
		for (Ticks t = 0; t <= 96; t += 16)
			constant.add(t, 2.0 * t);
		CHECK(std::fabs(constant.velocity(96) - 2.0) < 1e-9);

		// A quadratic is fitted exactly; the derivative at the last sample is 3 - 0.02 t.
		VelocityTracker quadratic;
		for (Ticks t = 0; t <= 96; t += 16)
			quadratic.add(t, 3.0 * t - 0.01 * t * t);
		CHECK(std::fabs(quadratic.velocity(96) - (3.0 - 0.02 * 96)) < 1e-6);

		VelocityTracker two;
		two.add(10, 0);
		two.add(30, 50);
		CHECK(std::fabs(two.velocity(30) - 2.5) < 1e-9);
	}

	TEST_CASE("stopped finger stale samples and degenerate input yield zero")
	{
		VelocityTracker tracker;
		for (Ticks t = 0; t <= 100; t += 10)
			tracker.add(t, 4.0 * t);
		CHECK(tracker.velocity(141) == 0.0);
		CHECK(tracker.velocity(139) != 0.0);

		// Only the last 100 ms count: an earlier slower stretch does not dilute the fling.
		VelocityTracker recent;
		for (Ticks t = 0; t <= 200; t += 10)
			recent.add(t, t <= 100 ? double(t) : 100.0 + 5.0 * (t - 100));
		CHECK(std::fabs(recent.velocity(200) - 5.0) < 1e-6);

		VelocityTracker same;
		for (int i = 0; i < 5; ++i)
			same.add(0, 10.0 * i);
		CHECK(same.velocity(0) == 0.0);
		VelocityTracker one;
		one.add(5, 1);
		CHECK(one.velocity(5) == 0.0);
		CHECK(VelocityTracker{}.velocity(0) == 0.0);
	}

	TEST_CASE("a fling coasts to its projected end inside the bounds")
	{
		ScrollAxis axis(defaults());
		axis.setBounds(0, 10000, 300);
		axis = flung(axis, 1000, 20, 16, 5); // 1.25 units/ms
		REQUIRE(axis.phase() == Phase::Decelerating);
		const double projected = axis.projectedEnd();
		CHECK(projected > axis.offset() + 500);
		const auto path = settle(axis, 1080, 16);
		REQUIRE(axis.phase() == Phase::Idle);
		CHECK(std::fabs(axis.offset() - projected) < 1e-9);
		CHECK(axis.offset() <= 10000);
		for (std::size_t i = 1; i < path.size(); ++i)
			CHECK(path[i] >= path[i - 1]);
	}

	TEST_CASE("stepping cadence does not change the path")
	{
		for (double maximum : {10000.0, 400.0}) // the second run crosses an edge into the spring
		{
			ScrollAxis base(defaults());
			base.setBounds(0, maximum, 300);
			base = flung(base, 0, 24, 16, 6);
			REQUIRE(base.isAnimating());
			ScrollAxis fine = base, medium = base, coarse = base;
			for (Ticks t = 112; t <= 3000; t += 16)
				fine.step(t);
			for (Ticks t = 129; t <= 3000; t += 33)
				medium.step(t);
			for (Ticks t = 196; t <= 3000; t += 100)
				coarse.step(t);
			const double a = fine.step(3000), b = medium.step(3000), c = coarse.step(3000);
			CHECK(std::fabs(a - b) < 1e-6);
			CHECK(std::fabs(a - c) < 1e-6);
			CHECK(fine.phase() == medium.phase());
			CHECK(fine.phase() == coarse.phase());
		}
	}

	TEST_CASE("rubber band is monotonic and bounded by the extent")
	{
		ScrollAxis axis(defaults());
		axis.setBounds(0, 500, 300);
		axis.setOffset(500);
		axis.beginDrag(0);
		double previous = 0;
		for (int i = 1; i <= 180; ++i)
		{
			axis.drag(i * 4, 5);
			CHECK(axis.overscroll() > previous);
			CHECK(axis.overscroll() < 300);
			CHECK(axis.clampedOffset() == 500.0);
			previous = axis.overscroll();
		}
		// The same physical pull gives the same stretch when bounds are re-applied.
		axis.setBounds(0, 500, 300);
		CHECK(std::fabs(axis.overscroll() - previous) < 1e-9);
		// Pulling back is symmetric and returns exactly to the edge.
		for (int i = 1; i <= 180; ++i)
			axis.drag(720 + i * 4, -5);
		CHECK(std::fabs(axis.overscroll()) < 1e-9);
		CHECK(axis.offset() == 500.0);
	}

	TEST_CASE("the spring settles without oscillation")
	{
		ScrollAxis axis(defaults());
		axis.setBounds(0, 500, 300);
		axis.setOffset(500);
		axis.beginDrag(0);
		for (int i = 1; i <= 40; ++i)
			axis.drag(i * 8, 5);
		const double pulled = axis.overscroll();
		REQUIRE(pulled > 50);
		axis.endDrag(400); // finger paused 80 ms: no release velocity
		REQUIRE(axis.phase() == Phase::Bouncing);
		double previous = pulled;
		Ticks finished = 0;
		for (Ticks t = 416; t <= 3000; t += 16)
		{
			axis.step(t);
			CHECK(axis.overscroll() >= 0);
			CHECK(axis.overscroll() <= previous + 1e-9);
			previous = axis.overscroll();
			if (!axis.isAnimating())
			{
				finished = t;
				break;
			}
		}
		CHECK(finished > 0);
		CHECK(finished <= 400 + 1500);
		CHECK(axis.overscroll() == 0.0);
		CHECK(axis.offset() == 500.0);
	}

	TEST_CASE("a fling into an edge hands its velocity to the spring")
	{
		ScrollAxis axis(defaults());
		axis.setBounds(0, 200, 300);
		axis = flung(axis, 0, 32, 16, 5); // 2 units/ms, projected far past 200
		REQUIRE(axis.phase() == Phase::Decelerating);
		CHECK(axis.projectedEnd() == 200.0);
		bool bounced = false;
		double peak = 0;
		for (Ticks t = 96; t <= 5000; t += 16)
		{
			axis.step(t);
			if (axis.phase() == Phase::Bouncing)
				bounced = true;
			peak = std::max(peak, axis.overscroll());
			CHECK(axis.overscroll() >= 0);
			if (!axis.isAnimating())
				break;
		}
		CHECK(bounced);
		CHECK(peak > 0);
		const double lambda = 2 * 3.14159265358979323846 / 500;
		CHECK(peak <= defaults().maxBounceVelocity / (lambda * std::exp(1.0)) + 1e-9);
		CHECK(axis.phase() == Phase::Idle);
		CHECK(axis.offset() == 200.0);
	}

	TEST_CASE("wrap mode is unbounded and bounce off stops at the edge")
	{
		ScrollPhysicsConfig wrap = defaults();
		wrap.wrap = true;
		ScrollAxis torus(wrap);
		torus.setBounds(0, 100, 50);
		torus = flung(torus, 0, 32, 16, 5);
		settle(torus, 80, 16);
		CHECK(torus.offset() > 1000);
		CHECK(torus.phase() == Phase::Idle);

		ScrollPhysicsConfig clamp = defaults();
		clamp.bounce = false;
		ScrollAxis hard(clamp);
		hard.setBounds(0, 200, 300);
		hard = flung(hard, 0, 32, 16, 5);
		bool stoppedAtEdge = false;
		for (Ticks t = 96; t <= 5000; t += 16)
		{
			hard.step(t);
			CHECK(hard.offset() <= 200);
			if (!hard.isAnimating())
			{
				stoppedAtEdge = hard.offset() == 200.0;
				break;
			}
		}
		CHECK(stoppedAtEdge);
		// Dragging past the edge with bounce off clamps instead of stretching.
		hard.beginDrag(6000);
		hard.drag(6016, 50);
		CHECK(hard.offset() == 200.0);
		CHECK(hard.overscroll() == 0.0);
	}

	TEST_CASE("interrupt keeps the offset and a new drag continues from it")
	{
		ScrollAxis axis(defaults());
		axis.setBounds(0, 10000, 300);
		axis = flung(axis, 0, 20, 16, 5);
		axis.step(300);
		const double midway = axis.offset();
		axis.interrupt();
		CHECK(axis.phase() == Phase::Idle);
		CHECK(axis.offset() == midway);
		axis.step(1000);
		CHECK(axis.offset() == midway);
		axis.beginDrag(1000);
		axis.drag(1016, 10);
		CHECK(std::fabs(axis.offset() - (midway + 10)) < 1e-9);

		// A finger landing mid-bounce keeps the stretch and can settle it later.
		ScrollAxis bouncing(defaults());
		bouncing.setBounds(0, 500, 300);
		bouncing.setOffset(500);
		bouncing.beginDrag(0);
		for (int i = 1; i <= 20; ++i)
			bouncing.drag(i * 8, 10);
		bouncing.endDrag(300);
		bouncing.step(316);
		const double stretched = bouncing.overscroll();
		REQUIRE(stretched > 0);
		bouncing.interrupt();
		CHECK(bouncing.overscroll() == stretched);
		// Frames keep refreshing the bounds while the finger holds the stretch.
		bouncing.setBounds(0, 500, 300);
		bouncing.step(350);
		CHECK(bouncing.overscroll() == stretched);
		bouncing.settle(400);
		CHECK(bouncing.phase() == Phase::Bouncing);
		settle(bouncing, 400, 16);
		CHECK(bouncing.offset() == 500.0);
	}

	TEST_CASE("fling velocity limits and momentum off")
	{
		ScrollAxis slow(defaults());
		slow.setBounds(0, 10000, 300);
		slow = flung(slow, 0, 0.5, 16, 5); // 0.03 units/ms, under the minimum
		CHECK(slow.phase() == Phase::Idle);

		ScrollAxis fast(defaults());
		fast.setBounds(0, 1e9, 300);
		fast = flung(fast, 0, 800, 16, 5); // 50 units/ms, clamped to 8
		REQUIRE(fast.phase() == Phase::Decelerating);
		const double expected = fast.offset() - 8.0 / std::log(0.998);
		CHECK(std::fabs(fast.projectedEnd() - expected) < 1e-6);

		ScrollPhysicsConfig off = defaults();
		off.momentum = false;
		ScrollAxis still(off);
		still.setBounds(0, 10000, 300);
		still = flung(still, 0, 20, 16, 5);
		CHECK(still.phase() == Phase::Idle);
		CHECK(still.offset() == 100.0);
	}

	TEST_CASE("bounds changes clamp idle offsets but not a drag in progress")
	{
		ScrollAxis axis(defaults());
		axis.setBounds(0, 1000, 300);
		axis.setOffset(800);
		axis.setBounds(0, 500, 300);
		CHECK(axis.offset() == 500.0);
		axis.beginDrag(0);
		axis.drag(16, 100);
		const double stretched = axis.overscroll();
		CHECK(stretched > 0);
		axis.setBounds(0, 500, 300);
		CHECK(std::fabs(axis.overscroll() - stretched) < 1e-9);
	}

	TEST_CASE("two axes step together and report deltas")
	{
		ScrollPhysicsConfig wrap = defaults();
		wrap.wrap = true;
		ScrollMotion motion(wrap);
		motion.beginDrag(0);
		for (int i = 1; i <= 5; ++i)
			motion.drag(i * 16, 16, -8);
		motion.endDrag(80);
		REQUIRE(motion.isAnimating());
		double sumX = 0, sumY = 0;
		for (Ticks t = 96; t <= 6000 && motion.isAnimating(); t += 16)
		{
			const auto [dx, dy] = motion.stepDelta(t);
			CHECK(dx >= 0);
			CHECK(dy <= 0);
			sumX += dx;
			sumY += dy;
		}
		CHECK(!motion.isAnimating());
		CHECK(std::fabs(sumX - (motion.x.offset() - 80)) < 1e-9);
		CHECK(std::fabs(sumY - (motion.y.offset() + 40)) < 1e-9);
		CHECK(std::fabs(sumX / -sumY - 2.0) < 1e-6);
	}

	TEST_CASE("widenTicks follows the reference across a 32-bit wrap")
	{
		const Ticks period = Ticks(1) << 32;
		CHECK(widenTicks(1000, 0) == 1000);
		CHECK(widenTicks(1000, 900) == 1000);
		CHECK(widenTicks(0xFFFFFF00u, period + 10) == 0xFFFFFF00u);
		CHECK(widenTicks(5, period - 10) == period + 5);
		CHECK(widenTicks(5, 2 * period - 10) == 2 * period + 5);
		CHECK(widenTicks(0xFFFFFF00u, period + 10) < period + 10);
	}

	TEST_CASE("presets follow the tuning sliders")
	{
		ScrollTuning saved = scrollTuning();
		scrollTuning() = ScrollTuning{};
		CHECK(std::fabs(ScrollPresets::widget().decelerationRate - 0.998) < 1e-12);
		CHECK(std::fabs(ScrollPresets::widget().rubberBandCoefficient - 0.55) < 1e-12);
		CHECK(ScrollPresets::widget().momentum);
		CHECK(ScrollPresets::widget().bounce);
		CHECK(!ScrollPresets::widget().wrap);
		CHECK(ScrollPresets::mapViewport().wrap);
		CHECK(!ScrollPresets::mapViewport().bounce);
		CHECK(!ScrollPresets::mouse().momentum);
		CHECK(!ScrollPresets::mouse().bounce);
		CHECK(decelerationRateForLevel(100) > decelerationRateForLevel(50));
		CHECK(decelerationRateForLevel(1) < decelerationRateForLevel(50));
		CHECK(decelerationRateForLevel(1) > 0.99);
		scrollTuning().momentum = 0;
		scrollTuning().bounce = 0;
		CHECK(!ScrollPresets::widget().momentum);
		CHECK(!ScrollPresets::widget().bounce);
		CHECK(ScrollPresets::mapViewport().momentum);
		scrollTuning() = ScrollTuning{};
		scrollTuning().reducedMotion = true;
		CHECK(!ScrollPresets::widget().bounce);
		CHECK(ScrollPresets::widget().momentum);
		scrollTuning().mapMomentum = 0;
		CHECK(!ScrollPresets::mapViewport().momentum);
		scrollTuning() = saved;
	}
}
