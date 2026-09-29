// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Blocking, single-submitter batches. Jobs may nest batches, but nested work
// stays on its current worker. No simulation state may escape a batch barrier.
class ComputeExecutor
{
public:
	struct Metrics
	{
		std::size_t batches = 0, jobs = 0, parallelBatches = 0;
		std::uint64_t batchNs = 0, waitNs = 0;
	};
private:
	using Clock = std::chrono::steady_clock;
	inline static thread_local ComputeExecutor *active = nullptr;
	inline static thread_local std::size_t activeSlot = 0;
	std::vector<std::thread> workers;
	std::mutex mutex;
	std::condition_variable ready, finished;
	bool stopping = false;
	std::size_t generation = 0, completed = 0, count = 0;
	std::atomic<std::size_t> next{0};
	std::function<void(std::size_t)> job;
	std::exception_ptr error;
	Metrics totals;
	struct WorkerMetrics { std::uint64_t jobs = 0, activeNs = 0; };
	std::vector<WorkerMetrics> workerMetrics{1};

	void invoke(std::size_t slot)
	{
		auto *previous = active;
		const auto previousSlot = activeSlot;
		active = this;
		activeSlot = slot;
		const auto started = Clock::now();
		for (;;)
		{
			const auto i = next.fetch_add(1, std::memory_order_relaxed);
			if (i >= count) break;
			++workerMetrics[slot].jobs;
			try { job(i); }
			catch (...) { std::lock_guard<std::mutex> lock(mutex); if (!error) error = std::current_exception(); }
		}
		workerMetrics[slot].activeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
		active = previous;
		activeSlot = previousSlot;
	}
	void worker(std::size_t slot)
	{
		std::size_t seen = 0;
		std::unique_lock<std::mutex> lock(mutex);
		for (;;)
		{
			ready.wait(lock, [&] { return stopping || generation != seen; });
			if (stopping) return;
			seen = generation;
			lock.unlock();
			invoke(slot);
			lock.lock();
			++completed;
			finished.notify_one();
		}
	}
	void stop()
	{
		{ std::lock_guard<std::mutex> lock(mutex); stopping = true; }
		ready.notify_all();
		for (auto &worker : workers) worker.join();
		workers.clear();
		stopping = false;
		generation = 0;
	}
public:
	ComputeExecutor() = default;
	ComputeExecutor(const ComputeExecutor &) = delete;
	ComputeExecutor &operator=(const ComputeExecutor &) = delete;
	~ComputeExecutor() { stop(); }
	// Configure only between batches. Thread creation failure retains a usable
	// serial executor; caller reports the actual thread count.
	void configure(unsigned threads,
		const std::function<std::thread(std::function<void()>)> &launch =
			[](std::function<void()> function) { return std::thread(std::move(function)); })
	{
		assert(!active && threads >= 1 && threads <= 64);
		stop();
#ifndef __EMSCRIPTEN__
		try
		{
			workers.reserve(threads > 0 ? threads - 1 : 0);
			for (unsigned i = 1; i < threads; ++i)
				workers.push_back(launch([this, i] { worker(i); }));
		}
		catch (...) { stop(); }
#endif
		workerMetrics.assign(threadCount(), {});
		totals = {};
	}
	std::size_t threadCount() const { return workers.size() + 1; }
	std::size_t slot() const { return active == this ? activeSlot : 0; }
	const Metrics &metrics() const { return totals; }
	// Sum of active elapsed times, NOT CPU time (the benchmark measures process CPU).
	std::uint64_t activeNs() const
	{
		std::uint64_t value = 0;
		for (const auto &worker : workerMetrics) value += worker.activeNs;
		return value;
	}
	void run(std::size_t n, const std::function<void(std::size_t)> &function)
	{
		if (!n) return;
		if (active)
		{
			for (std::size_t i = 0; i < n; ++i) function(i);
			return;
		}
		const auto start = Clock::now();
		++totals.batches;
		totals.jobs += n;
		if (n == 1 || workers.empty())
		{
			// Establish the same nesting context as a parallel batch.
			active = this; activeSlot = 0;
			try { for (std::size_t i = 0; i < n; ++i) function(i); }
			catch (...) { active = nullptr; throw; }
			active = nullptr;
			workerMetrics[0].jobs += n;
			workerMetrics[0].activeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
		}
		else
		{
			++totals.parallelBatches;
			{
				std::lock_guard<std::mutex> lock(mutex);
				job = function; count = n; next = 0; completed = 0; error = nullptr;
				++generation;
			}
			ready.notify_all();
			invoke(0);
			const auto waitStart = Clock::now();
			std::unique_lock<std::mutex> lock(mutex);
			finished.wait(lock, [&] { return completed == workers.size(); });
			totals.waitNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - waitStart).count();
			job = {};
			if (error) std::rethrow_exception(error);
		}
		totals.batchNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
	}
};
