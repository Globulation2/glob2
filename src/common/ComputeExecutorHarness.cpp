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
