// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <vector>
#include <memory>
#include <functional>
#include <utility>
#include <thread>
#include <atomic>
#include "ComputeExecutor.h"
#include "ComputeThreads.h"
#include <array>
#include <stdexcept>
#include <chrono>
#include <span>
#include <mutex>
#include <limits>
#include <string>
#include <Environment.h>
#include <future>

TEST_SUITE("ComputeExecutor")
{
TEST_CASE("automatic sizing respects platform limits and supports unknown CPU counts")
{
    CHECK(defaultComputeThreadCount(0) == 1);
    CHECK(defaultComputeThreadCount(1) == 1);
#ifdef __EMSCRIPTEN__
    CHECK(defaultComputeThreadCount(12) == 8);
    CHECK(defaultComputeThreadCount(128) == 8);
#else
    CHECK(defaultComputeThreadCount(12) == 12);
    CHECK(defaultComputeThreadCount(128) == 128);
#endif
}
TEST_CASE("thread configuration accepts auto and rejects malformed or overflowing overrides")
{
    CHECK(parseComputeThreadCount("auto") == 0);
    CHECK(parseComputeThreadCount("1") == 1);
    CHECK(parseComputeThreadCount("64") == 64);
    CHECK(parseComputeThreadCount("65") == 65);
    CHECK(parseComputeThreadCount("128") == 128);
    CHECK(parseComputeThreadCount(std::to_string(std::numeric_limits<unsigned>::max())) == std::numeric_limits<unsigned>::max());
    CHECK(resolveComputeThreadCount(0, 0) == 1);
#ifdef __EMSCRIPTEN__
    CHECK(resolveComputeThreadCount(0, 128) == 8);
#else
    CHECK(resolveComputeThreadCount(0, 128) == 128);
#endif
    CHECK(resolveComputeThreadCount(65, 8) == 65);
    for (const auto value : {"", "0", "-1", "+1", " 1", "1 ", "1x", "1.5", "999999999999999999999"})
        CHECK_THROWS_AS(parseComputeThreadCount(value), std::invalid_argument);
}
TEST_CASE("supported platforms execute jobs concurrently on distinct threads")
{
    if constexpr (GAGCore::ThreadSupport::available)
    {
        ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
        executor.configure(2);
        REQUIRE(executor.threadCount() == 2);
        std::atomic<unsigned> entered{0};
        std::atomic<bool> overlapped{true};
        std::array<std::thread::id, 2> ids;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        executor.run(2, [&](size_t i) {
            ids[i] = std::this_thread::get_id();
            ++entered;
            while (entered.load() < 2 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            if (entered.load() < 2) overlapped = false;
        });
        REQUIRE(overlapped.load());
        REQUIRE(ids[0] != ids[1]);
    }
}
TEST_CASE("exclusive slots; nested batches; reuse; errors; barriers and reconfiguration")
{
	ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
	for (unsigned threads : {1, 2, 4, 8, 1})
	{
		executor.configure(threads);
		std::array<std::atomic<int>, 8> occupied{};
		for (int repeat = 0; repeat < 50; ++repeat)
		{
			std::vector<int> output(137, 0);
			executor.run(0, [](size_t) { throw std::runtime_error("empty batch ran"); });
			executor.run(output.size(), [&](size_t i) {
				const auto slot = executor.slot();
				REQUIRE(slot < executor.threadCount());
				REQUIRE(occupied[slot].fetch_add(1) == 0);
				executor.run(3, [&](size_t j) { output[i] += int(i + j); });
				REQUIRE(occupied[slot].fetch_sub(1) == 1);
			});
			for (size_t i = 0; i < output.size(); ++i) REQUIRE(output[i] == int(3 * i + 3));
		}
		bool caught = false;
		try { executor.run(20, [](size_t i) { if (i == 7) throw std::runtime_error("job failure"); }); }
		catch (const std::runtime_error &) { caught = true; }
		REQUIRE(caught);
		int one = 0;
		executor.run(1, [&](size_t) { ++one; });
		REQUIRE(one == 1);
	}
	int launched = 0;
	executor.configure(4, [&](std::function<void()> function) {
		if (++launched == 2) throw std::runtime_error("injected thread creation failure");
		return std::thread(std::move(function));
	});
	REQUIRE(executor.threadCount() == 1);
	int completed = 0;
	executor.run(7, [&](size_t) { ++completed; });
	REQUIRE(completed == 7);
	MESSAGE("PASS executor: exclusive slots, nested batches, reuse, errors, barriers, reconfiguration");
}
}

TEST_SUITE("ComputeExecutor")
{
namespace
{
struct Counter { std::atomic<int> value{0}; };
void bump(void* context, std::size_t index) { static_cast<Counter*>(context)->value.fetch_add(int(index) + 1); }
struct Slow { std::atomic<int> running{0}, overlaps{0}, done{0}; };
void slowJob(void* context, std::size_t)
{
	auto* slow = static_cast<Slow*>(context);
	if (slow->running.fetch_add(1) != 0) slow->overlaps.fetch_add(1);
	std::this_thread::sleep_for(std::chrono::microseconds(200));
	slow->running.fetch_sub(1); slow->done.fetch_add(1);
}
void failing(void*, std::size_t) { throw std::runtime_error("deferred job failed"); }
}
TEST_CASE("deferred batches complete with lanes and isolated errors")
{
	for (unsigned threads : {1u, 2u, 4u})
	{
		CAPTURE(threads);
		ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
		executor.configure(threads);
		// Joining the newest batch first completes it; equal deadlines keep submission order.
		Counter a, b, c;
		ComputeExecutor::Group ga{5, {bump, &a}}, gb{7, {bump, &b}}, gc{3, {bump, &c}};
		auto A = executor.submit(std::span(&ga, 1)), B = executor.submit(std::span(&gb, 1)), C = executor.submit(std::span(&gc, 1));
		CHECK_FALSE(C.empty());
		executor.join(C);
		// C's jobs are done; A's and B's were all claimed before them but a
		// worker may still be finishing one, so their values are checked after
		// their own joins.
		CHECK(c.value.load() == 6); CHECK(C.empty());
		executor.join(A); executor.join(B);
		CHECK(a.value.load() == 15); CHECK(b.value.load() == 28);
		CHECK(executor.liveBatches() == 0);
		// One lane's jobs across batches never overlap.
		Slow slow; std::vector<ComputeExecutor::Batch> batches;
		for (int tick = 0; tick < 6; ++tick) { ComputeExecutor::Group g{1, {slowJob, &slow}, 3}; batches.push_back(executor.submit(std::span(&g, 1))); }
		for (auto& batch : batches) executor.join(batch);
		CHECK(slow.overlaps.load() == 0); CHECK(slow.done.load() == 6);
		// A failing batch rethrows at its join and leaves other batches intact.
		ComputeExecutor::Group bad{2, {failing, nullptr}}, good{4, {bump, &a}};
		auto Bad = executor.submit(std::span(&bad, 1)); auto Good = executor.submit(std::span(&good, 1));
		CHECK_THROWS_WITH_AS(executor.join(Bad), "deferred job failed", std::runtime_error);
		executor.join(Good);
		CHECK(a.value.load() == 25);
		// Blocking batches run while deferred work is live.
		Counter d; ComputeExecutor::Group gd{3, {bump, &d}};
		auto D = executor.submit(std::span(&gd, 1));
		std::atomic<int> ran{0};
		executor.run(8, [&](std::size_t) { ran.fetch_add(1); });
		CHECK(ran.load() == 8);
		executor.join(D);
		CHECK(d.value.load() == 6);
		// Submitting or joining from inside a job is a logic error; nested run() stays inline.
		executor.run(1, [&](std::size_t) {
			ComputeExecutor::Group g{1, {bump, &d}};
			CHECK_THROWS_AS(executor.submit(std::span(&g, 1)), std::logic_error);
			int nested = 0; executor.run(3, [&](std::size_t) { ++nested; }); CHECK(nested == 3);
		});
		// Blocking batches of changing sizes interleave with deferred work; a
		// worker arriving late for a finished batch must never touch the next one.
		{
			Slow interleaved;
			std::vector<ComputeExecutor::Batch> live;
			for (int round = 0; round < 120; ++round)
			{
				ComputeExecutor::Group g{1, {slowJob, &interleaved}, 3u};
				live.push_back(executor.submit(std::span(&g, 1)));
				std::atomic<int> ran{0};
				const std::size_t n = 1 + (round * 7) % 11;
				executor.run(n, [&](std::size_t) { ran.fetch_add(1); });
				executor.run(13, [&](std::size_t) { ran.fetch_add(1); });
				REQUIRE(ran.load() == int(n) + 13);
				if (round % 5 == 4) { for (auto& batch : live) executor.join(batch); live.clear(); }
			}
			for (auto& batch : live) executor.join(batch);
			CHECK(interleaved.done.load() == 120); CHECK(interleaved.overlaps.load() == 0);
			a.value.store(25); // keep the metric expectations below independent of this block
		}
		// Every deferred job so far has been joined and counted once.
		CHECK(executor.metrics().deferredBatches == 12 + 120); CHECK(executor.metrics().deferredJobs == 30 + 120);
		CHECK(executor.metrics().ownerJobs + executor.metrics().workerJobs == 30 + 120);
		CHECK(executor.metrics().ownerJobs == (executor.threadCount() == 1 ? 30u + 120u : 0u));
		// Empty submissions are empty batches; reconfiguration joins live work
		// and starts the metrics over.
		CHECK(executor.submit({}).empty());
		ComputeExecutor::Group tail{2, {bump, &d}};
		executor.submit(std::span(&tail, 1));
		executor.configure(1);
		CHECK(d.value.load() == 6 + 3);
		CHECK(executor.liveBatches() == 0);
		CHECK(executor.metrics().deferredBatches == 0); CHECK(executor.metrics().ownerJobs + executor.metrics().workerJobs == 0);
	}
}

TEST_CASE("idle workers retain parallelism for independent and mixed lane groups")
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    for (const auto mode : {ComputeTelemetryMode::Off, ComputeTelemetryMode::Waits,
                            ComputeTelemetryMode::Trace})
    {
        ComputeExecutor executor;
        executor.configureTelemetry(mode);
        executor.configure(3);
        for (unsigned shape = 0; shape < 4; ++shape)
        {
            CAPTURE(mode); CAPTURE(shape);
            struct Gate {
                std::atomic<unsigned> entered{0};
                std::atomic<bool> release{false};
                static void run(void* value, std::size_t) {
                    auto& gate = *static_cast<Gate*>(value);
                    ++gate.entered;
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                    while (!gate.release && std::chrono::steady_clock::now() < deadline)
                        std::this_thread::yield();
                }
            } gate;
            // Let both workers park between bursts. A lone notification must
            // not serialize a batch that has two independent runnable heads.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const ComputeExecutor::Job job{Gate::run, &gate};
            std::array<ComputeExecutor::Group, 2> groups;
            switch (shape) {
                case 0: groups = {{{2, job}, {0, job}}}; break;
                case 1: groups = {{{1, job, 4}, {1, job, 5}}}; break;
                case 2: groups = {{{2, job, 4}, {1, job, 5}}}; break;
                default: groups = {{{1, job, 4}, {1, job}}}; break;
            }
            auto batch = executor.submit(groups, shape);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (gate.entered < 2 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            CHECK(gate.entered >= 2);
            gate.release = true;
            executor.join(batch);
            CHECK(gate.entered == (shape == 2 ? 3u : 2u));
        }
    }
}

TEST_CASE("repeated lane groups and empty groups preserve ordering and independent work")
{
    for (unsigned threads : {1u, 3u})
    {
        CAPTURE(threads);
        ComputeExecutor executor;
        executor.configure(threads);
        struct State {
            std::atomic<unsigned> entered{0}, active{0}, overlaps{0};
            std::atomic<bool> release{false}, first{true};
            std::mutex mutex;
            std::vector<std::size_t> order;
            static void wait(State& state) {
                ++state.entered;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!state.release && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::yield();
            }
            static void ordered(void* value, std::size_t index) {
                auto& state = *static_cast<State*>(value);
                if (state.active.fetch_add(1)) ++state.overlaps;
                if (state.first.exchange(false)) wait(state);
                { std::lock_guard lock(state.mutex); state.order.push_back(index); }
                --state.active;
            }
            static void independent(void* value, std::size_t) { wait(*static_cast<State*>(value)); }
        } state;
        const ComputeExecutor::Job job{State::ordered, &state};
        const std::array<ComputeExecutor::Group, 5> groups{{
            {0, job, 4}, {2, job, 4}, {0, job, 4}, {2, job, 4},
            {1, {State::independent, &state}}}};
        // Serial fallback starts at join; threaded submission must expose both heads.
        if (threads == 1 || !GAGCore::ThreadSupport::available) state.release = true;
        auto batch = executor.submit(groups, 10);
        if (!state.release)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (state.entered < 2 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            CHECK(state.entered == 2);
            state.release = true;
        }
        executor.join(batch);
        CHECK(state.order == std::vector<std::size_t>{0, 1, 0, 1});
        CHECK(state.overlaps == 0);
        CHECK(state.entered == 2);
        CHECK(executor.liveBatches() == 0);
    }
}

TEST_CASE("external completion and continuation wake parked lane successors")
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    for (const auto mode : {ComputeTelemetryMode::Off, ComputeTelemetryMode::Waits,
                            ComputeTelemetryMode::Trace})
    {
        ComputeExecutor executor;
        executor.configureTelemetry(mode);
        executor.configure(2);
        for (bool resume : {false, true})
        for (bool early : {false, true})
        {
            CAPTURE(mode); CAPTURE(resume); CAPTURE(early);
            struct Suspended {
                ComputeExecutor& executor;
                std::mutex mutex;
                std::condition_variable ready;
                ComputeExecutor::CompletionTicket ticket;
                std::atomic<bool> allowReturn{true};
                static void run(void* value, std::size_t) {
                    auto& state = *static_cast<Suspended*>(value);
                    auto ticket = state.executor.defer();
                    { std::lock_guard lock(state.mutex); state.ticket = std::move(ticket); }
                    state.ready.notify_one();
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                    while (!state.allowReturn && std::chrono::steady_clock::now() < deadline)
                        std::this_thread::yield();
                }
            } state{executor};
            state.allowReturn = !early;
            const ComputeExecutor::Group suspend{1, {Suspended::run, &state}, 7};
            auto first = executor.submit(std::span(&suspend, 1), 10);
            ComputeExecutor::CompletionTicket ticket;
            {
                std::unique_lock lock(state.mutex);
                REQUIRE(state.ready.wait_for(lock, std::chrono::seconds(5), [&] { return bool(state.ticket); }));
                ticket = state.ticket;
            }
            Counter continued, successor, unrelated;
            const ComputeExecutor::Group next{3, {bump, &successor}, 7};
            auto second = executor.submit(std::span(&next, 1), 11);
            const ComputeExecutor::Group loose{1, {bump, &unrelated}};
            auto other = executor.submit(std::span(&loose, 1), 11);
            // The late case parks the worker after the callback returns. The
            // early case resolves the ticket while its callback is still active.
            if (!early)
            {
                executor.join(other);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            CHECK(successor.value == 0);
            CHECK_FALSE(executor.finished(first));
            CHECK(resume ? ticket->resume({bump, &continued}) : ticket->complete());
            state.allowReturn = true;
            executor.join(other);
            executor.join(first);
            executor.join(second);
            CHECK(continued.value == (resume ? 1 : 0));
            CHECK(successor.value == 6);
        }
    }
}
}

TEST_SUITE("ComputeExecutor")
{
namespace
{
struct Ordered { std::mutex mutex; std::vector<int> order; std::atomic<int> running{0}, overlaps{0}; };
void orderedJob(void* context, std::size_t index)
{
	auto* ordered = static_cast<Ordered*>(context);
	if (ordered->running.fetch_add(1) != 0) ordered->overlaps.fetch_add(1);
	std::this_thread::sleep_for(std::chrono::microseconds(50));
	{ std::lock_guard<std::mutex> lock(ordered->mutex); ordered->order.push_back(int(index)); }
	ordered->running.fetch_sub(1);
}
struct Gate { std::atomic<bool> open{false}; std::atomic<int> passed{0}; };
void gatedJob(void* context, std::size_t)
{
	auto* gate = static_cast<Gate*>(context);
	while (!gate->open.load()) std::this_thread::yield();
	gate->passed.fetch_add(1);
}
}
TEST_CASE("lane groups run in index order and the horizon is bounded")
{
	for (unsigned threads : {1u, 3u})
	{
		CAPTURE(threads);
		ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
		executor.configure(threads);
		// Several jobs of one lane group are sequenced by their offsets.
		Ordered ordered;
		ComputeExecutor::Group group{6, {orderedJob, &ordered}, 1};
		auto batch = executor.submit(std::span(&group, 1));
		executor.join(batch);
		CHECK(ordered.overlaps.load() == 0);
		CHECK(ordered.order == std::vector<int>{0, 1, 2, 3, 4, 5});
		// finished() reports the batch state while it is gated.
		Gate gate;
		ComputeExecutor::Group gated{1, {gatedJob, &gate}, 2};
		auto live = executor.submit(std::span(&gated, 1));
		CHECK_FALSE(executor.finished(live));
		CHECK(executor.liveBatches() == 1);
		gate.open = true;
		executor.join(live);
		CHECK(executor.finished(live)); CHECK(gate.passed.load() == 1);
		// The ring holds at most Slots live batches; the overflow leaves state intact.
		Gate hold;
		ComputeExecutor::Group held{1, {gatedJob, &hold}};
		std::vector<ComputeExecutor::Batch> batches;
		for (std::size_t i = 0; i < ComputeExecutor::Slots; ++i) batches.push_back(executor.submit(std::span(&held, 1)));
		CHECK_THROWS_AS(executor.submit(std::span(&held, 1)), std::logic_error);
		CHECK(executor.liveBatches() == ComputeExecutor::Slots);
		hold.open = true;
		for (auto& b : batches) executor.join(b);
		CHECK(hold.passed.load() == int(ComputeExecutor::Slots));
		CHECK(executor.liveBatches() == 0);
	}
}
namespace
{
struct Trace { std::mutex mutex; std::vector<int> order; };
struct Tagged { Trace* trace; int tag; };
void traced(void* context, std::size_t)
{
	auto* tagged = static_cast<Tagged*>(context);
	std::lock_guard<std::mutex> lock(tagged->trace->mutex);
	tagged->trace->order.push_back(tagged->tag);
}
struct Busy { std::atomic<int> done{0}; };
void busyJob(void* context, std::size_t)
{
	std::this_thread::sleep_for(std::chrono::microseconds(200));
	static_cast<Busy*>(context)->done.fetch_add(1);
}
struct Hold { std::atomic<int> entered{0}; std::atomic<bool> open{false}; };
void heldJob(void* context, std::size_t)
{
	auto* hold = static_cast<Hold*>(context);
	hold->entered.fetch_add(1);
	while (!hold->open.load()) std::this_thread::yield();
}
}
TEST_CASE("workers run deferred batches earliest due first and the owner only waits")
{
	if constexpr (!GAGCore::ThreadSupport::available) return;
	for (unsigned threads : {3u, 5u})
	{
		CAPTURE(threads);
		ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace); executor.configure(threads);
		// Hold every worker on its own gate, queue a long deadline before short
		// ones, then free one worker: it alone runs the three in claim order.
		std::vector<std::unique_ptr<Hold>> gates;
		std::vector<ComputeExecutor::Batch> holds;
		for (unsigned w = 1; w < threads; ++w)
		{
			gates.push_back(std::make_unique<Hold>());
			ComputeExecutor::Group hold{1, {heldJob, gates.back().get()}};
			holds.push_back(executor.submit(std::span(&hold, 1), 0));
		}
		for (auto& gate : gates) while (!gate->entered.load()) std::this_thread::yield();
		Trace trace;
		Tagged late{&trace, 100}, soon{&trace, 5}, tie{&trace, 6};
		ComputeExecutor::Group lateGroup{1, {traced, &late}}, soonGroup{1, {traced, &soon}}, tieGroup{1, {traced, &tie}};
		auto L = executor.submit(std::span(&lateGroup, 1), 100);
		auto S = executor.submit(std::span(&soonGroup, 1), 5);
		auto T = executor.submit(std::span(&tieGroup, 1), 5);
		gates.front()->open = true;
		executor.join(S); executor.join(T); executor.join(L);
		// Earliest due first, ties in submission order.
		CHECK(trace.order == std::vector<int>{5, 6, 100});
		for (auto& gate : gates) gate->open = true;
		for (auto& batch : holds) executor.join(batch);
		// The owner ran none of the Shared work, its own batches included.
		CHECK(executor.metrics().ownerJobs == 0);
		CHECK(executor.metrics().workerJobs == threads - 1 + 3);
		CHECK(executor.liveBatches() == 0);
	}
}
TEST_CASE("without workers the owner runs the jobs due no later than the join")
{
	{
		ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace); executor.configure(1);
		Trace trace;
		Tagged late{&trace, 9}, soon{&trace, 3}, lane0{&trace, 10}, lane1{&trace, 11};
		ComputeExecutor::Group lateGroup{1, {traced, &late}}, soonGroup{1, {traced, &soon}};
		ComputeExecutor::Group laneA{1, {traced, &lane0}, 4}, laneB{1, {traced, &lane1}, 4};
		auto L = executor.submit(std::span(&lateGroup, 1), 9);
		auto S = executor.submit(std::span(&soonGroup, 1), 3);
		auto A = executor.submit(std::span(&laneA, 1), 9);
		auto B = executor.submit(std::span(&laneB, 1), 10);
		CHECK(trace.order.empty()); // never before a join
		executor.join(L); // runs what is due no later: S, then L (A is due at 9 but submitted after L)
		CHECK(trace.order == std::vector<int>{3, 9});
		CHECK(executor.finished(S)); CHECK_FALSE(executor.finished(A));
		executor.join(B); // the lane predecessor A is due no later, so it runs first
		CHECK(trace.order == std::vector<int>{3, 9, 10, 11});
		executor.join(S); executor.join(A);
		CHECK(executor.metrics().ownerJobs == 4); CHECK(executor.metrics().workerJobs == 0);
		CHECK(executor.liveBatches() == 0);
	}
}
TEST_CASE("a lane keeps submission order: a lane job due before a live earlier one is rejected")
{
	for (unsigned threads : {1u, 3u})
	{
		CAPTURE(threads);
		ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace); executor.configure(threads);
		Trace lane; Busy others;
		std::array<Tagged, 6> tags;
		std::vector<ComputeExecutor::Batch> batches;
		for (int tick = 0; tick < 6; ++tick)
		{
			tags[tick] = {&lane, tick};
			ComputeExecutor::Group g{1, {traced, &tags[tick]}, 7};
			batches.push_back(executor.submit(std::span(&g, 1), ComputeExecutor::boundaryDue(tick + 8)));
			// Other producers' earlier deadlines interleave without reordering the lane.
			ComputeExecutor::Group other{2, {busyJob, &others}};
			batches.push_back(executor.submit(std::span(&other, 1), ComputeExecutor::advanceDue(tick + 3)));
		}
		Tagged extra{&lane, 99};
		ComputeExecutor::Group early{1, {traced, &extra}, 7};
		CHECK_THROWS_AS(executor.submit(std::span(&early, 1), ComputeExecutor::boundaryDue(5)), std::logic_error);
		for (auto& batch : batches) executor.join(batch);
		CHECK(lane.order == std::vector<int>{0, 1, 2, 3, 4, 5});
		CHECK(others.done.load() == 12);
		// Once the lane is idle a new match may start its deadlines over.
		auto restart = executor.submit(std::span(&early, 1), ComputeExecutor::boundaryDue(5));
		executor.join(restart);
		CHECK(executor.liveBatches() == 0);
	}
}
TEST_CASE("the slot count covers every producer's horizon")
{
	static_assert(ComputeExecutor::Slots >= (ComputeExecutor::AIHorizon + 1) + (ComputeExecutor::GradientHorizon + 1)
		+ (ComputeExecutor::BuildingHorizon + 1) + (ComputeExecutor::GrowthHorizon + 1) + (ComputeExecutor::StatisticsHorizon + 1));
	static_assert(ComputeExecutor::boundaryDue(7) < ComputeExecutor::advanceDue(7) && ComputeExecutor::advanceDue(7) < ComputeExecutor::boundaryDue(8));
	CHECK(ComputeExecutor::Slots == 74);
}
}

#include "ReadOnlyPhase.h"
TEST_SUITE("ReadOnlyPhase") {
TEST_CASE("borrowed groups cover serial parallel empty and exception barriers") {
    for (unsigned threads : {1, 4}) {
        ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
        executor.configure(threads);
        ReadOnlyPhase empty;
        empty.run(executor);
        std::array<unsigned, 9> results{};
        auto first = [&](size_t i) { results[i] = 11; };
        auto second = [&](size_t i) { results[i + 3] = 22; };
        ReadOnlyPhase phase;
        phase.add(3, first); phase.add(6, second);
        phase.run(executor);
        for (size_t i = 0; i < results.size(); ++i) CHECK(results[i] == (i < 3 ? 11 : 22));
        std::atomic<unsigned> active{0};
        auto fail = [&](size_t i) {
            ++active;
            std::this_thread::yield();
            --active;
            if (i == 0) throw std::runtime_error("observation failure");
        };
        ReadOnlyPhase failing;
        failing.add(16, fail);
        CHECK_THROWS_AS(failing.run(executor), std::runtime_error);
        CHECK(active.load() == 0);
        phase.run(executor); // Executor remains usable after the error barrier.
    }
}
}

TEST_SUITE("ComputeExecutor")
{
TEST_CASE("presentation cannot delay simulation barriers with spare workers or run on the simulation owner")
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    for (unsigned threads : {3u, 5u})
    {
        ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
        executor.configure(threads);
        std::atomic<bool> entered{false}, release{false}, ownerRan{false};
        const auto owner = std::this_thread::get_id();
        auto work = executor.submitPresentation(3, [&](size_t) {
            if (std::this_thread::get_id() == owner) ownerRan = true;
            entered = true;
            while (!release.load()) std::this_thread::yield();
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!entered && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        // Watchdog releases the gate on regressions, so the test fails rather
        // than leaving the suite stuck in a barrier or destructor.
        std::atomic<bool> timedOut{false};
        std::thread watchdog([&] {
            while (!release && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
            if (!release.exchange(true)) timedOut = true;
        });
        std::atomic<int> simulation{0};
        executor.run(19, [&](size_t) { ++simulation; });
        ComputeExecutor::Group group{11, {[](void* p, size_t) { ++*static_cast<std::atomic<int>*>(p); }, &simulation}};
        auto batch = executor.submit(std::span(&group, 1));
        executor.join(batch);
        executor.joinAll();
        CHECK(entered.load());
        CHECK(simulation.load() == 30);
        CHECK_FALSE(work->finished());
        work->cancel();
        release = true;
        watchdog.join();
        executor.cancelPresentationAndWait();
        CHECK_FALSE(timedOut.load());
        CHECK_FALSE(ownerRan.load());
        CHECK(work->status() == ComputeExecutor::Presentation::Status::Canceled);
    }
}
TEST_CASE("with one worker the owner waits through its presentation chunk and never runs the join itself")
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    executor.configure(2);
    std::atomic<bool> entered{false}, release{false}, ownerRan{false};
    const auto owner = std::this_thread::get_id();
    auto work = executor.submitPresentation(2, [&](size_t) {
        if (std::this_thread::get_id() == owner) ownerRan = true;
        entered = true;
        while (!release.load()) std::this_thread::yield();
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!entered && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    REQUIRE(entered.load());
    // A lane spanning two batches and an unrelated batch, all queued behind the
    // chunk the only worker is running.
    struct Step { std::mutex* mutex; std::vector<int>* order; int tag; };
    std::mutex mutex; std::vector<int> order;
    Step first{&mutex, &order, 1}, second{&mutex, &order, 2}, other{&mutex, &order, 3};
    const auto record = [](void* context, size_t) {
        auto* step = static_cast<Step*>(context);
        std::lock_guard<std::mutex> lock(*step->mutex);
        step->order->push_back(step->tag);
    };
    ComputeExecutor::Group laneA{1, {record, &first}, 5}, laneB{1, {record, &second}, 5}, loose{1, {record, &other}};
    auto A = executor.submit(std::span(&laneA, 1), 4);
    auto B = executor.submit(std::span(&laneB, 1), 6);
    auto F = executor.submit(std::span(&loose, 1), 5);
    std::atomic<bool> released{false};
    std::thread releaser([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        released = true;
        release = true;
    });
    executor.join(B);
    // The join returned only after the chunk ended: the owner waited, the worker
    // finished its chunk and then ran the due work earliest first.
    CHECK(released.load());
    executor.join(A); executor.join(F);
    releaser.join();
    CHECK(order == std::vector<int>{1, 3, 2});
    CHECK(executor.metrics().ownerJobs == 0);
    CHECK(executor.metrics().workerJobs == 3);
    executor.cancelPresentationAndWait();
    CHECK_FALSE(ownerRan.load());
}
TEST_CASE("presentation progresses before a continuous simulation backlog drains")
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    executor.configure(2);
    struct State { std::atomic<bool> entered{false}, release{false}; std::atomic<size_t> completed{0}; } state;
    ComputeExecutor::Group group{10000, {[](void* p, size_t index) {
        auto& state = *static_cast<State*>(p);
        if (!index) {
            state.entered = true;
            while (!state.release) std::this_thread::yield();
        }
        ++state.completed;
    }, &state}};
    auto batch = executor.submit(std::span(&group, 1));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!state.entered && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    std::atomic<size_t> observed{10000};
    auto work = executor.submitPresentation(1, [&](size_t) { observed = state.completed.load(); });
    state.release = true;
    while (!work->finished() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    CHECK(work->finished());
    CHECK(observed.load() < 10000);
    executor.join(batch);
    executor.cancelPresentationAndWait();
    CHECK(state.completed.load() == 10000);
}
TEST_CASE("presentation replacement, chunk pumping, cancellation and errors retain no captures")
{
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    executor.configure(1);
    int count = 0;
    auto input = std::make_shared<int>(4);
    std::weak_ptr<int> weak = input;
    auto old = executor.submitPresentation(1, [input, &count](size_t) { count += *input; });
    input.reset();
    auto current = executor.submitPresentation(3, [&](size_t i) { count += int(i) + 1; });
    CHECK(weak.expired());
    CHECK(old->status() == ComputeExecutor::Presentation::Status::Canceled);
    executor.joinAll();
    CHECK(count == 0);
    CHECK(executor.pumpPresentation());
    CHECK(count == 1);
    CHECK_FALSE(current->finished());
    current->cancel();
    CHECK(executor.pumpPresentation());
    CHECK(count == 1);
    CHECK(current->status() == ComputeExecutor::Presentation::Status::Canceled);
    auto bad = executor.submitPresentation(1, [](size_t) { throw std::runtime_error("presentation failure"); });
    CHECK(executor.pumpPresentation());
    CHECK_THROWS_WITH_AS(bad->rethrowFailure(), "presentation failure", std::runtime_error);
    CHECK_FALSE(executor.pumpPresentation());
    auto good = executor.submitPresentation(2, [&](size_t) { ++count; });
    CHECK(executor.pumpPresentation());
    CHECK(executor.pumpPresentation());
    CHECK(good->status() == ComputeExecutor::Presentation::Status::Complete);
    CHECK(count == 3);
    CHECK(executor.presentationMetrics().replaced == 1);
    auto pending = executor.submitPresentation(1, [](size_t) { FAIL("canceled job executed"); });
    executor.configure(1);
    CHECK(pending->status() == ComputeExecutor::Presentation::Status::Canceled);
}
TEST_CASE("resumable presentation yields without advancing or retaining canceled inputs")
{
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    executor.configure(1);
    auto input=std::make_shared<int>(0);
    std::weak_ptr<int> weak=input;
    std::vector<size_t> visited;
    auto work=executor.submitResumablePresentation(2,[input,&visited](size_t chunk) {
        visited.push_back(chunk);
        return ++*input==3;
    });
    input.reset();
    for (int i=0;i<4;++i) {
        REQUIRE(executor.pumpPresentation());
        CHECK_FALSE(work->finished());
    }
    CHECK(visited==std::vector<size_t>{0,0,0,1});
    work->cancel();
    REQUIRE(executor.pumpPresentation());
    CHECK(weak.expired());
    CHECK(work->status()==ComputeExecutor::Presentation::Status::Canceled);
    CHECK(visited.size()==4);
}


}

TEST_CASE("worker only maintenance never uses the owner including creation failure" * doctest::test_suite("ComputeExecutor"))
{
    struct Service : ComputeExecutor::WorkerOnly {
        std::atomic<unsigned> calls{0};
        bool pending() const noexcept override { return calls.load()==0; }
        void process() noexcept override { ++calls; }
    };
    for(bool fail:{false,true}) {
        ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
        if(fail) executor.configure(8,[](auto)->std::thread {throw std::runtime_error("injected creation failure");});
        else executor.configure(1);
        REQUIRE(executor.threadCount()==1);
        auto service=std::make_shared<Service>(); executor.setWorkerOnly(service);
        unsigned required=0;
        executor.run(1,[&](auto){++required;});
        const ComputeExecutor::Group group{1,{[](void* p,std::size_t){++*static_cast<unsigned*>(p);},&required}};
        auto batch=executor.submit(std::span(&group,1)); executor.join(batch);
        executor.joinAll(); CHECK(required==2); CHECK(service->calls.load()==0);
    }
}
TEST_CASE("worker only maintenance yields to required jobs and cannot delay their completed join" * doctest::test_suite("ComputeExecutor"))
{
    if constexpr(!GAGCore::ThreadSupport::available) return;
    struct Service : ComputeExecutor::WorkerOnly {
        std::atomic<bool> entered{false},release{false};
        std::atomic<unsigned>& required;
        std::atomic<unsigned>& presentation;
        unsigned observed=0, observedPresentation=0; bool onWorker=false;
        Service(std::atomic<unsigned>& required,std::atomic<unsigned>& presentation):required(required),presentation(presentation) {}
        bool pending() const noexcept override { return !entered.load(); }
        void process() noexcept override {
            observed=required.load(); observedPresentation=presentation.load(); onWorker=ComputeExecutor::workerSlot()!=0; entered=true;
            while(!release.load()) std::this_thread::yield(); // test-only held pass
        }
    };
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace); executor.configure(3); REQUIRE(executor.threadCount()==3);
    std::atomic<unsigned> required{0},entered{0}; std::atomic<bool> release{false};
    struct Context {std::atomic<unsigned>& required;std::atomic<unsigned>& entered;std::atomic<bool>& release;} context{required,entered,release};
    const ComputeExecutor::Group first{2,{[](void* p,std::size_t){
        auto& c=*static_cast<Context*>(p);++c.entered;while(!c.release.load())std::this_thread::yield();++c.required;
    },&context}};
    auto a=executor.submit(std::span(&first,1));
    while(entered.load()!=2) std::this_thread::yield();
    std::atomic<unsigned> presentation{0};
    auto ticket=executor.submitPresentation(12,[&](auto){++presentation;});
    auto service=std::make_shared<Service>(required,presentation); executor.setWorkerOnly(service);
    const ComputeExecutor::Group second{1,{[](void* p,std::size_t){++*static_cast<std::atomic<unsigned>*>(p);},&required}};
    auto b=executor.submit(std::span(&second,1)); release=true;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!service->entered.load() && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    const bool ran=service->entered.load();
    if(ran) { executor.join(a); executor.join(b); }
    service->release=true;
    CHECK(ran); if(ran) { CHECK(service->observed==3); CHECK(service->observedPresentation==12); CHECK(service->onWorker); }
    executor.join(a);executor.join(b);
    CHECK(executor.threadCount()==3);
}

TEST_CASE("suspended device work releases the only worker and preserves lane completion" * doctest::test_suite("ComputeExecutor"))
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace); executor.configure(2);
    struct Context {
        ComputeExecutor& executor;
        std::mutex mutex;
        std::condition_variable ready;
        ComputeExecutor::CompletionTicket ticket;
    } context{executor};
    const ComputeExecutor::Group device{1, {[](void* value, std::size_t) {
        auto& state = *static_cast<Context*>(value);
        auto ticket = state.executor.defer();
        { std::lock_guard lock(state.mutex); state.ticket = std::move(ticket); }
        state.ready.notify_one();
    }, &context}, 9};
    auto first = executor.submit(std::span(&device, 1), 10);
    ComputeExecutor::CompletionTicket ticket;
    {
        std::unique_lock lock(context.mutex);
        REQUIRE(context.ready.wait_for(lock, std::chrono::seconds(5), [&] { return bool(context.ticket); }));
        ticket = context.ticket;
    }
    std::atomic<unsigned> unrelated{0}, successor{0};
    auto increment = [](void* value, std::size_t) { ++*static_cast<std::atomic<unsigned>*>(value); };
    const ComputeExecutor::Group next{1, {increment, &successor}, 9}, other{1, {increment, &unrelated}};
    auto second = executor.submit(std::span(&next, 1), 11);
    auto loose = executor.submit(std::span(&other, 1), 11);
    executor.join(loose);
    CHECK(unrelated == 1); CHECK(successor == 0); CHECK_FALSE(executor.finished(first));
    CHECK(ticket->complete()); CHECK_FALSE(ticket->complete());
    executor.join(first); executor.join(second);
    CHECK(successor == 1); CHECK_FALSE(ticket->complete());
}

TEST_CASE("early device failure resumes exactly once on the worker at its original lane position" * doctest::test_suite("ComputeExecutor"))
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace); executor.configure(2);
    struct Context {
        ComputeExecutor& executor;
        std::atomic<unsigned> recovered{0}, successor{0};
        std::atomic<bool> resumed{false}, duplicate{false}, worker{false};
    } context{executor};
    const ComputeExecutor::Group first{1, {[](void* value, std::size_t) {
        auto& state = *static_cast<Context*>(value);
        auto ticket = state.executor.defer();
        const ComputeExecutor::Job recover{[](void* value, std::size_t) {
            auto& state = *static_cast<Context*>(value);
            state.worker = ComputeExecutor::workerSlot() != 0;
            ++state.recovered;
        }, value};
        state.resumed = ticket->resume(recover);
        state.duplicate = ticket->resume(recover);
    }, &context}, 10};
    const ComputeExecutor::Group next{1, {[](void* value, std::size_t) {
        auto& state = *static_cast<Context*>(value);
        if (state.recovered == 1) ++state.successor;
    }, &context}, 10};
    auto a = executor.submit(std::span(&first, 1), 3);
    auto b = executor.submit(std::span(&next, 1), 4);
    executor.join(a); executor.join(b);
    CHECK(context.resumed); CHECK_FALSE(context.duplicate); CHECK(context.worker);
    CHECK(context.recovered == 1); CHECK(context.successor == 1);
}

TEST_CASE("early asynchronous completion cannot release an invoking callback's inputs" * doctest::test_suite("ComputeExecutor"))
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace); executor.configure(2);
    struct Context { ComputeExecutor& executor; std::atomic<bool> finishedInside{true}; } context{executor};
    const ComputeExecutor::Group group{1, {[](void* value, std::size_t) {
        auto& state = *static_cast<Context*>(value);
        auto ticket = state.executor.defer();
        if (!ticket->complete(std::make_exception_ptr(std::runtime_error("device failure")))) return;
        state.finishedInside = state.executor.liveBatches() == 0;
    }, &context}};
    auto batch = executor.submit(std::span(&group, 1));
    CHECK_THROWS_AS(executor.join(batch), std::runtime_error);
    CHECK_FALSE(context.finishedInside);
}

TEST_CASE("continuation of a nonzero lane-group index keeps its original sequence" * doctest::test_suite("ComputeExecutor"))
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace); executor.configure(2);
    struct Context { ComputeExecutor& executor; std::vector<unsigned> order; } context{executor};
    const ComputeExecutor::Group group{3, {[](void* value,std::size_t index) {
        auto& state=*static_cast<Context*>(value);
        if(index==1) {
            auto ticket=state.executor.defer();
            ticket->resume({[](void* value,std::size_t index) {
                auto& state=*static_cast<Context*>(value);
                CHECK(index==0); state.order.push_back(1);
            },value});
        } else state.order.push_back(unsigned(index));
    },&context},12};
    auto batch=executor.submit(std::span(&group,1)); executor.join(batch);
    CHECK(context.order==std::vector<unsigned>{0,1,2});
}

TEST_CASE("retained completion tickets can be resolved after executor destruction" * doctest::test_suite("ComputeExecutor"))
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor::CompletionTicket ticket;
    {
        ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace); executor.configure(2);
        struct Context { ComputeExecutor& executor; ComputeExecutor::CompletionTicket& ticket; } context{executor,ticket};
        const ComputeExecutor::Group group{1,{[](void* value,std::size_t) {
            auto& state=*static_cast<Context*>(value);
            state.ticket=state.executor.defer(); state.ticket->complete();
        },&context}};
        auto batch=executor.submit(std::span(&group,1)); executor.join(batch);
    }
    CHECK_FALSE(ticket->complete());
}

TEST_CASE("graphics batches finish once, retain failures and release captures" *
          doctest::test_suite("ComputeExecutor"))
{
    for (unsigned threads : {1u, 2u, 5u})
    {
        ComputeExecutor executor;
        executor.configureTelemetry(ComputeTelemetryMode::Trace);
        executor.configure(threads);
        auto retained = std::make_shared<int>(42);
        std::weak_ptr<int> weak = retained;
        std::array<std::atomic<unsigned>, 37> results{};
        auto batch = executor.submitPresentationBatch(
            results.size(), {2},
            [&, retained](auto context)
            {
                ++results[context.index];
                CHECK((ComputeExecutor::workerSlot() != 0 || threads == 1));
            });
        retained.reset();
        executor.finishPresentationBatch(batch);
        executor.finishPresentationBatch(batch);
        CHECK(batch.status() == ComputeExecutor::Presentation::Status::Complete);
        CHECK(weak.expired());
        for (const auto &result : results)
            CHECK(result.load() == 1);
        auto empty = executor.submitPresentationBatch(0, {2}, {});
        CHECK(empty.finished());
        executor.finishPresentationBatch(empty);
        auto bad = executor.submitPresentationBatch(
            8, {2}, [](auto) { throw std::runtime_error("graphics error"); });
        CHECK_THROWS_WITH_AS(executor.finishPresentationBatch(bad), "graphics error",
                             std::runtime_error);
        CHECK(bad.finished());
        CHECK_THROWS_WITH_AS(bad.rethrowFailure(), "graphics error", std::runtime_error);
    }
}
TEST_CASE(
    "serial graphics finish never pumps scene or required work and abandonment releases captures" *
    doctest::test_suite("ComputeExecutor"))
{
    ComputeExecutor executor;
        executor.configureTelemetry(ComputeTelemetryMode::Trace);
    unsigned scene = 0, simulation = 0, graphics = 0;
    auto preparation = executor.submitPresentation(3, [&](auto) { ++scene; });
    ComputeExecutor::Group group{
        3, {[](void *p, std::size_t) { ++*static_cast<unsigned *>(p); }, &simulation}};
    auto required = executor.submit(std::span(&group, 1));
    auto batch = executor.submitPresentationBatch(3, {2}, [&](auto) { ++graphics; });
    executor.finishPresentationBatch(batch);
    CHECK(graphics == 3);
    CHECK(scene == 0);
    CHECK(simulation == 0);
    auto capture = std::make_shared<int>(1);
    std::weak_ptr<int> weak = capture;
    {
        auto abandoned = executor.submitPresentationBatch(3, {2}, [capture](auto) {});
        capture.reset();
    }
    CHECK(weak.expired());
    executor.join(required);
    executor.cancelPresentationAndWait();
}
TEST_CASE("graphics cancellation drains claimed work and reconfiguration releases inputs" *
          doctest::test_suite("ComputeExecutor"))
{
#ifndef __EMSCRIPTEN__
    ComputeExecutor executor;
        executor.configureTelemetry(ComputeTelemetryMode::Trace);
    executor.configure(4);
    std::mutex mutex;
    std::condition_variable changed;
    unsigned entered = 0;
    bool release = false;
    auto retained = std::make_shared<int>(7);
    std::weak_ptr<int> weak = retained;
    auto batch = executor.submitPresentationBatch(50, {2},
                                                  [&, retained](auto context)
                                                  {
                                                      std::unique_lock lock(mutex);
                                                      ++entered;
                                                      changed.notify_all();
                                                      changed.wait(lock, [&] { return release; });
                                                      CHECK(context.cancellation.canceled());
                                                  });
    retained.reset();
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered == 2; }));
    }
    batch.cancel();
    CHECK_FALSE(batch.finished());
    CHECK_FALSE(weak.expired());
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    changed.notify_all();
    executor.finishPresentationBatch(batch);
    CHECK(entered == 2);
    CHECK(weak.expired());
    CHECK(batch.status() == ComputeExecutor::Presentation::Status::Canceled);
    // A pending batch with no workers is canceled by reconfiguration.
    executor.configure(1);
    retained = std::make_shared<int>(9);
    weak = retained;
    auto pending = executor.submitPresentationBatch(3, {2}, [retained](auto) {});
    retained.reset();
    executor.configure(3);
    CHECK(pending.finished());
    CHECK(weak.expired());
#endif
}
TEST_CASE("both presentation classes progress during required lane backlog" *
          doctest::test_suite("ComputeExecutor"))
{
#ifndef __EMSCRIPTEN__
    ComputeExecutor executor;
        executor.configureTelemetry(ComputeTelemetryMode::Trace);
    executor.configure(2);
    std::atomic<unsigned> required{0}, scene{0}, graphics{0};
    std::atomic<bool> sceneEarly{false}, graphicsEarly{false};
    ComputeExecutor::Group group{
        512,
        {[](void *p, std::size_t) { ++*static_cast<std::atomic<unsigned> *>(p); }, &required},
        3};
    // Hold the sole worker until all three classes are admitted.
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, release = false;
    ComputeExecutor::Group gate{1,
                                {[](void *p, std::size_t)
                                 {
                                     auto &invoke = *static_cast<std::function<void()> *>(p);
                                     invoke();
                                 },
                                 nullptr}};
    std::function<void()> hold = [&]
    {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return release; });
    };
    gate.job.context = &hold;
    auto held = executor.submit(std::span(&gate, 1));
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }));
    }
    auto simulation = executor.submit(std::span(&group, 1));
    auto preparation = executor.submitPresentation(4,
                                                   [&](auto)
                                                   {
                                                       if (required.load() < 512)
                                                           sceneEarly.store(true);
                                                       ++scene;
                                                   });
    auto batch = executor.submitPresentationBatch(4, {2},
                                                  [&](auto)
                                                  {
                                                      if (required.load() < 512)
                                                          graphicsEarly.store(true);
                                                      ++graphics;
                                                  });
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    changed.notify_all();
    executor.finishPresentationBatch(batch);
    executor.join(held);
    executor.join(simulation);
    executor.cancelPresentationAndWait();
    CHECK(graphics == 4);
    CHECK(scene > 0);
    CHECK(sceneEarly.load());
    CHECK(graphicsEarly.load());
#endif
}
TEST_CASE("graphics callback barriers are rejected and completed tickets do not pump newer work" *
          doctest::test_suite("ComputeExecutor"))
{
    ComputeExecutor executor;
        executor.configureTelemetry(ComputeTelemetryMode::Trace);
    auto first = executor.submitPresentationBatch(
        1, {2},
        [&](auto)
        {
            auto empty = ComputeExecutor::PresentationBatchTicket{};
            CHECK_THROWS_AS(executor.finishPresentationBatch(empty), std::logic_error);
            CHECK_THROWS_AS(executor.configure(1), std::logic_error);
            CHECK_THROWS_AS(executor.submitPresentationBatch(0, {2}, {}), std::logic_error);
        });
    executor.finishPresentationBatch(first);
    first.cancel();
    CHECK(first.status() == ComputeExecutor::Presentation::Status::Complete);
    unsigned ran = 0;
    auto second = executor.submitPresentationBatch(2, {2}, [&](auto) { ++ran; });
    executor.finishPresentationBatch(first);
    CHECK(ran == 0);
    ComputeExecutor other;
    CHECK_THROWS_AS(other.finishPresentationBatch(second), std::logic_error);
    executor.finishPresentationBatch(second);
    CHECK(ran == 2);
}
TEST_CASE("empty graphics submissions bypass occupied admission and concurrency metrics respect "
          "ceilings" *
          doctest::test_suite("ComputeExecutor"))
{
    ComputeExecutor executor;
        executor.configureTelemetry(ComputeTelemetryMode::Trace);
    auto pending = executor.submitPresentationBatch(2, {1}, [](auto) {});
    auto empty = executor.submitPresentationBatch(0, {2}, {});
    CHECK(empty.finished());
    executor.finishPresentationBatch(empty);
    CHECK_FALSE(pending.finished());
    executor.finishPresentationBatch(pending);
    CHECK(executor.graphicsMetrics().parallelBatches == 0);
    for (unsigned threads : {2u, 4u})
    {
        executor.configure(threads);
        auto one = executor.submitPresentationBatch(10, {1}, [](auto) {});
        executor.finishPresentationBatch(one);
        CHECK(executor.graphicsMetrics().parallelBatches == 0);
    }
    ComputeExecutor::PresentationBatchTicket retained;
    auto input = std::make_shared<int>(1);
    std::weak_ptr<int> weak = input;
    {
        ComputeExecutor retired;
        retained = retired.submitPresentationBatch(4, {2}, [input](auto) {});
        input.reset();
    }
    CHECK(retained.finished());
    CHECK(weak.expired());
}
TEST_CASE("graphics capture destruction can inspect scheduler metrics before completion" * doctest::test_suite("ComputeExecutor"))
{
    for(unsigned threads : {1u,4u}) {
        ComputeExecutor executor;
        executor.configureTelemetry(ComputeTelemetryMode::Trace);executor.configure(threads);
        std::atomic<bool> released{false};
        auto capture=std::shared_ptr<int>(new int(1),[&](int* value) {
            const auto metrics=executor.graphicsMetrics();
            CHECK(metrics.jobs==1);
            released.store(true);delete value;
        });
        auto batch=executor.submitPresentationBatch(1,{2},[capture](auto){});capture.reset();
        executor.finishPresentationBatch(batch);CHECK(released.load());
    }
}

TEST_SUITE("ComputeExecutor")
{
namespace {
struct TelemetryCounter { std::atomic<int> value{0}; };
void telemetryBump(void* context, std::size_t index) { static_cast<TelemetryCounter*>(context)->value.fetch_add(int(index)+1); }
std::atomic<std::uint64_t> diagnosticTime{0};
std::uint64_t diagnosticClock() noexcept { return diagnosticTime.fetch_add(10); }
std::promise<void>* inspected = nullptr;
std::atomic<unsigned> waitClockReads{0};
std::uint64_t waitClock() noexcept {
    const auto read = waitClockReads.fetch_add(1);
    if (read == 2) inspected->set_value();
    return read*10;
}
struct TelemetryGate {
    std::promise<void> entered, release;
    std::shared_future<void> released = release.get_future().share();
    static void run(void* context, std::size_t) {
        auto& gate = *static_cast<TelemetryGate*>(context);
        gate.entered.set_value(); gate.released.wait();
    }
};
const ComputeTelemetry::JoinSummary& summary(const ComputeTelemetry::Snapshot& s, ComputeProducer producer, JoinReason reason) {
    return s.summary.joins[unsigned(producer)][unsigned(reason)];
}
}
TEST_CASE("disabled diagnostics do not read the injected clock or collect counters")
{
    diagnosticTime = 0;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Off, 0, diagnosticClock);
    for (unsigned threads : {1u, 3u}) {
        executor.configure(threads);
        TelemetryCounter counter;
        ComputeExecutor::Group group{7, {telemetryBump, &counter}};
        auto batch = executor.submit(std::span(&group, 1), 3, {ComputeProducer::Hiring, 1, 3});
        executor.join(batch, {JoinReason::TickDeadline, 3});
        executor.run(9, [](auto) {});
        executor.advancingTick(3);
        CHECK(counter.value == 28);
        CHECK(executor.metrics().deferredJobs == 0);
        CHECK(executor.activeNs() == 0);
        CHECK_FALSE(executor.telemetrySnapshot().available);
    }
    CHECK(diagnosticTime == 0);
}
TEST_CASE("owner execution, stale handles, reuse and lifecycle attribution use one locked inspection")
{
    diagnosticTime = 0;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Waits, 0, diagnosticClock);
    TelemetryCounter counter;
    ComputeExecutor::Group group{1, {telemetryBump, &counter}};
    auto batch = executor.submit(std::span(&group, 1), 4, {ComputeProducer::Hiring, 1, 2});
    auto stale = batch;
    executor.join(batch, {JoinReason::TickDeadline, 2});
    auto snapshot = executor.telemetrySnapshot();
    const auto& value = summary(snapshot, ComputeProducer::Hiring, JoinReason::TickDeadline);
    CHECK(value.joined == 1); CHECK(value.incomplete == 1);
    CHECK(value.ownerExecutions == 1); CHECK(value.backgroundAttempts == 0);
    CHECK(value.lock.totalNs == 10); CHECK(value.owner.totalNs == 10); CHECK(value.total.totalNs == 40);
    CHECK(value.owner.histogram[4] == 1);
    auto reused = executor.submit(std::span(&group, 1), 5, {ComputeProducer::Mixed, 2, 3});
    executor.join(stale, {JoinReason::TickDeadline, 2});
    CHECK(stale.empty());
    for (auto reason : {JoinReason::Save, JoinReason::Verification, JoinReason::Reconfigure, JoinReason::Teardown}) {
        executor.join(reused, {reason, ComputeTelemetry::NoTick});
        CHECK(summary(executor.telemetrySnapshot(), ComputeProducer::Mixed, reason).joined == 1);
        reused = executor.submit(std::span(&group, 1), 5, {ComputeProducer::Mixed, 2, 3});
    }
    executor.joinAll();
    snapshot = executor.telemetrySnapshot();
    CHECK(summary(snapshot, ComputeProducer::Hiring, JoinReason::TickDeadline).joined == 1);
    CHECK(snapshot.summary.backgroundTicks == 0);
    executor.resetTelemetry();
    CHECK(summary(executor.telemetrySnapshot(), ComputeProducer::Hiring, JoinReason::TickDeadline).joined == 0);
}
TEST_CASE("worker waits aggregate distinct ticks globally and per producer without worker timing")
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
    executor.configure(2);
    executor.configureTelemetry(ComputeTelemetryMode::Waits, 0, waitClock);
    for (auto producer : {ComputeProducer::Hiring, ComputeProducer::Hiring, ComputeProducer::PeriodicGradient}) {
        TelemetryGate gate;
        std::promise<void> inspection; inspected = &inspection; waitClockReads = 0;
        auto inspectionDone = inspection.get_future();
        ComputeExecutor::Group group{1, {TelemetryGate::run, &gate}};
        auto batch = executor.submit(std::span(&group, 1), 20, {producer, 1, 10});
        gate.entered.get_future().wait();
        std::thread release([&] { inspectionDone.wait(); gate.release.set_value(); });
        executor.join(batch, {JoinReason::TickDeadline, 10}); release.join();
    }
    executor.advancingTick(10); executor.advancingTick(10); executor.advancingTick(11);
    const auto snapshot = executor.telemetrySnapshot();
    CHECK(snapshot.summary.backgroundTicks == 1);
    CHECK(snapshot.summary.affectedTicks[unsigned(ComputeProducer::Hiring)] == 1);
    CHECK(snapshot.summary.affectedTicks[unsigned(ComputeProducer::PeriodicGradient)] == 1);
    CHECK(snapshot.summary.advancingTicks == 2);
    CHECK(snapshot.summary.tickBackground.samples == 1);
    CHECK(summary(snapshot, ComputeProducer::Hiring, JoinReason::TickDeadline).backgroundAttempts == 2);
    CHECK(executor.activeNs() == 0);
    CHECK(executor.metrics().workerJobs == 0);
}
TEST_CASE("complete joins and trace overflow remain valid and snapshots require drain")
{
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace, sizeof(ComputeTelemetry::Event)*2);
    TelemetryCounter counter;
    ComputeExecutor::Group group{1, {telemetryBump, &counter}};
    auto first = executor.submit(std::span(&group, 1), 1, {ComputeProducer::AI, 0, 1});
    auto second = executor.submit(std::span(&group, 1), 2, {ComputeProducer::ResourceGrowth, 0, 1});
    CHECK_THROWS_AS(executor.telemetrySnapshot(true), std::logic_error);
    executor.join(second, {JoinReason::TickDeadline, 1}); // Owner executes both in deadline order.
    executor.join(first, {JoinReason::TickDeadline, 1});
    auto snapshot = executor.telemetrySnapshot(true);
    CHECK(summary(snapshot, ComputeProducer::AI, JoinReason::TickDeadline).incomplete == 0);
    std::uint64_t lost = 0; for (const auto& stream : snapshot.streams) lost += stream.lost;
    CHECK(lost > 0);
    CHECK(snapshot.traceBytes <= sizeof(ComputeTelemetry::Event)*2);
    std::ostringstream out; ComputeTelemetry::writeJson(out, snapshot);
    CHECK(out.str().find("\"lost_events\"") != std::string::npos);
    executor.resetTelemetry();
    snapshot = executor.telemetrySnapshot(true);
    for (const auto& stream : snapshot.streams) { CHECK(stream.used == 0); CHECK(stream.lost == 0); }
}
TEST_CASE("trace preserves early external resolution and continuation callback ordering")
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
    executor.configure(2); executor.configureTelemetry(ComputeTelemetryMode::Trace);
    struct Work {
        ComputeExecutor& executor;
        static void continuation(void*, std::size_t) {}
        static void run(void* context, std::size_t) {
            auto& work = *static_cast<Work*>(context);
            auto ticket = work.executor.defer();
            REQUIRE(ticket->resume({continuation, nullptr})); // Resolution precedes callback return.
        }
    } work{executor};
    ComputeExecutor::Group group{1, {Work::run, &work}, 2};
    auto batch = executor.submit(std::span(&group, 1), 8, {ComputeProducer::PeriodicGradient, 1, 4});
    executor.join(batch, {JoinReason::TickDeadline, 4});
    const auto snapshot = executor.telemetrySnapshot(true);
    unsigned starts = 0, ends = 0, completes = 0, suspensions = 0, resolutions = 0, runnable = 0;
    for (const auto& stream : snapshot.streams) for (std::size_t i=0; i<stream.used; ++i) {
        const auto& event = stream.storage[i];
        CHECK(event.producer == ComputeProducer::PeriodicGradient);
        CHECK(event.serial != 0);
        if (event.kind == ComputeTelemetry::EventKind::CallbackStart) ++starts;
        if (event.kind == ComputeTelemetry::EventKind::CallbackEnd) ++ends;
        if (event.kind == ComputeTelemetry::EventKind::Complete) ++completes;
        if (event.kind == ComputeTelemetry::EventKind::Suspend) ++suspensions;
        if (event.kind == ComputeTelemetry::EventKind::ExternalResolution) ++resolutions;
        if (event.kind == ComputeTelemetry::EventKind::Runnable) ++runnable;
    }
    CHECK(starts == 2); CHECK(ends == 2); CHECK(completes == 1);
    CHECK(suspensions == 1); CHECK(resolutions == 1); CHECK(runnable == 1);
}
}

TEST_SUITE("ComputeExecutor")
{
TEST_CASE("trace storage allocation failure downgrades without losing required work")
{
    // On a 32-bit address space this budget can approach real addressable
    // memory rather than fail immediately; avoid exhausting the test host.
    if constexpr (sizeof(std::size_t) < 8) return;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace, std::numeric_limits<std::size_t>::max());
    CHECK(executor.telemetryMode() == ComputeTelemetryMode::Waits);
    CHECK(executor.telemetrySnapshot().available);
    CHECK(executor.telemetrySnapshot().requested == ComputeTelemetryMode::Trace);
    int value = 0;
    executor.run(1, [&](auto) { ++value; }, {ComputeProducer::Hiring, 0, 0}, {JoinReason::Verification, ComputeTelemetry::NoTick});
    CHECK(value == 1);
    CHECK(executor.telemetrySnapshot().summary.joins[unsigned(ComputeProducer::Hiring)][unsigned(JoinReason::Verification)].joined == 1);
}
TEST_CASE("blocking fork join failures and presentation remain traced without entering tick waits")
{
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    CHECK_THROWS_AS(executor.run(1, [](auto) { throw std::runtime_error("failure"); },
        {ComputeProducer::Other, 0, 0}, {JoinReason::Save, ComputeTelemetry::NoTick}), std::runtime_error);
    auto ticket = executor.submitPresentation(2, [](auto) {});
    while (executor.pumpPresentation()) {}
    CHECK(ticket->finished());
    const auto snapshot = executor.telemetrySnapshot(true);
    CHECK(snapshot.summary.backgroundTicks == 0);
    CHECK(snapshot.summary.joins[unsigned(ComputeProducer::Other)][unsigned(JoinReason::Save)].joined == 1);
    unsigned presentationEnds=0, runEnds=0;
    for (const auto& stream:snapshot.streams) for (std::size_t i=0; i<stream.used; ++i) {
        const auto& event=stream.storage[i];
        if(event.kind==ComputeTelemetry::EventKind::CallbackEnd) {
            if(event.executionKind==1) ++runEnds;
            if(event.executionKind==2) ++presentationEnds;
        }
    }
    CHECK(runEnds == 1); CHECK(presentationEnds == 2);
}
}

TEST_CASE("graphics diagnostics are optional and waits mode attributes the owner barrier" * doctest::test_suite("ComputeExecutor"))
{
    for (auto mode : {ComputeTelemetryMode::Off, ComputeTelemetryMode::Waits, ComputeTelemetryMode::Trace}) {
        ComputeExecutor executor;
        executor.configureTelemetry(mode);
        auto ticket = executor.submitPresentationBatch(3, {2}, [](auto) {});
        if (mode == ComputeTelemetryMode::Trace) CHECK_THROWS_AS(executor.telemetrySnapshot(true), std::logic_error);
        executor.finishPresentationBatch(ticket, {JoinReason::Verification, ComputeTelemetry::NoTick});
        executor.finishPresentationBatch(ticket, {JoinReason::Verification, ComputeTelemetry::NoTick});
        const auto snapshot = executor.telemetrySnapshot(mode == ComputeTelemetryMode::Trace);
        const auto& summary = snapshot.summary.joins[unsigned(ComputeProducer::Presentation)][unsigned(JoinReason::Verification)];
        CHECK(summary.joined == (mode == ComputeTelemetryMode::Off ? 0 : 1));
        CHECK(summary.ownerExecutions == (mode == ComputeTelemetryMode::Off ? 0 : 1));
        CHECK(executor.graphicsMetrics().jobs == (mode == ComputeTelemetryMode::Trace ? 3 : 0));
        CHECK(snapshot.summary.backgroundTicks == 0);
    }
}

TEST_CASE("yielded presentation chunks keep callback indices paired" * doctest::test_suite("ComputeExecutor"))
{
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    unsigned attempts = 0;
    auto ticket = executor.submitResumablePresentation(2, [&](std::size_t index) { return index == 0 || ++attempts > 1; });
    while (executor.pumpPresentation()) {}
    CHECK(ticket->finished());
    auto snapshot = executor.telemetrySnapshot(true);
    std::array<unsigned,2> starts{}, ends{};
    for (const auto& stream : snapshot.streams) for(std::size_t i=0;i<stream.used;++i) {
        const auto& event = stream.storage[i];
        if(event.kind==ComputeTelemetry::EventKind::CallbackStart) ++starts.at(event.job);
        if(event.kind==ComputeTelemetry::EventKind::CallbackEnd) ++ends.at(event.job);
    }
    CHECK(starts == ends); CHECK(starts[0] == 1); CHECK(starts[1] == 2);
}
TEST_CASE("worker-only maintenance is traced as other work without a required join" * doctest::test_suite("ComputeExecutor"))
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    struct Service : ComputeExecutor::WorkerOnly {
        std::atomic<bool> needs{true};
        std::promise<void> completed;
        std::shared_ptr<std::promise<void>> retired;
        explicit Service(std::shared_ptr<std::promise<void>> retired) : retired(std::move(retired)) {}
        ~Service() override { retired->set_value(); }
        bool pending() const noexcept override { return needs.load(); }
        void process() noexcept override { needs=false; completed.set_value(); }
    };
    ComputeExecutor executor;
    executor.configure(3); executor.configureTelemetry(ComputeTelemetryMode::Trace);
    auto retired = std::make_shared<std::promise<void>>();
    auto retiredFuture = retired->get_future();
    auto service = std::make_shared<Service>(retired);
    executor.setWorkerOnly(service); service->completed.get_future().wait();
    executor.setWorkerOnly({}); service.reset();
    // The worker's retained service is released after its locked trace events.
    retiredFuture.wait();
    const auto snapshot = executor.telemetrySnapshot(true);
    unsigned ends=0;
    for(const auto& stream:snapshot.streams) for(std::size_t i=0;i<stream.used;++i) {
        const auto& event=stream.storage[i];
        if(event.executionKind==4 && event.kind==ComputeTelemetry::EventKind::CallbackEnd) {
            CHECK(event.producer==ComputeProducer::Other); ++ends;
        }
    }
    CHECK(ends == 1); CHECK(snapshot.summary.backgroundTicks == 0);
}

TEST_CASE("session reset suppresses a returning maintenance interval from the previous epoch" * doctest::test_suite("ComputeExecutor"))
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    struct Service : ComputeExecutor::WorkerOnly {
        std::atomic<bool> needs{true};
        std::promise<void> entered, release;
        std::shared_future<void> released = release.get_future().share();
        std::shared_ptr<std::promise<void>> retired;
        explicit Service(std::shared_ptr<std::promise<void>> retired) : retired(std::move(retired)) {}
        ~Service() override { retired->set_value(); }
        bool pending() const noexcept override { return needs.load(); }
        void process() noexcept override { needs=false; entered.set_value(); released.wait(); }
    };
    ComputeExecutor executor;
    executor.configure(3); executor.configureTelemetry(ComputeTelemetryMode::Trace);
    auto retired = std::make_shared<std::promise<void>>(); auto retiredFuture = retired->get_future();
    auto service = std::make_shared<Service>(retired);
    executor.setWorkerOnly(service); service->entered.get_future().wait();
    executor.resetTelemetry();
    CHECK(executor.telemetrySnapshot(true).traceCoverageGap);
    service->release.set_value(); executor.setWorkerOnly({}); service.reset(); retiredFuture.wait();
    const auto snapshot=executor.telemetrySnapshot(true);
    for(const auto& stream:snapshot.streams) CHECK(stream.used==0);
    CHECK(snapshot.traceCoverageGap); CHECK_FALSE(snapshot.traceOpenWork);
}

TEST_CASE("synchronous graphics adapter keeps empty calls idle and guards owner callbacks" *
          doctest::test_suite("ComputeExecutor"))
{
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    executor.configure(1);
    executor.executePresentationJobs(0, {});
    CHECK(executor.graphicsMetrics().batches == 0);
    const auto owner = std::this_thread::get_id();
    executor.executePresentationJobs(1, [&](std::size_t index)
    {
        CHECK(index == 0);
        CHECK(std::this_thread::get_id() == owner);
        CHECK(ComputeExecutor::workerSlot() == 0);
        CHECK_THROWS_AS(executor.configure(1), std::logic_error);
        CHECK_THROWS_AS(executor.executePresentationJobs(0, {}), std::logic_error);
        CHECK_THROWS_AS(executor.submitPresentationBatch(0, {2}, {}), std::logic_error);
        auto empty = ComputeExecutor::PresentationBatchTicket{};
        CHECK_THROWS_AS(executor.finishPresentationBatch(empty), std::logic_error);
    });
    const auto metrics = executor.graphicsMetrics();
    CHECK(metrics.batches == 1);
    CHECK(metrics.jobs == 1);
    CHECK(metrics.parallelBatches == 0);
    CHECK(metrics.waitNs >= metrics.activeNs);
    auto admitted = executor.submitPresentationBatch(1, {2}, [](auto) {});
    CHECK_THROWS_WITH_AS(executor.executePresentationJobs(1, [](auto) {}),
                         "Graphics batch already admitted", std::logic_error);
    CHECK(executor.graphicsMetrics().jobs == 1);
    executor.finishPresentationBatch(admitted);
}
TEST_CASE("graphics adapter restores callback state on failure and never pumps other work" *
          doctest::test_suite("ComputeExecutor"))
{
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    unsigned scene = 0, required = 0, graphics = 0;
    auto preparation = executor.submitPresentation(1, [&](auto) { ++scene; });
    ComputeExecutor::Group group{
        1, {[](void *value, std::size_t) { ++*static_cast<unsigned *>(value); }, &required}};
    auto pending = executor.submit(std::span(&group, 1));
    CHECK_THROWS_WITH_AS(executor.executePresentationJobs(5, [&](std::size_t index)
    {
        ++graphics;
        CHECK(ComputeExecutor::workerSlot() == 0);
        if (index == 1) throw std::runtime_error("adapter failure");
    }), "adapter failure", std::runtime_error);
    CHECK(graphics == 2);
    CHECK(scene == 0);
    CHECK(required == 0);
    CHECK(executor.graphicsMetrics().jobs == 2);
    executor.executePresentationJobs(2, [&](auto) { ++graphics; });
    CHECK(graphics == 4);
    executor.join(pending);
    CHECK(required == 1);
    executor.cancelPresentationAndWait();
    executor.configure(2);
#ifdef __EMSCRIPTEN__
    const auto owner = std::this_thread::get_id();
    executor.executePresentationJobs(3, [&](auto)
    {
        CHECK(std::this_thread::get_id() == owner);
        CHECK_THROWS_AS(executor.configure(1), std::logic_error);
    });
#endif
}
TEST_CASE("synchronous graphics cancellation rejects incomplete output after draining" *
          doctest::test_suite("ComputeExecutor"))
{
#ifndef __EMSCRIPTEN__
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    executor.configure(2);
    std::promise<void> entered, release;
    auto enteredFuture = entered.get_future();
    auto released = release.get_future();
    auto scene = executor.submitPresentation(1, [&](auto)
    {
        entered.set_value();
        // A bounded wait also lets a failed assertion retire the executor.
        released.wait_for(std::chrono::seconds(5));
    });
    REQUIRE(enteredFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    std::atomic<unsigned> drawn{0};
    std::atomic<bool> admitted{false};
    std::thread reconfigure([&]
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (executor.graphicsMetrics().batches == 0 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        admitted.store(executor.graphicsMetrics().batches == 1);
        // The only worker is in scene preparation, so this cancels the pending
        // raster batch. Its owner must wake before scene preparation is released.
        executor.configure(1);
    });
    CHECK_THROWS_WITH_AS(executor.executePresentationJobs(3, [&](auto) { ++drawn; }),
                         "Synchronous graphics batch was canceled", std::runtime_error);
    CHECK(admitted.load());
    CHECK(drawn.load() == 0);
    CHECK_FALSE(scene->finished());
    release.set_value();
    reconfigure.join();
    // Cancellation must leave no occupied graphics admission or callback state.
    executor.executePresentationJobs(2, [&](auto) { ++drawn; });
    CHECK(drawn.load() == 2);
#endif
}
TEST_CASE("graphics adapter preserves occupied admission and serial finish shares the scene cap" *
          doctest::test_suite("ComputeExecutor"))
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    executor.configure(2);
    std::atomic<bool> sceneEntered{false}, releaseScene{false}, sceneInside{false}, overlap{false};
    auto scene = executor.submitPresentation(1, [&](auto)
    {
        sceneInside.store(true);
        sceneEntered.store(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!releaseScene.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        sceneInside.store(false);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!sceneEntered.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    REQUIRE(sceneEntered.load());
    auto admitted = executor.submitPresentationBatch(3, {1}, [&](auto)
    {
        if (sceneInside.load()) overlap.store(true);
    });
    CHECK_THROWS_WITH_AS(executor.executePresentationJobs(1, [](auto) {}),
                         "Graphics batch already admitted", std::logic_error);
    std::thread release([&]
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        releaseScene.store(true);
    });
    executor.finishPresentationBatch(admitted);
    release.join();
    CHECK_FALSE(overlap.load());
    CHECK(scene->finished());
    CHECK(executor.graphicsMetrics().jobs == 3);
}
TEST_CASE("canceling a slot-blocked graphics batch wakes its owner before scene completion" *
          doctest::test_suite("ComputeExecutor"))
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    executor.configure(2);
    std::atomic<bool> sceneEntered{false}, releaseScene{false};
    auto scene = executor.submitPresentation(1, [&](auto)
    {
        sceneEntered.store(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!releaseScene.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!sceneEntered.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    REQUIRE(sceneEntered.load());
    auto admitted = executor.submitPresentationBatch(3, {1}, [](auto) {});
    std::thread cancel([&]
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        admitted.cancel();
    });
    executor.finishPresentationBatch(admitted);
    cancel.join();
    CHECK(admitted.status() == ComputeExecutor::Presentation::Status::Canceled);
    CHECK_FALSE(scene->finished());
    CHECK(executor.graphicsMetrics().jobs == 0);
    releaseScene.store(true);
    executor.cancelPresentationAndWait();
}
