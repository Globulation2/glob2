// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/GradientWorkspace.h"
#include "map/TerrainType.h"
#include "map/TerrainRegistry.h"
#include "sim/snapshot/WorldSnapshot.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include "ComputeExecutor.h"
#include "SnapshotGradient.h"
#include <stdexcept>
#include <vector>
#include <utility>

// Fixed-tick publication. The simulation thread owns pending/free
// and all slot pointers; workers own private output and immutable inputs until joined.
// Exactly one submit per advance. Completion time never selects publication time.
class GradientPipeline
{
public:
	struct Job {
		std::optional<SimulationSnapshot::Handle> snapshotLease;
		std::uint16_t **slot = nullptr;
		std::unique_ptr<std::uint16_t[]> data;
		std::shared_ptr<const std::vector<std::uint8_t>> water; // Test callback compatibility.
		std::shared_ptr<const std::vector<TerrainType>> terrain;
		std::shared_ptr<const TerrainRegistry> registry;
		std::shared_ptr<const TerrainMovementSnapshot> profiles;
		unsigned terrainBuckets = 64;
		bool modifiedCosts = false;
		int swim = 0;
		std::uint64_t due = 0;
		bool superseded = false, done = false;
		std::exception_ptr error;
		gradient_preparation::Request request;
		gradient_preparation::CrowdingScratch* crowding = nullptr;
		ComputeExecutor::Batch batch;
		GradientPipeline* owner = nullptr;
		std::function<void(Job&)> seed;
		std::uint64_t preparationNs = 0;
	};
	// Stable save boundary. A view is valid only during visitPendingSnapshots;
	// the owning queue and worker state remain private to the pipeline.
	struct PendingSnapshot {
		std::uint16_t **slot;
		const std::uint16_t *data;
		unsigned remaining;
		bool superseded;
	};
	struct RestoredSnapshot {
		std::uint16_t **slot;
		int swim;
		unsigned remaining;
		bool superseded;
		std::unique_ptr<std::uint16_t[]> data;
	};
	using Work = std::function<void(Job &, GradientWorkspace &)>;
	// Simulation-owner callback observes publication, never computation.
	std::function<void(std::uint16_t**)> onPublished;
	// Executor due key of a job published `remaining` advances from now. The
	// Map maps it onto simulation ticks; standalone use orders by this
	// pipeline's own ticks.
	std::function<std::uint64_t(unsigned remaining)> deadline;
	static constexpr unsigned MaxDelay = 16;
	static_assert(MaxDelay <= ComputeExecutor::GradientHorizon);
	struct Metrics { std::uint64_t jobs=0, published=0, discarded=0, waitNs=0, maxPending=0, preparationNs=0; } metrics;
private:
	std::deque<std::unique_ptr<Job>> pending;
	std::vector<std::unique_ptr<Job>> spare;
	ComputeExecutor* executor = nullptr;
	bool shared = true;
	struct Workspace { GradientWorkspace propagation; gradient_preparation::CrowdingScratch crowding; };
	std::vector<Workspace> workspaces;
	unsigned delay = 0;
	std::uint64_t tick = 0, lastSubmission = 0;
	std::size_t cells = 0;
	Work work;
	std::atomic<std::uint64_t> activeNs{0};
	using Clock = std::chrono::steady_clock;
	static std::uint64_t ns(Clock::time_point start) {
		return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
	}
	void execute(Job &job, Workspace &scratch) noexcept {
		const auto start = Clock::now();
		job.crowding = &scratch.crowding;
		try
		{
			const auto preparationStart = Clock::now();
			try { if (job.seed) job.seed(job); }
			catch (...) { job.preparationNs = ns(preparationStart); throw; }
			job.preparationNs = ns(preparationStart);
			work(job, scratch.propagation);
		}
		catch (...)
		{
			job.error = std::current_exception();
		}
		// Completion releases all borrowed immutable inputs, including failures.
		job.water.reset(); job.terrain.reset(); job.registry.reset(); job.profiles.reset();
		job.snapshotLease.reset(); job.seed = {}; job.crowding = nullptr;
		activeNs.fetch_add(ns(start), std::memory_order_relaxed);
		job.done = true;
	}
	static void run(void* context, std::size_t) {
		auto& job = *static_cast<Job*>(context);
		auto& pipeline = *job.owner;
		pipeline.execute(job, pipeline.workspaces[pipeline.executor->slot()]);
	}
	void wait(Job &job) {
		const auto start = Clock::now();
		if (executor) executor->join(job.batch);
		metrics.preparationNs += std::exchange(job.preparationNs, 0);
		metrics.waitNs += ns(start);
		if (!job.done) throw std::logic_error("Unprepared gradient reservation");
	}
public:
	~GradientPipeline() { reset(); }
	bool enabled() const { return delay != 0; }
	unsigned workerCount() const { return shared && executor ? executor->threadCount()-1 : 0; }
	unsigned delayTicks() const { return delay; }
	std::uint64_t activeElapsedNs() const { return activeNs.load(std::memory_order_relaxed); }
	void finish() { for (auto &job : pending) { wait(*job); if(job->error) std::rethrow_exception(job->error); } }
	void reset() noexcept {
		// Unsubmitted reservations can be discarded; submitted callbacks must end
		// before their contexts, destination slots or scratch storage are destroyed.
		for (auto& job : pending) if (!job->batch.empty()) {
			try { executor->join(job->batch); } catch (...) {}
		}
		pending.clear(); spare.clear(); workspaces.clear();
		delay = 0; tick = 0; lastSubmission = 0;
	}
	void configure(ComputeExecutor& target, bool sharedExecution, unsigned ticks, std::size_t size, Work callback) {
		reset(); metrics = {}; activeNs = 0; cells = size; work = std::move(callback);
		executor = &target; shared = sharedExecution;
		resizeWorkspaces(); delay = ticks;
	}
	// Call after the executor is resized, with all previous work drained.
	void resizeWorkspaces() {
        workspaces.resize(executor ? executor->threadCount() : 1);
        // Bound optional seed caches across the entire pool, not per thread.
        for (auto& workspace : workspaces) {
            workspace.crowding.materials = {};
            workspace.crowding.materials.budget = 64 * 1024 * 1024 / workspaces.size();
        }
    }
	// Saving completes private work without changing publication deadlines.
	template<class Visitor> void visitPendingSnapshots(Visitor visitor) {
		finish();
		for (const auto &job : pending)
			visitor(PendingSnapshot{job->slot, job->data.get(),
				static_cast<unsigned>(job->due-tick), job->superseded});
	}
	std::size_t pendingCount() const { return pending.size(); }
	void restoreCompleted(RestoredSnapshot snapshot) {
		if (!enabled() || !snapshot.slot || !*snapshot.slot || !snapshot.data ||
			!snapshot.remaining || snapshot.remaining>delay || pending.size()>=delay ||
			(!pending.empty() && pending.back()->due>=tick+snapshot.remaining))
			throw std::runtime_error("Invalid saved gradient deadline or destination");
		auto job=std::make_unique<Job>();
		job->slot=snapshot.slot; job->swim=snapshot.swim; job->due=tick+snapshot.remaining;
		job->superseded=snapshot.superseded; job->done=true; job->data=std::move(snapshot.data);
		pending.push_back(std::move(job));
	}
	// Execution is local configuration, never part of saved simulation state.
	void setWorkerCount(unsigned count) { finish(); shared = count != 0; }
	// Publish before the teams step; preparation observes the completed previous tick.
	void advance() {
		++tick;
		while (!pending.empty() && pending.front()->due <= tick) {
			auto &job = *pending.front(); wait(job);
			if (job.error) std::rethrow_exception(job.error);
			if (!job.superseded) {
				auto *old = *job.slot; *job.slot = job.data.release(); job.data.reset(old);
				++metrics.published;
				if (onPublished) onPublished(job.slot);
			} else ++metrics.discarded;
			spare.push_back(std::move(pending.front())); pending.pop_front();
		}
	}
	// Main-thread synchronous refreshes supersede any older snapshot of this slot.
	void invalidate(std::uint16_t **slot) {
		for (auto &job : pending) if (job->slot == slot) job->superseded = true;
	}
	Job *reserve(std::uint16_t **slot, int swim) {
		if (!enabled() || tick == lastSubmission || pending.size() >= delay)
			throw std::logic_error("gradient pipeline requires one submission per advanced tick");
		std::unique_ptr<Job> job;
		if (spare.empty()) { job = std::make_unique<Job>(); job->data.reset(new std::uint16_t[cells]); }
		else { job = std::move(spare.back()); spare.pop_back(); }
		job->slot=slot; job->swim=swim; job->due=tick+delay;
		job->done=false; job->superseded=false; job->error=nullptr; job->owner=this; job->preparationNs=0;
		auto *ptr=job.get(); pending.push_back(std::move(job));
		lastSubmission = tick;
		++metrics.jobs;
		metrics.maxPending = std::max<std::uint64_t>(metrics.maxPending, pending.size());
		return ptr;
	}
	// The closure must own immutable inputs. No live world reads after dispatch.
	template<class Seed> void prepare(Job *ptr, Seed &&seed) {
		try {
			ptr->seed = std::forward<Seed>(seed);
			// Owner-only execution computes now; publication keeps its deadline.
			if (!shared) { execute(*ptr, workspaces[0]); return; }
			const ComputeExecutor::Group group{1, {&run, ptr}, ComputeExecutor::NoLane};
			const unsigned remaining = unsigned(ptr->due - tick);
			ptr->batch = executor->submit(std::span(&group, 1),
				deadline ? deadline(remaining) : ComputeExecutor::advanceDue(ptr->due));
		} catch (...) {
			ptr->seed = {}; ptr->snapshotLease.reset(); ptr->water.reset();
			ptr->terrain.reset(); ptr->registry.reset(); ptr->profiles.reset();
			ptr->error = std::current_exception(); ptr->done = true;
			throw;
		}
	}
	template<class Seed> void submit(std::uint16_t **slot, int swim, Seed &&seed) {
		prepare(reserve(slot, swim), std::forward<Seed>(seed));
	}
};
