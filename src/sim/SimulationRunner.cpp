// SPDX-License-Identifier: GPL-3.0-or-later
#include "sim/SimulationRunner.h"

#include "Engine.h"
#include "SDLCompat.h"

#include <chrono>
#include <system_error>

SimulationRunner::SimulationRunner(Engine &engine) : engine(engine) {}

SimulationRunner::~SimulationRunner() { stop(); }

bool SimulationRunner::start()
{
#ifdef __EMSCRIPTEN__
	return false; // The browser build has no threads; the session runs serially.
#else
	try
	{
		thread = std::thread([this] { run(); });
		return true;
	}
	catch (const std::system_error &)
	{
		return false;
	}
#endif
}

void SimulationRunner::stop()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		stopping = true;
	}
	wake.notify_all();
	if (thread.joinable())
		thread.join();
}

void SimulationRunner::park(std::unique_lock<std::mutex> &lock)
{
	parked = true;
	parkedChanged.notify_all();
	wake.wait(lock, [&] { return (!parkRequested && !suspended) || stopping; });
	parked = false;
}

void SimulationRunner::run()
{
	try
	{
		std::unique_lock<std::mutex> lock(mutex);
		while (!stopping)
		{
			if (parkRequested || suspended)
			{
				park(lock);
				continue;
			}
			lock.unlock();
			const Uint64 now = engine.sessionClock();
			const Uint32 delay = engine.sessionDelay(now);
			bool running = true;
			if (delay == 0)
				running = engine.simulationStep(now);
			// Extract at most once per scene the main thread takes.
			if (!scenes.pending())
			{
				engine.extractScene(scenes.back());
				scenes.publish();
			}
			lock.lock();
			if (!running)
				break;
			if (delay > 0)
				wake.wait_for(lock, std::chrono::milliseconds(delay),
							  [&] { return stopping || parkRequested || suspended || !scenes.pending(); });
		}
	}
	catch (...)
	{
		std::lock_guard<std::mutex> lock(mutex);
		failure = std::current_exception();
	}
	std::lock_guard<std::mutex> lock(mutex);
	finished = true;
	parked = false;
	parkedChanged.notify_all();
}

void SimulationRunner::withGame(const std::function<void()> &work)
{
	if (!thread.joinable() || finished)
	{
		work();
		return;
	}
	{
		std::unique_lock<std::mutex> lock(mutex);
		parkRequested = true;
		wake.notify_all();
		parkedChanged.wait(lock, [&] { return parked || finished.load(); });
	}
	struct Release
	{
		SimulationRunner &runner;
		~Release()
		{
			{
				std::lock_guard<std::mutex> lock(runner.mutex);
				runner.parkRequested = false;
			}
			runner.wake.notify_all();
		}
	} release{*this};
	work();
}

void SimulationRunner::suspend()
{
	std::unique_lock<std::mutex> lock(mutex);
	suspended = true;
	wake.notify_all();
	if (thread.joinable())
		parkedChanged.wait(lock, [&] { return parked || finished.load(); });
}

void SimulationRunner::resume()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		suspended = false;
	}
	wake.notify_all();
}

const Scene *SimulationRunner::acquireScene()
{
	if (scenes.acquire())
	{
		haveScene = true;
		// The simulation thread may be waiting for the main thread to take this one.
		{
			std::lock_guard<std::mutex> lock(mutex);
		}
		wake.notify_all();
	}
	return haveScene ? &scenes.current() : nullptr;
}

void SimulationRunner::rethrowFailure()
{
	std::exception_ptr error;
	{
		std::lock_guard<std::mutex> lock(mutex);
		error = failure;
	}
	if (error)
		std::rethrow_exception(error);
}
