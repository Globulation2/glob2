// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/GradientWorkspace.h"
#include "AsyncGradientExecutor.h"
#include "map/TerrainType.h"
#include "map/TerrainRegistry.h"
#include "sim/snapshot/WorldSnapshot.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <thread>
#include <ThreadSupport.h>
#include <stdexcept>
#include <vector>
#include <utility>

// Fixed-tick publication. The simulation thread owns pending/free
// and all slot pointers; workers own only job data/water until done is signalled.
// Exactly one submit per advance. Completion time never selects publication time.
class GradientPipeline
{
public:
	struct Job {
		std::optional<SimulationSnapshot::Handle> terrainLease;
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
 AsyncGradientExecutor::Handle task;
		std::exception_ptr error;
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
	using Factory = std::function<std::thread(std::function<void()>)>;
	// Simulation-owner callback observes publication, never computation.
	std::function<void(std::uint16_t**)> onPublished;
	struct Metrics { std::uint64_t jobs=0, published=0, discarded=0, waitNs=0, maxPending=0, preparationNs=0; } metrics;
private:
	std::deque<std::unique_ptr<Job>> pending;
	std::vector<std::unique_ptr<Job>> spare;
	std::shared_ptr<AsyncGradientExecutor> executor;
	unsigned delay = 0;
	std::uint64_t tick = 0, lastSubmission = 0;
	std::size_t cells = 0;
	Work work;
	std::atomic<std::uint64_t> activeNs{0};
	using Clock = std::chrono::steady_clock;
	static std::uint64_t ns(Clock::time_point start) {
		return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
	}
	void execute(Job &job, GradientWorkspace &scratch) noexcept {
		const auto start = Clock::now();
		try
		{
			work(job, scratch);
		}
		catch (...)
		{
			job.error = std::current_exception();
		}
		// Completion releases all borrowed immutable inputs, including failures.
		job.water.reset(); job.terrain.reset(); job.registry.reset(); job.profiles.reset();
		job.terrainLease.reset();
		activeNs.fetch_add(ns(start), std::memory_order_relaxed);
		job.done = true;
	}
	void wait(Job &job) {
		const auto start = Clock::now();
		executor->wait(job.task);
		metrics.waitNs += ns(start);
	}
public:
 explicit GradientPipeline(std::shared_ptr<AsyncGradientExecutor> pool=std::make_shared<AsyncGradientExecutor>()) : executor(std::move(pool)) {}
	~GradientPipeline() { reset(); }
	bool enabled() const { return delay != 0; }
	unsigned workerCount() const { return executor->workerCount(); }
	unsigned delayTicks() const { return delay; }
	std::uint64_t activeElapsedNs() const { return activeNs.load(std::memory_order_relaxed); }
	void finish() { for (auto &job : pending) { wait(*job); if(job->error) std::rethrow_exception(job->error); } }
 void reset() noexcept {
  for(auto &job:pending) try { executor->wait(job->task); } catch(...) {}
  pending.clear(); spare.clear(); delay=0; tick=0; lastSubmission=0;
 }
 void configure(unsigned count,unsigned ticks,std::size_t size,Work callback,
  Factory factory=[](std::function<void()> f){return GAGCore::ThreadSupport::launch(std::move(f));}) {
  reset(); metrics={}; activeNs=0; cells=size; work=std::move(callback);
  executor->configure(count,std::move(factory)); delay=ticks;
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
	void setWorkerCount(unsigned count) {
		finish();
		auto savedPending=std::move(pending);
		auto savedSpare=std::move(spare);
		const auto savedTick=tick, savedSubmission=lastSubmission, savedActive=activeElapsedNs();
		const auto savedMetrics=metrics;
		configure(count, delay, cells, work);
		pending=std::move(savedPending); spare=std::move(savedSpare);
		tick=savedTick; lastSubmission=savedSubmission; metrics=savedMetrics; activeNs=savedActive;
	}
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
		job->done=false; job->superseded=false; job->error=nullptr; job->task.reset();
		auto *ptr=job.get(); pending.push_back(std::move(job));
		lastSubmission = tick;
		++metrics.jobs;
		metrics.maxPending = std::max<std::uint64_t>(metrics.maxPending, pending.size());
		return ptr;
	}
	// The owner reserves before the read-only batch; only this job's private
	// inputs are written during preparation. Queue membership stays unchanged.
	template<class Seed> void prepare(Job *ptr, Seed &&seed) {
        const auto preparationStart=Clock::now();
        bool inputsPrepared=false;
		try {
			seed(*ptr);
            metrics.preparationNs += ns(preparationStart); inputsPrepared=true;
			ptr->task=executor->submit([this,ptr](GradientWorkspace &scratch){execute(*ptr,scratch);});
		}
		catch (...) {
			if (!inputsPrepared) metrics.preparationNs += ns(preparationStart);
			ptr->water.reset(); ptr->terrain.reset(); ptr->registry.reset(); ptr->profiles.reset(); ptr->terrainLease.reset();
			ptr->error=std::current_exception(); ptr->done=true;
			throw;
		}
	}
	template<class Seed> void submit(std::uint16_t **slot, int swim, Seed &&seed) {
		prepare(reserve(slot, swim), std::forward<Seed>(seed));
	}
};
