// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ThreadSupport.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace AIEngine
{
// The simulation owner submits and drains. Workers execute private controller
// work. One controller's jobs are FIFO and never overlap; different controllers
// may run concurrently. No worker calls submit/configure/drain recursively.
class Executor
{
	struct Lane
	{
		std::deque<std::function<void()>> jobs;
		bool active = false;
	};
	std::array<Lane, 32> lanes;
	std::array<unsigned, 32> ready{};
	unsigned readyHead = 0, readyCount = 0;
	std::vector<std::thread> workers;
	std::mutex mutex;
	std::condition_variable changed, idle;
	std::size_t outstanding = 0;
	bool stopping = false;
	std::atomic<std::uint64_t> computationNs{0};

	void work()
	{
		for (;;)
		{
			std::function<void()> job;
			unsigned actor;
			{
				std::unique_lock lock(mutex);
				changed.wait(lock, [&] { return stopping || readyCount; });
				if (!readyCount) return;
				actor = ready[readyHead]; readyHead = (readyHead + 1) % ready.size(); --readyCount;
				auto& lane = lanes.at(actor);
				job = std::move(lane.jobs.front()); lane.jobs.pop_front();
			}
			// Submitted tasks are packaged_tasks: errors are retained in futures.
			job();
			{
				std::lock_guard lock(mutex);
				auto& lane = lanes.at(actor);
				if (lane.jobs.empty()) lane.active = false;
				else { ready[(readyHead + readyCount) % ready.size()] = actor; ++readyCount; }
				--outstanding;
				if (!outstanding) idle.notify_all();
			}
			changed.notify_one();
		}
	}
	void stop()
	{
		drain();
		{ std::lock_guard lock(mutex); stopping = true; }
		changed.notify_all();
		for (auto& worker : workers) worker.join();
		workers.clear();
		stopping = false;
	}

public:
	Executor() = default;
	Executor(const Executor&) = delete;
	Executor& operator=(const Executor&) = delete;
	~Executor() { stop(); }
	// Zero means inline execution; one means one background worker. A platform
	// without threads or a thread-creation failure retains the inline path.
	void configure(unsigned count,
		const std::function<std::thread(std::function<void()>)>& launch =
			[](std::function<void()> job) { return GAGCore::ThreadSupport::launch(std::move(job)); })
	{
		if (count > 64) throw std::invalid_argument("AI worker count exceeds 64");
		stop();
		if constexpr (GAGCore::ThreadSupport::available)
			try
			{
				workers.reserve(count);
				for (unsigned i = 0; i < count; ++i) workers.push_back(launch([this] { work(); }));
			}
			catch (...) { stop(); }
	}
	unsigned workerCount() const { return workers.size(); }
	std::uint64_t activeNs() const { return computationNs.load(std::memory_order_relaxed); }
	template<class Result> std::future<Result> submit(unsigned actor, std::function<Result()> function)
	{
		if (actor >= lanes.size()) throw std::invalid_argument("AI controller index exceeds 31");
		auto timed = [this,function=std::move(function)]() mutable -> Result {
            struct Timer {
                std::atomic<std::uint64_t>& total;
                std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
                ~Timer() {total.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count(),std::memory_order_relaxed);}
            } timer{computationNs};
            return function();
        };
        auto task = std::make_shared<std::packaged_task<Result()>>(std::move(timed));
		auto future = task->get_future();
		if (workers.empty()) { (*task)(); return future; }
		{
			std::lock_guard lock(mutex);
			auto& lane = lanes[actor];
			lane.jobs.push_back([task] { (*task)(); });
			if (!lane.active)
			{
				ready[(readyHead + readyCount) % ready.size()] = actor; ++readyCount;
				lane.active = true;
			}
			++outstanding;
		}
		changed.notify_one();
		return future;
	}
	void drain()
	{
		std::unique_lock lock(mutex);
		idle.wait(lock, [&] { return outstanding == 0; });
	}
};
} // namespace AIEngine
