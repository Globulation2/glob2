// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <string>
#include <cstdint>
#include <PerformanceTelemetry.h>
#include <cmath>
#include <sstream>
#include <thread>
using namespace PerformanceTelemetry;
static std::uint64_t timeNs;
static unsigned clockReads;
static std::uint64_t fakeClock()
{
	++clockReads;
	return timeNs;
}
static Metric &metric(Id id)
{
	return collector().window[unsigned(id)];
}
static void nested(unsigned remaining)
{
	Scope scope(Id::Tasks);
	++timeNs;
	if (remaining)
		nested(remaining - 1);
}
TEST_SUITE("PerformanceTelemetry")
{
TEST_CASE("moments; nesting; work/sleep/present separation; budgets; jitter; sampling; identity; capture and disabled control")
{
	const auto originalClock = collector().clock;
	Moments a, b;
	a.add(10);
	a.add(20);
	b.add(30);
	b.add(40);
	a.merge(b);
	REQUIRE((a.count == 4 && a.total == 100 && a.mean == 25 && a.m2 == 500 && a.maximum == 40));
	Budget budget;
	for (auto n : {11, 12, 9, 15})
		budget.add(n, 10);
	REQUIRE((budget.exceeded == 3 && budget.excess == 8 && budget.longest == 2 && budget.worst == 5));
	auto &c = collector();
	c.clock = fakeClock;
	timeNs = 100;
	c.reset();
	c.output = false;
	c.configure(0, 10, 20, "play");
	{
		Scope loop(Id::Loop);
		Scope work(Id::Work);
		timeNs += 3;
		{
			Scope sleep(Id::Sleep);
			timeNs += 20;
		}
		{
			Scope present(Id::Present);
			timeNs += 10;
		}
		timeNs += 8;
	}
	REQUIRE((metric(Id::Loop).time.total == 41 && metric(Id::Work).time.total == 11));
	REQUIRE(c.workBudget.exceeded == 1);
	c.presented();
	timeNs += 20;
	c.presented();
	timeNs += 30;
	c.presented();
	REQUIRE((metric(Id::FrameInterval).time.mean == 25 && metric(Id::Jitter).time.mean == 10));
	REQUIRE(c.frameBudget.exceeded == 1);
	const auto readsBefore = clockReads;
	for (int i = 0; i < 128; ++i)
	{
		Scope query(Id::PathPoint);
		++timeNs;
	}
	REQUIRE(clockReads - readsBefore == 4);
	REQUIRE((metric(Id::PathPoint).calls == 128 && metric(Id::PathPoint).time.count == 2));
	const auto actor = c.actor(3, 2, 5, 0);
	REQUIRE(actor == c.actor(3, 2, 5, 0));
	REQUIRE(actor != c.actor(3, 2, 5, 1));
	{
		Scope ai(Id::AI, actor);
		timeNs += 5;
	}
	REQUIRE(c.actors[actor].window.time.total == 5);
	c.capture(511);
	REQUIRE(metric(Id::Loop).calls == 1);
	c.capture(512);
	REQUIRE((metric(Id::Loop).calls == 0 && c.total[unsigned(Id::Loop)].calls == 1));
	REQUIRE(c.phase == 1);
	for (int i = 0; i < 64; ++i)
	{
		Scope query(Id::PathPoint);
		++timeNs;
	}
	REQUIRE(metric(Id::PathPoint).time.count == 1);
	c.configure(513, 0, 0, "uncapped");
	REQUIRE((!c.lastPresentation && !c.lastInterval));
	{
		Scope work(Id::Work);
		timeNs += 100;
	}
	REQUIRE(c.workBudget.count == 0);
	timeNs += 5000000000ULL;
	c.capture(513);
	REQUIRE(metric(Id::Work).calls == 0);
	std::ostringstream out;
	c.write(out, "TEST", 513, true);
	REQUIRE(out.str().find("estimated_total_ns=") != std::string::npos);
	REQUIRE(out.str().find("player=3 team=2 implementation=5 generation=0") != std::string::npos);
	c.enabled = false;
	{
		Scope disabled(Id::Loop);
		++timeNs;
	}
	REQUIRE(metric(Id::Loop).calls == 0);

	c.reset();
	c.output = false;
	nested(70);
	REQUIRE((c.depth == 0 && c.droppedScopes == 7 && metric(Id::Tasks).calls == 71));
	REQUIRE((metric(Id::Tasks).time.count == 64 && !metric(Id::Tasks).selfComplete));
	Moments large;
	large.add((std::uint64_t(1) << 55) + 1);
	REQUIRE(large.total == (std::uint64_t(1) << 55) + 1);
	std::ostringstream incomplete;
	c.write(incomplete, "TEST", 0, false);
	REQUIRE(incomplete.str().find("total_ns=na") != std::string::npos);
	MESSAGE("Collector bytes: " << sizeof(Collector));
	c.reset();
	c.clock = originalClock;
}
TEST_CASE("a bound simulation collector is absorbed with thread attribution, actors and budget")
{
	auto &main = collector();
	const auto originalClock = main.clock;
	main.clock = fakeClock;
	timeNs = 1000;
	main.reset();
	main.output = false;
	main.configure(0, 5, 16, "live");
	Collector simulation;
	simulation.clock = fakeClock;
	simulation.reset();
	// The simulation thread records into the collector bound for it.
	std::thread([&] {
		bindCollector(&simulation);
		REQUIRE(&collector() == &simulation);
		const int ai = collector().actor(1, 1, 2, 0);
		{
			Scope work(Id::Work);
			Scope tick(Id::Tasks, ai);
			timeNs += 7;
		}
		bindCollector(nullptr);
	}).join();
	REQUIRE(&collector() == &main);
	{
		Scope draw(Id::Present);
		timeNs += 2;
	}
	main.absorb(simulation);
	CHECK(main.window[unsigned(Id::Tasks)].calls == 1);
	CHECK(main.window[unsigned(Id::Work)].calls == 1);
	CHECK(simulation.window[unsigned(Id::Tasks)].calls == 0);
	CHECK(main.workBudget.exceeded == 0); // the simulation had no budget yet
	CHECK(simulation.budgetNs == main.budgetNs);
	REQUIRE(main.actorCount == 1);
	CHECK((main.actors[0].player == 1 && main.actors[0].window.calls == 1));
	CHECK(simulation.actors[0].window.calls == 0);
	std::ostringstream out;
	main.write(out, "TEST", 0, false);
	CHECK(out.str().find("scope=simulation.tasks thread=simulation") != std::string::npos);
	CHECK(out.str().find("scope=render.present thread=main") != std::string::npos);
	// With the budget handed over, simulation work is judged against it.
	std::thread([&] {
		bindCollector(&simulation);
		{
			Scope work(Id::Work);
			timeNs += 9;
		}
		bindCollector(nullptr);
	}).join();
	main.absorb(simulation);
	CHECK(main.workBudget.exceeded == 1);
	main.reset();
	main.clock = originalClock;
}
}
