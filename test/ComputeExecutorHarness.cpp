// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComputeExecutor.h"
#include <array>
#include <cassert>
#include <iostream>
#include <stdexcept>

int main()
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
				assert(slot < executor.threadCount());
				assert(occupied[slot].fetch_add(1) == 0);
				executor.run(3, [&](size_t j) { output[i] += int(i + j); });
				assert(occupied[slot].fetch_sub(1) == 1);
			});
			for (size_t i = 0; i < output.size(); ++i) assert(output[i] == int(3 * i + 3));
		}
		bool caught = false;
		try { executor.run(20, [](size_t i) { if (i == 7) throw std::runtime_error("job failure"); }); }
		catch (const std::runtime_error &) { caught = true; }
		assert(caught);
		int one = 0;
		executor.run(1, [&](size_t) { ++one; });
		assert(one == 1);
	}
	int launched = 0;
	executor.configure(4, [&](std::function<void()> function) {
		if (++launched == 2) throw std::runtime_error("injected thread creation failure");
		return std::thread(std::move(function));
	});
	assert(executor.threadCount() == 1);
	int completed = 0;
	executor.run(7, [&](size_t) { ++completed; });
	assert(completed == 7);
	std::cout << "PASS executor: exclusive slots, nested batches, reuse, errors, barriers, reconfiguration\n";
}
