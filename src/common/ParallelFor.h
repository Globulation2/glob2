// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ThreadSupport.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <system_error>
#include <thread>
#include <vector>

// Fork/join over independent indices for offline tools (map generation searches,
// reports, studies): no simulation shares the CPUs, so a batch may use them all.
// Each index must write only its own outputs; callers combine those in index
// order afterwards, so the result never depends on the schedule or the width.
// The first exception (lowest index) is rethrown on the caller.
namespace ParallelFor
{
// Process-wide ceiling on participants (caller included). Zero means the
// hardware's logical CPU count; one keeps every batch on the caller, which an
// outer runner already filling the machine with processes should choose.
inline std::atomic<unsigned> &limitStorage()
{
	static std::atomic<unsigned> limit{0};
	return limit;
}
inline void setLimit(unsigned participants) { limitStorage().store(participants); }
inline unsigned limit()
{
	if (const unsigned set = limitStorage().load())
		return set;
	return std::max(1u, std::thread::hardware_concurrency());
}

template <class Function> void run(std::size_t count, Function &&function, unsigned participants = 0)
{
	if (!count)
		return;
	unsigned width = std::min<std::size_t>(participants ? std::min(participants, limit()) : limit(), count);
	if constexpr (!GAGCore::ThreadSupport::available)
		width = 1;
	if (width <= 1)
	{
		for (std::size_t i = 0; i < count; ++i)
			function(i);
		return;
	}
	std::atomic<std::size_t> next{0};
	std::vector<std::exception_ptr> errors(count);
	const auto drain = [&]
	{
		for (std::size_t i; (i = next.fetch_add(1)) < count;)
		{
			try
			{
				function(i);
			}
			catch (...)
			{
				errors[i] = std::current_exception();
			}
		}
	};
	std::vector<std::thread> workers;
	workers.reserve(width - 1);
	try
	{
		for (unsigned i = 1; i < width; ++i)
			workers.push_back(GAGCore::ThreadSupport::launch(drain));
	}
	catch (const std::system_error &)
	{
		// Fewer workers only narrows the batch; the caller drains the rest.
	}
	drain();
	for (auto &worker : workers)
		worker.join();
	for (auto &error : errors)
		if (error)
			std::rethrow_exception(error);
}
} // namespace ParallelFor
