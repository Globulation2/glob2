// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/GradientWorkspace.h"
#include <ThreadSupport.h>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Execution only. Simulation owners choose submission and publication ticks.
class AsyncGradientExecutor
{
  public:
	struct Task
	{
		bool done = false;
		std::exception_ptr error;
		std::function<void(GradientWorkspace &)> work;
	};
	using Handle = std::shared_ptr<Task>;
	using Factory = std::function<std::thread(std::function<void()>)>;

  private:
	std::mutex mutex;
	std::condition_variable ready, completed;
	std::deque<Handle> queue;
	std::vector<std::thread> workers;
	std::size_t running = 0;
	bool stopping = false;
	GradientWorkspace serial;
	void execute(const Handle &task, GradientWorkspace &scratch) noexcept
	{
		try
		{
			task->work(scratch);
		}
		catch (...)
		{
			task->error = std::current_exception();
		}
		{
			std::lock_guard<std::mutex> lock(mutex);
			task->work = {};
			task->done = true;
			--running;
		}
		completed.notify_all();
	}
	void stop()
	{
		drain();
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopping = true;
		}
		ready.notify_all();
		for (auto &thread : workers)
			thread.join();
		workers.clear();
		stopping = false;
	}

  public:
	~AsyncGradientExecutor() { stop(); }
	void drain()
	{
		std::unique_lock<std::mutex> lock(mutex);
		completed.wait(lock, [&] { return running == 0; });
	}
	unsigned workerCount() const { return workers.size(); }
	void configure(
		unsigned count,
		Factory factory = [](auto f) { return GAGCore::ThreadSupport::launch(std::move(f)); })
	{
		stop();
		if constexpr (GAGCore::ThreadSupport::available)
		{
			try
			{
				for (unsigned i = 0; i < count; ++i)
					workers.push_back(factory(
						[this]
						{
							GradientWorkspace scratch;
							for (;;)
							{
								Handle task;
								{
									std::unique_lock<std::mutex> lock(mutex);
									ready.wait(lock, [&] { return stopping || !queue.empty(); });
									if (queue.empty())
										return;
									task = queue.front();
									queue.pop_front();
								}
								execute(task, scratch);
							}
						}));
			}
			catch (...)
			{
				stop();
			}
		}
	}
	Handle submit(std::function<void(GradientWorkspace &)> work)
	{
		auto task = std::make_shared<Task>();
		task->work = std::move(work);
		{
			std::lock_guard<std::mutex> lock(mutex);
			++running;
			if (!workers.empty())
				queue.push_back(task);
		}
		if (workers.empty())
			execute(task, serial);
		else
			ready.notify_one();
		return task;
	}
	void wait(const Handle &task)
	{
		if (!task)
			return;
		std::unique_lock<std::mutex> lock(mutex);
		completed.wait(lock, [&] { return task->done; });
		if (task->error)
			std::rethrow_exception(task->error);
	}
};
