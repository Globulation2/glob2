// SPDX-License-Identifier: GPL-3.0-or-later
#include "BackgroundWork.h"
#include <ThreadSupport.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
class Pool
{
  public:
	Pool()
	{
		if constexpr (!GAGCore::ThreadSupport::available)
			return;
		// Leave most cores to the simulation and the graphics thread.
		const unsigned count = std::clamp(std::thread::hardware_concurrency() / 4, 2u, 6u);
		try
		{
			for (unsigned i = 0; i < count; ++i)
				threads.push_back(GAGCore::ThreadSupport::launch([this] { work(); }));
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
		}
		wake.notify_all();
		for (auto &thread : threads)
			thread.join();
	}
	void submit(std::function<void()> task)
	{
		if (threads.empty())
		{
			task();
			return;
		}
		{
			std::lock_guard lock(mutex);
			tasks.push_back(std::move(task));
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
				wake.wait(lock, [&] { return stop || !tasks.empty(); });
				if (stop)
					return;
				task = std::move(tasks.front());
				tasks.pop_front();
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
	std::deque<std::function<void()>> tasks;
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
void submit(std::function<void()> task) { pool().submit(std::move(task)); }
unsigned workers() { return pool().workers(); }
}
