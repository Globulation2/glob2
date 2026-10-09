// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/snapshot/WorldSnapshot.h"
#include "ComputeExecutor.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

// Fixed-tick publication for scheduled building fields; the sibling of
// GradientPipeline. Differences: up to MaxJobsPerTick reservations per advanced
// tick, every job staged in one tick is submitted as one executor batch, and
// publication goes through an owner callback that may still reject a result
// (the Map validates it against live buildings).
//
// The simulation thread owns the pending queue, the spare list and every
// destination. A worker owns its job's payload outputs and immutable inputs from
// submission until the owner joins. Jobs publish in reservation order at
// reservation tick + delay. Completion time never selects publication time:
// advance() joins a job at its deadline, however early or late it finished.
//
// Payload is the producer-specific job description (building identity, owner
// scalars, inputs and outputs). If it provides `void releaseInputs() noexcept`,
// that is called once the job completes or fails, so borrowed immutable inputs
// end with the computation, as the snapshot lease does.
template<class Payload> class BuildingGradientPipeline
{
public:
	static constexpr unsigned MaxJobsPerTick = 4;
	static constexpr unsigned MaxDelay = 8;
	static_assert(MaxDelay <= ComputeExecutor::BuildingHorizon);
	struct Job {
		std::optional<SimulationSnapshot::Handle> snapshotLease;
		Payload payload{};
		std::uint64_t due = 0;
		bool superseded = false, done = false;
		std::exception_ptr error;
		ComputeExecutor::Batch batch; // Shared by every job of its tick.
		BuildingGradientPipeline* owner = nullptr;
	};
	using Work = std::function<void(Job&)>;
	// Simulation-owner callbacks. publish() sees each due, unsuperseded, error-free
	// result and returns whether it was installed; retire() then sees every job
	// leaving the queue at its deadline (published or discarded) so it can recycle
	// pooled outputs. reset() drops jobs without callbacks; drain() retires them.
	std::function<bool(Job&)> publish;
	// Executor due key of jobs published `remaining` advances from now; see
	// GradientPipeline::deadline.
	std::function<std::uint64_t(unsigned remaining)> deadline;
	std::function<void(Job&)> retire;
	struct Metrics {
		std::uint64_t jobs=0, batches=0, published=0, discarded=0, waitNs=0, maxPending=0, maxBatch=0;
	} metrics;
private:
	std::deque<std::unique_ptr<Job>> pending;
	std::vector<std::unique_ptr<Job>> spare;
	ComputeExecutor* executor = nullptr;
	bool shared = true;
	std::vector<ComputeExecutor::Group> groups;
	unsigned delay = 0, admitted = 0;
	std::uint64_t tick = 0;
	Work work;
	using Clock = std::chrono::steady_clock;
	static std::uint64_t ns(Clock::time_point start) {
		return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
	}
	static void releaseInputs(Job& job) noexcept {
		job.snapshotLease.reset();
		if constexpr (requires(Payload& p) { p.releaseInputs(); }) job.payload.releaseInputs();
	}
	void execute(Job& job) noexcept {
		try { work(job); }
		catch (...) { job.error = std::current_exception(); }
		releaseInputs(job);
		job.done = true;
	}
	static void run(void* context, std::size_t) {
		auto& job = *static_cast<Job*>(context);
		job.owner->execute(job);
	}
	void wait(Job& job) {
		const auto start = Clock::now();
		if (executor) executor->join(job.batch); // Later jobs of the batch hold stale handles.
		metrics.waitNs += ns(start);
		if (!job.done) throw std::logic_error("Unprepared building gradient reservation");
	}
	unsigned dueCount(std::uint64_t due) const {
		unsigned count = 0;
		for (auto it = pending.rbegin(); it != pending.rend() && (*it)->due == due; ++it) ++count;
		return count;
	}
public:
	~BuildingGradientPipeline() { reset(); }
	bool enabled() const { return delay != 0; }
	unsigned delayTicks() const { return delay; }
	std::size_t pendingCount() const { return pending.size(); }
	// True while this advanced tick can admit another job.
	bool canReserve() const {
		return enabled() && tick != 0 && admitted < MaxJobsPerTick && pending.size() < delay*MaxJobsPerTick &&
			dueCount(tick+delay) < MaxJobsPerTick;
	}
	// Complete all private work without publishing; rethrows the oldest failure.
	void finish() { for (auto& job : pending) { wait(*job); if (job->error) std::rethrow_exception(job->error); } }
	void reset() noexcept {
		// Unsubmitted reservations can be discarded; submitted callbacks must end
		// before their contexts, destinations or scratch storage are destroyed.
		for (auto& job : pending) if (!job->batch.empty()) {
			try { executor->join(job->batch); } catch (...) {}
		}
		pending.clear(); spare.clear(); groups.clear();
		delay = 0; admitted = 0; tick = 0;
	}
	// Teardown with pooled outputs: complete submitted work, retire every pending
	// job unpublished, then reset. Worker failures are dropped with their results.
	void drain() noexcept {
		for (auto& job : pending) {
			if (!job->batch.empty()) { try { executor->join(job->batch); } catch (...) {} }
			if (retire) { try { retire(*job); } catch (...) {} }
		}
		reset();
	}
	void configure(ComputeExecutor& target, bool sharedExecution, unsigned ticks, Work callback) {
		if (ticks > MaxDelay) throw std::invalid_argument("building gradient delay exceeds its horizon");
		reset(); metrics = {}; work = std::move(callback);
		executor = &target; shared = sharedExecution; delay = ticks;
	}
	// Execution is local configuration, never part of saved simulation state.
	void setWorkerCount(unsigned count) { finish(); shared = count != 0; }
	// Publish before the teams step; preparation observes the completed previous tick.
	void advance() {
		++tick; admitted = 0;
		while (!pending.empty() && pending.front()->due <= tick) {
			auto& job = *pending.front(); wait(job);
			if (job.error) std::rethrow_exception(job.error);
			if (!job.superseded && publish && publish(job)) ++metrics.published;
			else ++metrics.discarded;
			if (retire) retire(job);
			spare.push_back(std::move(pending.front())); pending.pop_front();
		}
	}
	// Synchronous refreshes and lifecycle changes supersede older pending results.
	template<class Match> unsigned invalidate(Match&& match) {
		unsigned count = 0;
		for (auto& job : pending)
			if (!job->superseded && match(std::as_const(job->payload))) { job->superseded = true; ++count; }
		return count;
	}
	// Admit one job due at tick + delay. The caller fills snapshotLease and payload,
	// then hands every job staged this tick to one prepare() call.
	Job* reserve() {
		if (!canReserve()) throw std::logic_error("building gradient pipeline admission exceeded");
		std::unique_ptr<Job> job;
		if (spare.empty()) job = std::make_unique<Job>();
		else { job = std::move(spare.back()); spare.pop_back(); }
		job->due = tick+delay; job->done = false; job->superseded = false; job->error = nullptr;
		job->owner = this; job->batch = {}; job->snapshotLease.reset();
		auto* ptr = job.get(); pending.push_back(std::move(job));
		++admitted; ++metrics.jobs;
		metrics.maxPending = std::max<std::uint64_t>(metrics.maxPending, pending.size());
		return ptr;
	}
	// Submit staged jobs as one executor batch. Inputs must be immutable and owned
	// by the job: no live world reads after dispatch.
	void prepare(std::span<Job* const> jobs) {
		if (jobs.empty()) return;
		try {
			// Owner-only execution computes now; publication keeps its deadline.
			if (!shared) { for (auto* job : jobs) execute(*job); return; }
			groups.clear();
			for (auto* job : jobs) groups.push_back({1, {&run, job}, ComputeExecutor::NoLane});
			const auto due = jobs.front()->due; // every job staged this tick shares it
			const auto batch = executor->submit(groups,
				deadline ? deadline(unsigned(due - tick)) : ComputeExecutor::advanceDue(due));
			for (auto* job : jobs) job->batch = batch;
			++metrics.batches;
			metrics.maxBatch = std::max<std::uint64_t>(metrics.maxBatch, jobs.size());
		} catch (...) {
			const auto error = std::current_exception();
			for (auto* job : jobs) { releaseInputs(*job); job->error = error; job->done = true; }
			throw;
		}
	}
	// Saving completes private work without changing publication deadlines. The
	// visitor receives each job in publication order with its remaining ticks and
	// may finish the job's private state (for example a resumable search).
	template<class Visitor> void visitPending(Visitor&& visitor) {
		finish();
		for (auto& job : pending) visitor(*job, static_cast<unsigned>(job->due-tick));
	}
	// Owner-only fields of pending jobs (for example their generation), without
	// joining: the visitor must not touch anything a worker reads or writes.
	template<class Visitor> void visitOwnerFields(Visitor&& visitor) {
		for (auto& job : pending) visitor(job->payload);
	}
	// Loading re-creates a completed job with its saved remaining ticks, validated
	// against the same admission bounds a live reservation obeys.
	Job& restoreCompleted(unsigned remaining, bool superseded, Payload payload) {
		const auto due = tick+remaining;
		if (!enabled() || !remaining || remaining > delay || pending.size() >= delay*MaxJobsPerTick ||
			(!pending.empty() && pending.back()->due > due) || dueCount(due) >= MaxJobsPerTick)
			throw std::runtime_error("Invalid saved building gradient deadline");
		auto job = std::make_unique<Job>();
		job->payload = std::move(payload); job->due = due; job->superseded = superseded;
		job->done = true; job->owner = this;
		auto& result = *job; pending.push_back(std::move(job));
		return result;
	}
};
