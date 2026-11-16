// SPDX-License-Identifier: GPL-3.0-or-later
#include "BackgroundWork.h"
#include <ThreadSupport.h>
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
// GLOB2_BACKGROUND_RENDER_WORKERS (1-64) overrides the default for measurements.
unsigned workerCount()
{
	if (const char *value = std::getenv("GLOB2_BACKGROUND_RENDER_WORKERS"))
	{
		char *end = nullptr;
		const unsigned long count = std::strtoul(value, &end, 10);
		if (end != value && !*end && count >= 1 && count <= 64)
			return unsigned(count);
	}
	// At normal speed the simulation needs about a core. Beyond four workers the
	// graphics thread's launch and install budgets, not the workers, set the pace.
	return std::clamp(std::thread::hardware_concurrency() / 2, 2u, 4u);
}

class Pool
{
  public:
	Pool()
	{
		if constexpr (!GAGCore::ThreadSupport::available)
			return;
		const unsigned count = workerCount();
		try
		{
			for (unsigned i = 0; i < count; ++i)
				threads.push_back(GAGCore::ThreadSupport::launch([this] { work(); },
				                                                  GAGCore::ThreadSupport::ThreadRole::Background));
		}
		catch (const std::system_error &)
		{
			// Fewer workers only slow composition; with none, tasks run inline.
		}
	}
	~Pool()
	{
		{
			std::lock_guard lock(mutex);
			stop = true;
			tasks.clear();
			ahead.clear();
		}
		wake.notify_all();
		for (auto &thread : threads)
			thread.join();
	}
	void submit(std::function<void()> task, BackgroundWork::Priority priority)
	{
		if (threads.empty())
		{
			task();
			return;
		}
		{
			std::lock_guard lock(mutex);
			if (priority == BackgroundWork::Priority::First)
				tasks.push_front(std::move(task));
			else
				(priority == BackgroundWork::Priority::Visible ? tasks : ahead).push_back(std::move(task));
		}
		wake.notify_one();
	}
	unsigned workers() const { return unsigned(threads.size()); }

  private:
	void work()
	{
		for (;;)
		{
			std::function<void()> task;
			{
				std::unique_lock lock(mutex);
				wake.wait(lock, [&] { return stop || !tasks.empty() || !ahead.empty(); });
				if (stop)
					return;
				auto &queue = tasks.empty() ? ahead : tasks;
				task = std::move(queue.front());
				queue.pop_front();
			}
			try
			{
				task();
			}
			catch (...)
			{
				// Tasks report their own failures; a worker never stops for one.
			}
		}
	}
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<std::function<void()>> tasks, ahead;
	std::vector<std::thread> threads;
	bool stop = false;
};

Pool &pool()
{
	static Pool instance;
	return instance;
}
}

namespace BackgroundWork
{
void submit(std::function<void()> task, Priority priority) { pool().submit(std::move(task), priority); }
unsigned workers() { return pool().workers(); }
}
