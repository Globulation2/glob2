// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scene/Scene.h"
#include "scene/SceneBuffer.h"

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
//!   whenever the main thread has taken the previous Scene, extracts the next one
//!   into a SceneBuffer. It is the only thread that touches the game while running.
//! - withGame() runs main-thread work that reads or writes the game (input, GUI
//!   logic) while the simulation is parked between ticks; a sleeping simulation
//!   counts as parked, so at normal speed this does not wait.
//! - Drawing reads only acquireScene() and GUI state.
//!
//! The simulation's results do not depend on this: ticks, orders and the
//! synchronized RNG are the same as in serial execution.
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
	//! The newest published Scene, or null before the first one.
	const Scene *acquireScene();
	//! True once the simulation ended the session or failed.
	bool ended() const { return finished.load(); }
	//! Rethrow a failure raised on the simulation thread, if any.
	void rethrowFailure();

private:
	void run();
	void park(std::unique_lock<std::mutex> &lock);

	Engine &engine;
	SceneBuffer<Scene> scenes;
	std::thread thread;
	std::mutex mutex;
	std::condition_variable wake, parkedChanged;
	bool stopping = false, parkRequested = false, suspended = false, parked = false;
	std::atomic<bool> finished{false};
	std::exception_ptr failure;
	bool haveScene = false;
};
