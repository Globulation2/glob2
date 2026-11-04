// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <algorithm>
#include <string>
#include <cstdint>
#include <PerformanceTelemetry.h>
#include <cmath>
#include <sstream>
#include <iostream>
#include <thread>
#include <vector>
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
	TEST_CASE("moments; nesting; work/sleep/present separation; budgets; jitter; sampling; "
			  "identity; capture and disabled control")
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
		REQUIRE((budget.exceeded == 3 && budget.excess == 8 && budget.longest == 2 &&
				 budget.worst == 5));
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
		REQUIRE(out.str().find("player=3 team=2 implementation=5 generation=0") !=
				std::string::npos);
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
		std::thread(
			[&]
			{
				bindCollector(&simulation);
				REQUIRE(&collector() == &simulation);
				const int ai = collector().actor(1, 1, 2, 0);
				{
					Scope work(Id::Work);
					Scope tick(Id::Tasks, ai);
					timeNs += 7;
				}
				bindCollector(nullptr);
			})
			.join();
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
		std::thread(
			[&]
			{
				bindCollector(&simulation);
				{
					Scope work(Id::Work);
					timeNs += 9;
				}
				bindCollector(nullptr);
			})
			.join();
		main.absorb(simulation);
		CHECK(main.workBudget.exceeded == 1);
		main.reset();
		main.clock = originalClock;
	}
}

TEST_SUITE("PerformanceTelemetry")
{
	TEST_CASE("nested wall measurements exclude children and leave CPU fields unavailable")
	{
		auto &c = collector();
		const auto original = c.clock;
		c.clock = fakeClock;
		timeNs = 100;
		c.reset();
		c.output = false;
		{
			Scope parent(Id::Tick);
			timeNs += 1000;
			{
				Scope child(Id::Units);
				timeNs += 3000;
			}
			timeNs += 1000;
		}
		const auto &m = metric(Id::Tick);
		CHECK(m.time.total == 5000);
		CHECK(m.self == 2000);
		CHECK(metric(Id::Units).time.total == 3000);
		CHECK(m.cpuSamples == 0);
		CHECK(m.cpuNs == 0);
		CHECK(m.selfCPU == 0);
		std::ostringstream output;
		c.write(output, "GLOB2_PERF_SAMPLE", 1, false);
		CHECK(output.str().find("cpu_samples=0 cpu_total_ns=na") != std::string::npos);
		c.reset();
		c.clock = original;
	}
	TEST_CASE("descriptors distinguish GPU clocks and frame aggregate populations")
	{
		auto c = std::make_unique<Collector>();
		c->output = true;
		std::ostringstream output;
		struct Restore
		{
			std::streambuf *original;
			~Restore() { std::cout.rdbuf(original); }
		} restore{std::cout.rdbuf(output.rdbuf())};
		c->describe("test=descriptors");
		std::istringstream lines(output.str());
		std::string line;
		bool elapsed = false, timestamp = false, latency = false, wall = false;
		while (std::getline(lines, line))
		{
			if (line.find("scope=render.gpu.execution ") != std::string::npos)
			{
				elapsed = true;
				CHECK(line.find("source=gpu_elapsed_query") != std::string::npos);
				CHECK(line.find("population=sampled_gpu_frames") != std::string::npos);
			}
			if (line.find("scope=render.gpu.render_passes ") != std::string::npos)
			{
				timestamp = true;
				CHECK(line.find("source=gpu_timestamp_query") != std::string::npos);
			}
			if (line.find("scope=render.capture_latency ") != std::string::npos)
			{
				latency = true;
				CHECK(line.find("population=presented_frame_counter_deltas") != std::string::npos);
			}
			if (line.find("scope=simulation.units ") != std::string::npos)
			{
				wall = true;
				CHECK(line.find("source=monotonic_wall") != std::string::npos);
			}
		}
		CHECK(elapsed);
		CHECK(timestamp);
		CHECK(latency);
		CHECK(wall);
	}
	TEST_CASE("histogram merges preserve population and percentile upper bounds")
	{
		Moments a, b;
		for (auto value : {0, 1, 2, 3, 100})
			a.add(value);
		b.add(200);
		a.merge(b);
		std::uint64_t count = 0;
		for (auto n : a.histogram)
			count += n;
		CHECK(count == 6);
		CHECK(a.total == 306);
		CHECK(a.percentile(.5) == 3);
		CHECK(a.percentile(.99) == 200);
	}
	TEST_CASE("slow ticks retain the largest observations across absorption and reset")
	{
		auto owner = std::make_unique<Collector>(), worker = std::make_unique<Collector>();
		std::vector<std::pair<std::uint64_t, std::uint64_t>> expected, actual;
		for (unsigned i = 0; i < 512; ++i)
		{
			const auto wall = std::uint64_t((i * 37) % 521 + 1);
			SlowTick tick{i, wall, i + 10};
			(i < 256 ? worker : owner)->slowTick(tick);
			expected.emplace_back(wall, tick.cpuNs);
		}
		owner->absorb(*worker);
		for (const auto &tick : owner->slowTicks)
			actual.emplace_back(tick.wallNs, tick.cpuNs);
		std::sort(expected.rbegin(), expected.rend());
		expected.resize(64);
		std::sort(actual.rbegin(), actual.rend());
		CHECK(actual == expected);
		for (const auto &tick : worker->slowTicks)
			CHECK(tick.wallNs == 0);
		worker->slowTick({600, 1000, 42});
		owner->reset();
		owner->absorb(*worker);
		CHECK(std::count_if(owner->slowTicks.begin(), owner->slowTicks.end(),
							[](const auto &t) { return t.wallNs != 0; }) == 1);
		CHECK(std::find_if(owner->slowTicks.begin(), owner->slowTicks.end(),
						   [](const auto &t) { return t.wallNs == 1000 && t.cpuNs == 42; }) !=
			  owner->slowTicks.end());
	}
	TEST_CASE("bounded history compacts complete coverage without losing counts or moments")
	{
		auto history = std::make_unique<History>();
		const auto n = History::Capacity * 4;
		for (std::size_t i = 0; i < n; ++i)
		{
			Window w;
			w.tickStart = i;
			w.tickEnd = i + 1;
			w.startNs = i * 10;
			w.endNs = (i + 1) * 10;
			auto &m = w.metrics[unsigned(Id::Tick)];
			m.calls = 1;
			m.time.add(10);
			m.cpuSamples = 1;
			m.cpuNs = 4;
			history->append(std::move(w));
			CHECK(history->size <= History::Capacity);
		}
		Moments combined;
		std::uint64_t ticks = 0, calls = 0, cpu = 0, end = 0;
		bool coarsened = false;
		for (std::size_t i = 0; i < history->size; ++i)
		{
			const auto &w = history->window(i);
			CHECK(w.tickStart == end);
			end = w.tickEnd;
			combined.merge(w.metrics[unsigned(Id::Tick)].time);
			calls += w.metrics[unsigned(Id::Tick)].calls;
			cpu += w.metrics[unsigned(Id::Tick)].cpuNs;
			ticks += w.tickEnd - w.tickStart;
			coarsened |= w.windows > 1;
		}
		CHECK(ticks == n);
		CHECK(calls == n);
		CHECK(cpu == n * 4);
		CHECK(combined.total == n * 10);
		CHECK(combined.count == n);
		CHECK(combined.mean == 10);
		CHECK(coarsened);
		CHECK(sizeof(History) <= 12 * 1024 * 1024);
	}
	TEST_CASE("history compaction preserves surviving physical slots")
	{
		auto history = std::make_unique<History>();
		std::array<const Window *, History::Capacity> addresses{};
		for (std::size_t i = 0; i < History::Capacity; ++i)
		{
			auto w = std::make_unique<Window>();
			w->tickStart = i;
			w->tickEnd = i + 1;
			w->startNs = i * 10;
			w->endNs = (i + 1) * 10;
			history->append(std::move(*w));
			addresses[i] = &history->window(i);
		}
		auto w = std::make_unique<Window>();
		w->tickStart = History::Capacity;
		w->tickEnd = History::Capacity + 1;
		w->startNs = History::Capacity * 10;
		w->endNs = (History::Capacity + 1) * 10;
		history->append(std::move(*w));
		CHECK(&history->window(0) == addresses[0]);
		CHECK(history->window(0).windows == 2);
		for (std::size_t i = 1; i + 1 < History::Capacity; ++i)
			CHECK(&history->window(i) == addresses[i + 1]);
		CHECK(&history->window(History::Capacity - 1) == addresses[1]);
	}
	TEST_CASE("capture fully overwrites recycled slots and late GPU lookup follows logical history")
	{
		auto c = std::make_unique<Collector>();
		c->clock = fakeClock;
		timeNs = 100;
		c->reset();
		c->output = false;
		c->mode = "long previous mode";
		c->budgetNs = 7;
		c->presentationBudgetNs = 11;
		for (std::size_t i = 0; i < History::Capacity; ++i)
		{
			c->windowFlags = Suspended;
			c->workBudget.add(15, 7);
			c->frameBudget.add(20, 11);
			c->tickBudget.add(30, 7);
			c->record(Id::GPUTime, 10);
			timeNs += 1000000000ULL;
			c->capture(i + 1, true);
		}
		c->mode = "live";
		c->budgetNs = 0;
		c->presentationBudgetNs = 0;
		timeNs += 1000000000ULL;
		c->capture(History::Capacity + 1, true);
		const auto &tail = c->history->window(c->history->size - 1);
		CHECK(tail.windows == 1);
		CHECK(tail.flags == 0);
		CHECK(tail.budgetNs == 0);
		CHECK(tail.presentationBudgetNs == 0);
		CHECK(tail.work.count == 0);
		CHECK(tail.frames.count == 0);
		CHECK(tail.ticks.count == 0);
		CHECK(tail.metrics[unsigned(Id::GPUTime)].calls == 0);
		std::array<char, 24> mode{};
		std::copy_n("live", 4, mode.begin());
		CHECK(tail.mode == mode);
		for (std::size_t i = 0; i < History::Capacity * 2; ++i)
		{
			timeNs += 1000000000ULL;
			c->capture(History::Capacity + 2 + i, true);
		}
		// After repeated compaction, logical ordering differs from physical slots.
		// A delayed GPU result must still find the interval containing its timestamp.
		const auto &target = c->history->window(1);
		const auto start = c->started + target.startNs + 1;
		const auto before = target.metrics[unsigned(Id::GPUTime)].time.total;
		c->gpuResult(9, start, 300);
		CHECK(c->history->window(1).metrics[unsigned(Id::GPUTime)].time.total == before + 300);
		CHECK(c->history->window(c->history->size - 1).metrics[unsigned(Id::GPUTime)].calls == 0);
		CHECK(c->total[unsigned(Id::GPUTime)].time.total == History::Capacity * 10 + 300);
	}
	TEST_CASE("one-second windows retain history without exports and late GPU samples update "
			  "original windows")
	{
		auto &c = collector();
		const auto clock = c.clock;
		c.clock = fakeClock;
		timeNs = 100;
		c.reset();
		c.output = false;
		c.configure(0, 0, 0, "headless");
		c.record(Id::Tick, 10);
		timeNs += 1000000000ULL;
		c.capture(30);
		REQUIRE(c.history);
		REQUIRE(c.history->size == 1);
		c.gpuResult(7, 101, 300);
		CHECK(c.history->window(0).metrics[unsigned(Id::GPUTime)].time.total == 300);
		CHECK(c.total[unsigned(Id::GPUTime)].time.total == 300);
		c.capture(30, true, true);
		CHECK(c.total[unsigned(Id::GPUTime)].time.total == 300);
		c.reset();
		CHECK_FALSE(c.history);
		c.clock = clock;
	}
}
