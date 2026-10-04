// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <GestureScroll.h>
#include <EventQueue.h>
using namespace GAGCore;
namespace
{
struct TuningScope
{
	ScrollTuning saved = scrollTuning();
	TuningScope() { scrollTuning() = {}; }
	~TuningScope() { scrollTuning() = saved; }
};
GestureScrollEvent sample(Uint64 time, double dy, ScrollGesturePhase phase = ScrollGesturePhase::Changed,
						 ScrollGesturePhase momentum = ScrollGesturePhase::None)
{
	GestureScrollEvent e;
	e.sequence = 17;
	e.timestamp = SDL_MS_TO_NS(time);
	e.dy = dy; e.phase = phase; e.momentum = momentum;
	return e;
}
}
TEST_SUITE("GestureScroll")
{
TEST_CASE("native deltas accumulate fractions and release never adds a fling")
{
	TuningScope tuning;
	GestureScrollController controller;
	ScrollAxis axis;
	auto e = sample(1000, 0, ScrollGesturePhase::Began);
	controller.begin(e, axis, 50, 1000, 200);
	for (int i = 1; i <= 10; ++i) controller.handle(sample(1000 + i, -0.125), axis);
	CHECK(axis.offset() == doctest::Approx(51.25));
	controller.handle(sample(1020, 0, ScrollGesturePhase::Ended), axis);
	CHECK_FALSE(axis.isAnimating());
	controller.update(1050, axis);
	CHECK(axis.offset() == doctest::Approx(51.25));
	controller.handle(sample(1060, -5, ScrollGesturePhase::None, ScrollGesturePhase::Began), axis);
	CHECK(axis.offset() == doctest::Approx(56.25));
	controller.update(1090, axis);
	CHECK(axis.offset() == doctest::Approx(56.25));
	controller.handle(sample(1100, 0, ScrollGesturePhase::None, ScrollGesturePhase::Ended), axis);
	CHECK_FALSE(controller.pending());
	CHECK_FALSE(axis.isAnimating());
}
TEST_CASE("native momentum is swallowed after reaching either edge")
{
	TuningScope tuning;
	for (double direction : {-1., 1.})
	{
		GestureScrollController controller;
		ScrollAxis axis;
		auto e = sample(1000, 0, ScrollGesturePhase::Began);
		controller.begin(e, axis, 50, 100, 200);
		controller.handle(sample(1020, 0, ScrollGesturePhase::Ended), axis);
		controller.handle(sample(1040, direction * 150, ScrollGesturePhase::None, ScrollGesturePhase::Began), axis);
		CHECK(axis.isAnimating());
		const auto stretched = axis.offset();
		controller.handle(sample(1050, direction * 500, ScrollGesturePhase::None, ScrollGesturePhase::Changed), axis);
		CHECK(axis.offset() == stretched);
		controller.update(2000, axis);
		CHECK(axis.offset() == doctest::Approx(direction < 0 ? 100 : 0));
	}
}
TEST_CASE("release from stretch springs and ignores native tail; new contact catches spring")
{
	TuningScope tuning;
	GestureScrollController controller;
	ScrollAxis axis;
	auto e = sample(1000, 0, ScrollGesturePhase::Began);
	controller.begin(e, axis, 0, 100, 200);
	controller.handle(sample(1010, 80), axis);
	CHECK(axis.overscroll() < 0);
	CHECK(axis.overscroll() > -80);
	controller.handle(sample(1020, 0, ScrollGesturePhase::Ended), axis);
	CHECK(axis.isAnimating());
	const auto stretched = axis.offset();
	controller.handle(sample(1030, 20, ScrollGesturePhase::None, ScrollGesturePhase::Began), axis);
	CHECK(axis.offset() == stretched);
	e.sequence = 18; e.timestamp = SDL_MS_TO_NS(1040);
	controller.begin(e, axis, axis.offset(), 100, 200);
	CHECK(axis.offset() == stretched);
	CHECK_FALSE(axis.isAnimating());
}
TEST_CASE("horizontal tray chooses and retains its initial dominant axis")
{
	TuningScope tuning;
	for (bool horizontalInput : {true, false})
	{
		GestureScrollController controller;
		ScrollAxis axis;
		auto e = sample(1000, 0, ScrollGesturePhase::Began);
		controller.begin(e, axis, 50, 1000, 200, true);
		e.phase = ScrollGesturePhase::Changed;
		e.dx = horizontalInput ? -10 : -1; e.dy = horizontalInput ? -1 : -10;
		controller.handle(e, axis);
		CHECK(axis.offset() == 60);
		e.dx = horizontalInput ? -1 : -100; e.dy = horizontalInput ? -100 : -1;
		controller.handle(e, axis);
		CHECK(axis.offset() == 61);
	}
}
TEST_CASE("settings; zero content; timeout only after release; cancelled tails stay owned")
{
	TuningScope tuning;
	GestureScrollController controller;
	ScrollAxis axis;
	auto e = sample(1000, 0, ScrollGesturePhase::Began);
	scrollTuning().bounce = 0;
	controller.begin(e, axis, 50, 100, 200);
	controller.handle(sample(1010, 100), axis);
	CHECK(axis.offset() == 0);
	controller.update(5000, axis);
	controller.handle(sample(5010, -20), axis);
	CHECK(axis.offset() == 20); // A resting finger never times out.
	controller.handle(sample(5020, 0, ScrollGesturePhase::Ended), axis);
	scrollTuning().momentum = 0;
	controller.handle(sample(5030, -30, ScrollGesturePhase::None, ScrollGesturePhase::Began), axis);
	CHECK(axis.offset() == 20);
	controller.update(5180, axis);
	CHECK_FALSE(controller.pending());
	controller.handle(sample(5190, -30, ScrollGesturePhase::None, ScrollGesturePhase::Changed), axis);
	CHECK(axis.offset() == 20);
	CHECK(controller.owns(e));
	scrollTuning().bounce = 50; scrollTuning().reducedMotion = true;
	e.sequence++;
	controller.begin(e, axis, 0, 0, 100);
	e.dy = 100;
	controller.handle(e, axis);
	CHECK(axis.overscroll() == 0);
	controller.cancel(axis);
	CHECK(controller.owns(e));
	CHECK_FALSE(axis.isAnimating());
}
TEST_CASE("gesture payload survives SDL queue; source reuse; deferred queue copies and clearing")
{
	REQUIRE(SDL_InitSubSystem(SDL_INIT_EVENTS));
	auto e = sample(1000, -0.125, ScrollGesturePhase::Ended);
	e.x = 18.25; e.y = 23.5; e.dx = 1.5; e.wheelY = -0.0125f;
	e.direction = SDL_MOUSEWHEEL_FLIPPED;
	auto event = gestureScrollEvent(e);
	REQUIRE(SDL_PushEvent(&event));
	event = {};
	REQUIRE(SDL_PeepEvents(&event, 1, SDL_GETEVENT, gestureScrollEventType(), gestureScrollEventType()) == 1);
	EventQueue first, deferred;
	first.push_back(event);
	deferred.push_back(first.events().front());
	first.clear(); event = {}; SDL_PumpEvents();
	const auto copy = scrollGesture(deferred.events().front());
	REQUIRE(copy);
	CHECK(copy->dy == -0.125);
	CHECK(copy->x == 18.25);
	CHECK(copy->sequence == e.sequence);
	CHECK(copy->phase == ScrollGesturePhase::Ended);
	auto wheel = gestureWheelFallback(*copy);
	CHECK(wheel.wheel.y == doctest::Approx(-0.0125));
	CHECK(wheel.wheel.direction == SDL_MOUSEWHEEL_FLIPPED);
	deferred.clear();
	CHECK(deferred.events().empty());
	SDL_QuitSubSystem(SDL_INIT_EVENTS);
}
}
