// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "render/scene/Scene.h"
#include "render/scene/SceneBuffer.h"
#include "render/scene/SceneExtract.h"

#include <PerformanceTelemetry.h>
#include "ComputeExecutor.h"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

class Engine;

//! Runs a game session's simulation on its own thread while the main thread
//! handles input and draws.
//!
//! - The simulation thread paces and runs ticks (Engine::simulationStep) and,
//!   whenever the main thread has taken the previous PresentationFrame, captures immutable
//!   inputs for a compute-worker presentation task. That task publishes a complete
//!   PresentationFrame without participating in simulation barriers.
//! - withGame() protects exceptional live-state access (save capture, dialogs,
//!   settings and diagnostics). Routine input and rendering use immutable Scenes.
//! - Drawing reads only acquireScene() and GUI state.
//!
//! The simulation's results do not depend on this: ticks, orders and the
//! owner RNG streams are the same as in serial execution.
class SimulationRunner
{
public:
	explicit SimulationRunner(Engine &engine);
	~SimulationRunner();
	SimulationRunner(const SimulationRunner &) = delete;
	SimulationRunner &operator=(const SimulationRunner &) = delete;

	//! Start the simulation thread; false when threads are unavailable (run serially).
	bool start();
	//! Stop the simulation thread at a tick boundary and join it. Idempotent.
	void stop();
	//! Run work on the calling thread while the simulation is parked between ticks.
	//! Runs it directly once the simulation thread has ended.
	void withGame(const std::function<void()> &work);
	//! Keep the simulation parked (e.g. while the application is in the background)
	//! until resume().
	void suspend();
	void resume();
	void requestScene(SceneRequest request);
    std::optional<SceneRequest> admitPresentation();
    void publishPresentation(const SimulationSnapshot::Handle& world,SceneRequest request);
	//! The newest published PresentationFrame, or null before the first one.
	const PresentationFrame *acquireScene();
	bool sceneReady() const { return haveScene; }
	//! True once the simulation ended the session or failed.
	bool ended() const { return finished.load(); }
	//! Rethrow a failure raised on the simulation thread, if any.
	void rethrowFailure();
	//! What the simulation thread measures (PerformanceTelemetry scopes). The main
	//! thread absorbs the mailbox while running, or this collector after stop().
	PerformanceTelemetry::Collector telemetry;
    void absorbTelemetry(PerformanceTelemetry::Collector& target);
    Uint32 latestTelemetryTick();


private:
	void run();
    std::mutex telemetryMutex;
    PerformanceTelemetry::Collector telemetryMailbox;
    Uint32 completedTelemetryTick = 0;
	void park(std::unique_lock<std::mutex> &lock);

	Engine &engine;
	SceneBuffer<PresentationFrame> scenes;
	SceneRequest requestedScene;
    Uint64 requestedSceneGeneration = 1;
    Uint64 admittedSceneGeneration = 0, publishedSceneGeneration = 0;
	SceneExtractor presentationExtractor;
	ComputeExecutor::PresentationTicket presentation;
	std::thread thread;
	std::mutex mutex;
	std::condition_variable wake, parkedChanged;
	bool stopping = false, parkRequested = false, suspended = false, parked = false;
	std::atomic<bool> finished{false};
	std::exception_ptr failure;
	bool haveScene = false;
};
