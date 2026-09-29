// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef main
#undef main
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <PerformanceTelemetry.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
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
int main()
{
	Moments a, b;
	a.add(10);
	a.add(20);
	b.add(30);
	b.add(40);
	a.merge(b);
	assert(a.count == 4 && a.total == 100 && a.mean == 25 && a.m2 == 500 && a.maximum == 40);
	Budget budget;
	for (auto n : {11, 12, 9, 15})
		budget.add(n, 10);
	assert(budget.exceeded == 3 && budget.excess == 8 && budget.longest == 2 && budget.worst == 5);
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
	assert(metric(Id::Loop).time.total == 41 && metric(Id::Work).time.total == 11);
	assert(c.workBudget.exceeded == 1);
	c.presented();
	timeNs += 20;
	c.presented();
	timeNs += 30;
	c.presented();
	assert(metric(Id::FrameInterval).time.mean == 25 && metric(Id::Jitter).time.mean == 10);
	assert(c.frameBudget.exceeded == 1);
	const auto readsBefore = clockReads;
	for (int i = 0; i < 128; ++i)
	{
		Scope query(Id::PathPoint);
		++timeNs;
	}
	assert(clockReads - readsBefore == 4);
	assert(metric(Id::PathPoint).calls == 128 && metric(Id::PathPoint).time.count == 2);
	const auto actor = c.actor(3, 2, 5, 0);
	assert(actor == c.actor(3, 2, 5, 0));
	assert(actor != c.actor(3, 2, 5, 1));
	{
		Scope ai(Id::AI, actor);
		timeNs += 5;
	}
	assert(c.actors[actor].window.time.total == 5);
	c.capture(511);
	assert(metric(Id::Loop).calls == 1);
	c.capture(512);
	assert(metric(Id::Loop).calls == 0 && c.total[unsigned(Id::Loop)].calls == 1);
	assert(c.phase == 1);
	for (int i = 0; i < 64; ++i)
	{
		Scope query(Id::PathPoint);
		++timeNs;
	}
	assert(metric(Id::PathPoint).time.count == 1);
	c.configure(513, 0, 0, "uncapped");
	assert(!c.lastPresentation && !c.lastInterval);
	{
		Scope work(Id::Work);
		timeNs += 100;
	}
	assert(c.workBudget.count == 0);
	timeNs += 5000000000ULL;
	c.capture(513);
	assert(metric(Id::Work).calls == 0);
	std::ostringstream out;
	c.write(out, "TEST", 513, true);
	assert(out.str().find("estimated_total_ns=") != std::string::npos);
	assert(out.str().find("player=3 team=2 implementation=5 generation=0") != std::string::npos);
	c.enabled = false;
	{
		Scope disabled(Id::Loop);
		++timeNs;
	}
	assert(metric(Id::Loop).calls == 0);

	c.reset();
	c.output = false;
	nested(70);
	assert(c.depth == 0 && c.droppedScopes == 7 && metric(Id::Tasks).calls == 71);
	assert(metric(Id::Tasks).time.count == 64 && !metric(Id::Tasks).selfComplete);
	Moments large;
	large.add((std::uint64_t(1) << 55) + 1);
	assert(large.total == (std::uint64_t(1) << 55) + 1);
	std::ostringstream incomplete;
	c.write(incomplete, "TEST", 0, false);
	assert(incomplete.str().find("total_ns=na") != std::string::npos);
	std::cout << "Collector bytes: " << sizeof(Collector) << "\n";
	std::cout << "PASS performance moments, nesting, work/sleep/present separation, budgets, "
				 "jitter, sampling, identity, capture and disabled control\n";
}
