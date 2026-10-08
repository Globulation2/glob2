// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <vector>
#include <functional>
#include <utility>
#include <thread>
#include <atomic>
#include "ComputeExecutor.h"
#include <array>
#include <stdexcept>
#include <chrono>
#include <span>

TEST_SUITE("ComputeExecutor")
{
TEST_CASE("supported platforms execute jobs concurrently on distinct threads")
{
    if constexpr (GAGCore::ThreadSupport::available)
    {
        ComputeExecutor executor;
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
TEST_CASE("deferred batches drain FIFO with owner participation, lanes, isolated errors and placements")
{
	for (unsigned threads : {1u, 2u, 4u})
	{
		CAPTURE(threads);
		ComputeExecutor executor;
		executor.configure(threads);
		// Joining the newest batch first still completes the older ones.
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
		// Blocking batches run while deferred work is live; owner-only batches
		// run at their join, never on workers.
		Counter d; ComputeExecutor::Group gd{3, {bump, &d}};
		auto D = executor.submit(std::span(&gd, 1), ComputeExecutor::Placement::OwnerOnly);
		std::atomic<int> ran{0};
		executor.run(8, [&](std::size_t) { ran.fetch_add(1); });
		CHECK(ran.load() == 8);
		CHECK(d.value.load() == 0);
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
TEST_CASE("completed lanes can change placement while an unrelated older batch remains")
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor; executor.configure(2);
    Gate held, completed; completed.open = true;
    ComputeExecutor::Group older{1, {gatedJob, &held}};
    auto first = executor.submit(std::span(&older, 1), ComputeExecutor::Placement::OwnerOnly);
    ComputeExecutor::Group lane{1, {gatedJob, &completed}, 2};
    auto second = executor.submit(std::span(&lane, 1));
    while (!executor.finished(second)) std::this_thread::yield();
    REQUIRE(executor.liveBatches() == 2);
    auto third = executor.submit(std::span(&lane, 1), ComputeExecutor::Placement::OwnerOnly);
    held.open = true;
    executor.join(third); executor.join(second); executor.join(first);
    CHECK(completed.passed == 2);
}
TEST_CASE("lane groups run in index order, placements cannot mix on a lane, and the horizon is bounded")
{
	for (unsigned threads : {1u, 3u})
	{
		CAPTURE(threads);
		ComputeExecutor executor;
		executor.configure(threads);
		// Several jobs of one lane group are sequenced by their offsets.
		Ordered ordered;
		ComputeExecutor::Group group{6, {orderedJob, &ordered}, 1};
		auto batch = executor.submit(std::span(&group, 1));
		executor.join(batch);
		CHECK(ordered.overlaps.load() == 0);
		CHECK(ordered.order == std::vector<int>{0, 1, 2, 3, 4, 5});
		// A live Shared lane rejects an OwnerOnly batch on the same lane, and
		// finished() reports the batch state while it is gated.
		Gate gate;
		ComputeExecutor::Group gated{1, {gatedJob, &gate}, 2};
		auto live = executor.submit(std::span(&gated, 1));
		CHECK_FALSE(executor.finished(live));
		ComputeExecutor::Group clash{1, {gatedJob, &gate}, 2};
		CHECK_THROWS_AS(executor.submit(std::span(&clash, 1), ComputeExecutor::Placement::OwnerOnly), std::logic_error);
		CHECK(executor.liveBatches() == 1);
		gate.open = true;
		executor.join(live);
		CHECK(executor.finished(live)); CHECK(gate.passed.load() == 1);
		// The ring holds at most Slots live batches; the overflow leaves state intact.
		Gate hold;
		ComputeExecutor::Group held{1, {gatedJob, &hold}};
		std::vector<ComputeExecutor::Batch> batches;
		for (std::size_t i = 0; i < ComputeExecutor::Slots; ++i) batches.push_back(executor.submit(std::span(&held, 1), ComputeExecutor::Placement::OwnerOnly));
		CHECK_THROWS_AS(executor.submit(std::span(&held, 1), ComputeExecutor::Placement::OwnerOnly), std::logic_error);
		CHECK(executor.liveBatches() == ComputeExecutor::Slots);
		hold.open = true;
		for (auto& b : batches) executor.join(b);
		CHECK(hold.passed.load() == int(ComputeExecutor::Slots));
		CHECK(executor.liveBatches() == 0);
	}
}
}

#include "ReadOnlyPhase.h"
TEST_SUITE("ReadOnlyPhase") {
TEST_CASE("borrowed groups cover serial parallel empty and exception barriers") {
    for (unsigned threads : {1, 4}) {
        ComputeExecutor executor;
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
TEST_CASE("presentation cannot delay simulation barriers or run on the simulation owner")
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    for (unsigned threads : {2u, 3u, 5u})
    {
        ComputeExecutor executor;
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
TEST_CASE("presentation progresses before a continuous simulation backlog drains")
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    ComputeExecutor executor;
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
