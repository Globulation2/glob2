// SPDX-License-Identifier: GPL-3.0-or-later
#include "sim/SimulationRunner.h"

#include "Engine.h"
#include "sim/presentation/SceneInputs.h"
#include <SDL3/SDL_stdinc.h>

#include <chrono>
#include <system_error>

SimulationRunner::SimulationRunner(Engine &engine) : engine(engine) {}

SimulationRunner::~SimulationRunner() { stop(); }

bool SimulationRunner::start()
{
#ifdef __EMSCRIPTEN__
	// Keep simulation serial within the browser application host, including
	// its pthread variant; the native runner owns a separate simulation thread.
	return false;
#else
	telemetry.reset();
	requestedScene = engine.gui.sceneRequest(false);
	engine.gui.startScriptClientChannel();
	try
	{
		thread = std::thread([this] { run(); });
		return true;
	}
	catch (const std::system_error &)
	{
		engine.gui.game.scriptClient.stop();
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
	engine.gui.game.map.computeExecutor().cancelPresentationAndWait();
	engine.gui.game.scriptClient.stop();
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
	PerformanceTelemetry::bindCollector(&telemetry);
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
			// Diagnostic output owns a bounded publication slot. Let the client
			// drain it before advancing, as the serial loop does after each tick.
			// Otherwise a fast headless producer finishes while the first PNG
			// batch is rendering and silently misses later capture intervals.
			if (engine.headlessDiagnosticsPending())
			{
				wake.wait_for(lock, std::chrono::milliseconds(1));
				continue;
			}
			lock.unlock();
			const Uint64 now = engine.sessionClock();
			const Uint32 delay = engine.sessionDelay(now);
			bool running = true;
			if (delay == 0)
				running = engine.simulationStep(now);
            else engine.refreshRetainedPresentation();
            {
                std::lock_guard telemetryLock(telemetryMutex);
                telemetryMailbox.absorb(telemetry);
                completedTelemetryTick = engine.gui.game.stepCounter;
            }
			lock.lock();
			if (!running)
				break;
			if (delay > 0)
				wake.wait_for(lock, std::chrono::milliseconds(delay));
		}
	}
	catch (...)
	{
		std::lock_guard<std::mutex> lock(mutex);
		failure = std::current_exception();
	}
	PerformanceTelemetry::bindCollector(nullptr);
	std::lock_guard<std::mutex> lock(mutex);
	finished = true;
	parked = false;
	parkedChanged.notify_all();
}

std::optional<SceneRequest> SimulationRunner::admitPresentation()
{
    if (presentation && presentation->finished()) presentation->rethrowFailure();
    if (presentation && !presentation->finished()) return {};
    std::lock_guard lock(mutex);
    // A completed pending frame occupies the middle slot, not the writable
    // back slot. Allow a newer request to replace it, bounded to one submission
    // per client request so an uncapped simulation cannot flood preparation.
    if (requestedSceneGeneration == publishedSceneGeneration) return {};
    admittedSceneGeneration = requestedSceneGeneration;
    return requestedScene;
}
void SimulationRunner::publishPresentation(const SimulationSnapshot::Handle& world,SceneRequest request)
{
    publishedSceneGeneration = admittedSceneGeneration;
    auto input=std::make_shared<const SceneInputs>(SceneInputs{world,std::move(request)});
    const auto chunks=SceneExtractor::preparationChunks(*input);
    presentation=engine.gui.game.map.computeExecutor().submitResumablePresentation(chunks,[this,input,chunks](size_t chunk) {
        if (!presentationExtractor.prepareChunk(*input,scenes.back(),chunk)) return false;
        if (chunk+1==chunks) {scenes.publish();wake.notify_all();}
        return true;
    });
}

void SimulationRunner::withGame(const std::function<void()> &work)
{
    const auto mutate=[&] {
        // Exceptional owner access may mutate entities without advancing a tick.
        // Invalidate before invoking it, including when the callback throws.
        engine.gui.game.snapshots().invalidateBoundary();
        work();
    };
	if (!thread.joinable() || finished)
	{
		mutate();
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
	mutate();
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

const PresentationFrame *SimulationRunner::acquireScene()
{
	// A one-participant native executor uses the graphics thread for preparation.
	// Yield between chunks just as the serial/browser producer does.
	const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
	while (engine.gui.game.map.computeExecutor().pumpPresentation() &&
	       std::chrono::steady_clock::now() < until) {}
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

void SimulationRunner::requestScene(SceneRequest request)
{
    { std::lock_guard lock(mutex); requestedScene = std::move(request); ++requestedSceneGeneration; }
    wake.notify_all();
}

void SimulationRunner::absorbTelemetry(PerformanceTelemetry::Collector& target)
{
    std::lock_guard lock(telemetryMutex);
    target.absorb(telemetryMailbox);
}

Uint32 SimulationRunner::latestTelemetryTick()
{
    std::lock_guard lock(telemetryMutex);
    return completedTelemetryTick;
}
