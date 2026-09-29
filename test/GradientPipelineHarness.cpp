// SPDX-License-Identifier: GPL-3.0-or-later
#include "map/gradient/GradientPipeline.h"
#include <array>
#include <cassert>
#include <iostream>
#include <stdexcept>

int main()
{
	for (unsigned workers : {0, 1, 2, 4, 8}) for (unsigned delay : {1, 2, 3, 8}) {
		std::array<std::uint16_t *, 7> slots{};
		for (auto &slot : slots) slot = new std::uint16_t[16]{};
		GradientPipeline pipeline;
		pipeline.configure(workers, delay, 16, [](auto &job, auto &) {
			if (job.data[0] % 3 == 0) std::this_thread::sleep_for(std::chrono::microseconds(40));
			for (int i=1; i<16; ++i) job.data[i] = job.data[0] + job.water[i];
		});
		std::array<unsigned, 7> expected{};
		std::array<bool, 1001> cancelled{};
		for (unsigned tick=1; tick<=1000; ++tick) {
			pipeline.advance();
			if (tick>delay && !cancelled[tick-delay]) expected[(tick-delay)%7] = tick-delay;
			for (unsigned s=0; s<slots.size(); ++s) assert(slots[s][0] == expected[s]);
			pipeline.submit(&slots[tick%7], 0, [&](auto &job) {
				job.data[0] = tick; job.water.assign(16, 2);
			});
			if (tick%5 == 0) {
				pipeline.invalidate(&slots[tick%7]);
				for(unsigned old = tick>delay ? tick-delay+1 : 1; old<=tick; ++old)
					if (old%7 == tick%7) cancelled[old] = true;
				slots[tick%7][0] = 60000; expected[tick%7] = 60000;
			}
		}
		pipeline.finish(); // Completing work must not publish early.
		for(unsigned s=0;s<slots.size();++s) assert(slots[s][0] == expected[s]);
		assert(pipeline.metrics.maxPending <= delay);
		pipeline.reset(); // Drain before slot destruction.
		for (auto *slot : slots) delete[] slot;
	}
	GradientPipeline fallback;
	unsigned created=0;
	fallback.configure(4, 3, 1, [](auto &job, auto &) { job.data[0]=9; },
		[&](auto fn) { if (++created == 2) throw std::runtime_error("injected creation failure"); return std::thread(fn); });
	assert(fallback.workerCount()==0 && fallback.delayTicks()==3);
	auto *slot = new std::uint16_t[1]{};
	fallback.advance(); fallback.submit(&slot, 0, [](auto &) {});
	fallback.advance(); fallback.advance(); assert(slot[0]==0);
	fallback.advance(); assert(slot[0]==9); fallback.reset(); delete[] slot;
	// Propagation failure is delivered at its fixed deadline; teardown remains safe.
	fallback.configure(2, 1, 1, [](auto &, auto &) { throw std::runtime_error("injected work failure"); });
	slot = new std::uint16_t[1]{};
	fallback.advance(); fallback.submit(&slot, 0, [](auto &) {});
	bool caught=false; try { fallback.advance(); } catch (const std::runtime_error &) { caught=true; }
	assert(caught); fallback.reset(); delete[] slot;
	std::cout << "PASS fixed publication, supersession, bounded buffers, scheduling stress, fallback, exceptions and teardown\n";
}
