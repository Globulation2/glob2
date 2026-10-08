// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <ThreadSupport.h>
#include <vector>

// One executor for the simulation's parallel work. Two shapes of work share
// its threads:
//
//  * run(): a blocking fork/join batch submitted and joined by one thread. Jobs
//    may nest batches; nested work stays on its current thread. No simulation
//    state may escape the barrier.
//  * submit()/join(): deferred batches (AI decisions, gradients). The owner
//    thread submits each batch tagged with the tick it is due. Workers run
//    deferred jobs earliest due first, in submission order within a lane. At a
//    join the owner only waits: it never runs deferred work while any worker
//    exists, even when the only worker also runs presentation, which it
//    interleaves with these jobs. With no workers the owner runs the jobs due
//    no later than the batch at its join, because nothing else can.
//
// Jobs compute only from inputs fixed at submission, so the schedule never
// changes a result. Joins always end: a worker claims a lane job only after its
// predecessor completed, and neither jobs nor presentation chunks wait on the owner.
class ComputeExecutor
{
public:
	// Presentation is best-effort work, never part of a simulation barrier.
	// One chunk runs at a time; at most one replacement waits behind it.
	class Presentation
	{
		friend class ComputeExecutor;
	public:
		enum class Status { Pending, Running, Complete, Canceled, Failed };
		Status status() const { return state.load(std::memory_order_acquire); }
		bool finished() const
		{
			const auto value = status();
			return value == Status::Complete || value == Status::Canceled || value == Status::Failed;
		}
		void cancel() { canceled.store(true, std::memory_order_release); }
		void rethrowFailure() const { if (status() == Status::Failed) std::rethrow_exception(error); }
	private:
		std::atomic<Status> state{Status::Pending};
		std::atomic<bool> canceled{false};
		std::function<bool(std::size_t)> job;
		std::size_t next = 0, count = 0;
		std::exception_ptr error;
	};
	using PresentationTicket = std::shared_ptr<Presentation>;
	struct PresentationMetrics
	{
		std::uint64_t submitted = 0, replaced = 0, chunks = 0, activeNs = 0;
	};
	struct Metrics
	{
		std::size_t batches = 0, jobs = 0, parallelBatches = 0;
		std::uint64_t batchNs = 0, waitNs = 0;
		std::size_t deferredBatches = 0, deferredJobs = 0, ownerJobs = 0, workerJobs = 0;
		std::uint64_t joinWaitNs = 0;
	};
	struct Job
	{
		void (*invoke)(void*, std::size_t) = nullptr;
		void* context = nullptr;
	};
	static constexpr unsigned NoLane = ~0u;
	static constexpr unsigned Lanes = 32;
	struct Group
	{
		std::size_t count = 0;
		Job job;
		unsigned lane = NoLane;
	};
	class Batch
	{
		friend class ComputeExecutor;
		std::size_t slot = 0;
		std::uint64_t serial = 0;
	public:
		bool empty() const { return serial == 0; }
	};
	// Join order within one simulation tick: the observation boundary of tick t
	// (AI deliveries) precedes the publications at the start of tick t's step.
	static constexpr std::uint64_t boundaryDue(std::uint64_t tick) { return 2 * tick; }
	static constexpr std::uint64_t advanceDue(std::uint64_t tick) { return 2 * tick + 1; }
	// A batch occupies a slot from submission until its join. Each deferred
	// producer submits at most one batch per tick and joins it at its horizon,
	// so it holds at most horizon + 1 slots: AI decisions (8), periodic
	// gradients (16), building gradients (8) and growth (16), plus headroom for tests and
	// teardown. The producers static_assert their horizons against these.
	static constexpr std::size_t AIHorizon = 8, GradientHorizon = 16, BuildingHorizon = 8, GrowthHorizon = 16;
	static constexpr std::size_t Slots = (AIHorizon + 1) + (GradientHorizon + 1) + (BuildingHorizon + 1) + (GrowthHorizon + 1) + 13;
private:
	using Clock = std::chrono::steady_clock;
	inline static thread_local ComputeExecutor *active = nullptr;
	inline static thread_local std::size_t activeSlot = 0;
	struct Slot
	{
		std::uint64_t serial = 0; // zero: free
		std::uint64_t due = 0;
		std::vector<Group> groups;
		std::vector<std::size_t> starts; // first job index of each group
		std::vector<std::uint64_t> laneBases; // lane sequence of each group's first job
		std::vector<char> claimed;
		std::size_t total = 0, unclaimed = 0, completed = 0, firstUnclaimed = 0;
		std::exception_ptr error;
	};
	struct Claim { std::size_t slot = 0, index = 0; bool valid = false; };
	std::vector<std::thread> workers;
	mutable std::mutex mutex;
	std::condition_variable ready, runFinished, slotDone;
	bool stopping = false;
	// run(): one blocking batch at a time. inFlight counts workers inside
	// invoke() for the current generation; the batch state changes only when
	// its jobs are done and no worker is still reading it.
	std::size_t generation = 0, runDone = 0, count = 0, inFlight = 0;
	std::atomic<std::size_t> next{0};
	std::function<void(std::size_t)> job;
	std::exception_ptr error;
	// submit()/join(): live batches, each held from submission to its join.
	std::array<Slot, Slots> slots;
	std::size_t live = 0;
	std::uint64_t nextSerial = 1;
	std::array<std::uint64_t, Lanes> laneIssued{}, laneCompleted{}, laneDue{};
	Metrics totals;
	struct WorkerMetrics { std::uint64_t jobs = 0, activeNs = 0; };
	std::vector<WorkerMetrics> workerMetrics{1};
	PresentationTicket presentation, presentationPending;
	bool presentationRunning = false;
	// Set before launching workers, never inferred from the vector while it grows.
	// Zero means there are no workers, so the owner runs deferred jobs itself.
	unsigned presentationWorker = 0;
	bool ownerRunsDeferred() const { return presentationWorker == 0; }
	PresentationMetrics presentationTotals;
	std::condition_variable presentationDone;

	bool presentationClaimable(std::size_t worker) const
	{
		return worker == presentationWorker && !presentationRunning && (presentation || presentationPending);
	}
	PresentationTicket claimPresentation()
	{
		if (!presentation) presentation = std::move(presentationPending);
		presentationRunning = true;
		presentation->state.store(Presentation::Status::Running, std::memory_order_release);
		return presentation;
	}
	void executePresentation(const PresentationTicket& work, std::size_t thread)
	{
		auto* previous = active;
		const auto previousSlot = activeSlot;
		active = this; activeSlot = thread;
		const auto start = Clock::now();
		bool ran = false;
		try
		{
			if (!work->canceled.load(std::memory_order_acquire))
			{
				if (work->job(work->next)) ++work->next;
				ran = true;
			}
		}
		catch (...) { work->error = std::current_exception(); }
		active = previous; activeSlot = previousSlot;
		// Release captured inputs outside the scheduler lock before publishing
		// completion. A ticket retained for status must not retain a world.
		const bool canceled = work->canceled.load(std::memory_order_acquire);
		const bool done = canceled || work->error || work->next == work->count;
		if (done) work->job = {};
		{
			std::lock_guard<std::mutex> lock(mutex);
			presentationTotals.chunks += ran;
			presentationTotals.activeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
			if (done)
			{
				work->state.store(work->error ? Presentation::Status::Failed : canceled ? Presentation::Status::Canceled : Presentation::Status::Complete,
					std::memory_order_release);
				presentation.reset();
			}
			presentationRunning = false;
		}
		presentationDone.notify_all();
		ready.notify_all();
	}

	void invoke(std::size_t slot)
	{
		auto *previous = active;
		const auto previousSlot = activeSlot;
		active = this;
		activeSlot = slot;
		const auto started = Clock::now();
		for (;;)
		{
			const auto i = next.fetch_add(1, std::memory_order_relaxed);
			if (i >= count) break;
			++workerMetrics[slot].jobs;
			try { job(i); }
			catch (...) { std::lock_guard<std::mutex> lock(mutex); if (!error) error = std::current_exception(); }
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (++runDone == count) runFinished.notify_all();
			}
		}
		workerMetrics[slot].activeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
		active = previous;
		activeSlot = previousSlot;
	}
	// Under mutex: the earliest due, then earliest submitted, live batch with a
	// claimable job. Only workers claim, unless there are none: then the owner
	// claims the jobs due no later than limit. claimable() has no side effect.
	static bool before(const Slot& a, const Slot& b) { return a.due != b.due ? a.due < b.due : a.serial < b.serial; }
	std::size_t group(const Slot& slot, std::size_t index) const
	{
		return std::size_t(std::upper_bound(slot.starts.begin(), slot.starts.end(), index) - slot.starts.begin()) - 1;
	}
	// A job is ready when its lane predecessor has completed.
	bool laneReady(const Slot& slot, std::size_t index) const
	{
		const auto g = group(slot, index);
		const auto lane = slot.groups[g].lane;
		return lane == NoLane || laneCompleted[lane] >= slot.laneBases[g] + (index - slot.starts[g]);
	}
	// The first unclaimed job of the slot this thread may claim, or total. An
	// inline owner runs every lane predecessor first (due no later, submitted
	// earlier), so it needs no readiness check.
	std::size_t firstClaimable(const Slot& slot, bool owner) const
	{
		for (std::size_t i = slot.firstUnclaimed; i < slot.total; ++i)
			if (!slot.claimed[i] && (owner || laneReady(slot, i))) return i;
		return slot.total;
	}
	Claim pick(bool owner, const Slot* limit) const
	{
		Claim best;
		for (std::size_t i = 0; i < Slots; ++i)
		{
			const auto& slot = slots[i];
			if (!slot.serial || !slot.unclaimed) continue;
			if (owner && limit && before(*limit, slot)) continue;
			if (best.valid && !before(slot, slots[best.slot])) continue;
			const auto index = firstClaimable(slot, owner);
			if (index != slot.total) best = {i, index, true};
		}
		return best;
	}
	bool claimable(bool owner, const Slot* limit = nullptr) const { return pick(owner, limit).valid; }
	Claim claim(bool owner, const Slot* limit = nullptr)
	{
		assert(owner == ownerRunsDeferred());
		const auto best = pick(owner, limit);
		if (!best.valid) return best;
		auto& slot = slots[best.slot];
		slot.claimed[best.index] = 1; --slot.unclaimed;
		while (slot.firstUnclaimed < slot.total && slot.claimed[slot.firstUnclaimed]) ++slot.firstUnclaimed;
		return best;
	}
	void freeSlot(Slot& slot)
	{
		slot.serial = 0; slot.groups.clear(); slot.starts.clear(); slot.laneBases.clear(); slot.claimed.clear();
		slot.total = slot.unclaimed = slot.completed = slot.firstUnclaimed = 0; slot.error = nullptr;
		--live;
	}
	// Outside the mutex: run one claimed deferred job, then record completion.
	void execute(const Claim& claim, std::size_t thread)
	{
		Group group; std::size_t offset = 0;
		{
			std::lock_guard<std::mutex> lock(mutex);
			auto& slot = slots[claim.slot];
			const auto g = this->group(slot, claim.index);
			group = slot.groups[g]; offset = claim.index - slot.starts[g];
			assert(group.lane == NoLane || laneCompleted[group.lane] == slot.laneBases[g] + offset);
		}
		auto *previous = active;
		const auto previousSlot = activeSlot;
		active = this; activeSlot = thread;
		const auto started = Clock::now();
		std::exception_ptr failure;
		try { group.job.invoke(group.job.context, offset); }
		catch (...) { failure = std::current_exception(); }
		workerMetrics[thread].activeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
		++workerMetrics[thread].jobs;
		active = previous; activeSlot = previousSlot;
		{
			std::lock_guard<std::mutex> lock(mutex);
			auto& slot = slots[claim.slot];
			if (failure && !slot.error) slot.error = failure;
			if (thread) ++totals.workerJobs; else ++totals.ownerJobs;
			if (group.lane != NoLane) { ++laneCompleted[group.lane]; ready.notify_all(); }
			if (++slot.completed == slot.total) slotDone.notify_all();
		}
	}
	void worker(std::size_t slot)
	{
		std::size_t seen = 0, simulationClaims = 0;
		std::unique_lock<std::mutex> lock(mutex);
		for (;;)
		{
			ready.wait(lock, [&] { return stopping || generation != seen || claimable(false) || presentationClaimable(slot); });
			if (stopping) return;
			// The designated worker interleaves presentation chunks with
			// simulation jobs, so neither starves the other: a join waits at
			// most one chunk for it, and other workers keep claiming meanwhile.
			if (simulationClaims >= 8 && presentationClaimable(slot))
			{
				const auto work = claimPresentation();
				simulationClaims = 0;
				lock.unlock();
				executePresentation(work, slot);
				lock.lock();
				continue;
			}
			if (generation != seen)
			{
				++simulationClaims;
				seen = generation;
				++inFlight;
				lock.unlock();
				invoke(slot);
				lock.lock();
				if (--inFlight == 0) runFinished.notify_all();
				continue;
			}
			const auto claimed = claim(false);
			if (!claimed.valid)
			{
				if (!presentationClaimable(slot)) continue;
				const auto work = claimPresentation();
				simulationClaims = 0;
				lock.unlock();
				executePresentation(work, slot);
				lock.lock();
				continue;
			}
			++simulationClaims;
			lock.unlock();
			execute(claimed, slot);
			lock.lock();
		}
	}
	void stop()
	{
		cancelPresentationAndWait();
		joinAll();
		{ std::lock_guard<std::mutex> lock(mutex); stopping = true; }
		ready.notify_all();
		for (auto &worker : workers) worker.join();
		workers.clear();
		stopping = false;
		generation = 0;
	}
public:
	ComputeExecutor() = default;
	ComputeExecutor(const ComputeExecutor &) = delete;
	ComputeExecutor &operator=(const ComputeExecutor &) = delete;
	~ComputeExecutor() { stop(); }
	// Configure only between batches; pending deferred work is joined first.
	// Thread creation failure retains a usable serial executor; caller reports
	// the actual thread count.
	void configure(unsigned threads,
		const std::function<std::thread(std::function<void()>)> &launch =
			[](std::function<void()> function) { return GAGCore::ThreadSupport::launch(std::move(function)); })
	{
		assert(!active && threads >= 1);
		stop();
		presentationWorker = threads > 1 ? threads - 1 : 0;
		if constexpr (GAGCore::ThreadSupport::available)
		{
			try
			{
				workers.reserve(threads > 0 ? threads - 1 : 0);
				for (unsigned i = 1; i < threads; ++i)
					workers.push_back(launch([this, i] { worker(i); }));
			}
			catch (...) { stop(); presentationWorker = 0; }
		}
		else presentationWorker = 0;
		workerMetrics.assign(threadCount(), {});
		totals = {};
		presentationTotals = {};
	}
	// The submitting thread owns admission. Replacing pending work releases its
	// captures immediately; active work finishes its current chunk on its worker.
	PresentationTicket submitPresentation(std::size_t chunks, std::function<void(std::size_t)> function)
	{
		if (!function) throw std::invalid_argument("Presentation needs nonempty work");
		return submitResumablePresentation(chunks, [function=std::move(function)](std::size_t chunk) {
			function(chunk);
			return true;
		});
	}
	// Returning false yields to simulation jobs, then resumes the same chunk.
	// This keeps data-dependent operations bounded without inspecting the world
	// or constructing an operation list on the submitting thread.
	PresentationTicket submitResumablePresentation(std::size_t chunks, std::function<bool(std::size_t)> function)
	{
		if (active) throw std::logic_error("Presentation cannot be submitted from inside a job");
		if (!chunks || !function) throw std::invalid_argument("Presentation needs nonempty work");
		auto work = std::make_shared<Presentation>();
		work->count = chunks; work->job = std::move(function);
		PresentationTicket replaced;
		{
			std::lock_guard<std::mutex> lock(mutex);
			replaced = std::move(presentationPending);
			presentationPending = work;
			++presentationTotals.submitted;
			if (replaced) ++presentationTotals.replaced;
		}
		if (replaced)
		{
			replaced->job = {};
			replaced->state.store(Presentation::Status::Canceled, std::memory_order_release);
		}
		ready.notify_all();
		return work;
	}
	// Graphics/application thread fallback for an executor with no workers.
	// This is deliberately not called by run(), join(), or joinAll().
	bool pumpPresentation()
	{
		if (active) throw std::logic_error("Presentation cannot be pumped from inside a job");
		PresentationTicket work;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (presentationWorker || !presentationClaimable(0)) return false;
			work = claimPresentation();
		}
		executePresentation(work, 0);
		return true;
	}
	// Lifecycle barrier only: does not run presentation on the caller. A running
	// chunk retains its inputs until it exits; unstarted chunks are discarded.
	void cancelPresentationAndWait()
	{
		if (active) throw std::logic_error("Presentation lifecycle barrier inside a job");
		PresentationTicket discarded, pending;
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (presentation) presentation->cancel();
			pending = std::move(presentationPending);
			presentationDone.wait(lock, [&] { return !presentationRunning; });
			discarded = std::move(presentation);
		}
		for (auto& work : {discarded, pending}) if (work)
		{
			work->job = {};
			work->state.store(Presentation::Status::Canceled, std::memory_order_release);
		}
	}
	PresentationMetrics presentationMetrics() const { std::lock_guard<std::mutex> lock(mutex); return presentationTotals; }
	std::size_t threadCount() const { return workers.size() + 1; }
	std::size_t slot() const { return active == this ? activeSlot : 0; }
	// A consistent copy; workers update the deferred counters under the mutex.
	Metrics metrics() const { std::lock_guard<std::mutex> lock(mutex); return totals; }
	// Sum of active elapsed times, NOT CPU time (the benchmark measures process CPU).
	std::uint64_t activeNs() const
	{
		std::uint64_t value = 0;
		for (const auto &worker : workerMetrics) value += worker.activeNs;
		return value;
	}
	void run(std::size_t n, const std::function<void(std::size_t)> &function)
	{
		if (!n) return;
		if (active)
		{
			for (std::size_t i = 0; i < n; ++i) function(i);
			return;
		}
		const auto start = Clock::now();
		++totals.batches;
		totals.jobs += n;
		if (n == 1 || workers.empty())
		{
			// Establish the same nesting context as a parallel batch.
			active = this; activeSlot = 0;
			try { for (std::size_t i = 0; i < n; ++i) function(i); }
			catch (...) { active = nullptr; throw; }
			active = nullptr;
			workerMetrics[0].jobs += n;
			workerMetrics[0].activeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
		}
		else
		{
			++totals.parallelBatches;
			{
				// A worker that woke late for the previous batch may still be
				// inside it; let it leave before the batch state is rewritten.
				std::unique_lock<std::mutex> lock(mutex);
				runFinished.wait(lock, [&] { return inFlight == 0; });
				job = function; count = n; next = 0; runDone = 0; error = nullptr;
				++generation;
			}
			ready.notify_all();
			invoke(0);
			const auto waitStart = Clock::now();
			// Completion counts jobs plus the workers still inside this batch: a
			// worker busy with a deferred job never joins it and cannot delay the
			// barrier, while one that did join must leave before the state changes.
			std::unique_lock<std::mutex> lock(mutex);
			runFinished.wait(lock, [&] { return runDone == count && inFlight == 0; });
			totals.waitNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - waitStart).count();
			job = {};
			if (error) std::rethrow_exception(error);
		}
		totals.batchNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
	}
	// Queue a batch of groups, due at the given tick key; groups are copied.
	// Returns an empty batch when nothing was submitted. Never call from inside a job.
	Batch submit(std::span<const Group> groups, std::uint64_t due = 0)
	{
		if (active) throw std::logic_error("Deferred batches cannot be submitted from inside a job");
		Batch batch;
		std::size_t total = 0;
		for (const auto& group : groups)
		{
			if (group.lane != NoLane && group.lane >= Lanes) throw std::invalid_argument("Compute lane index exceeds the lane count");
			total += group.count;
		}
		if (!total) return batch;
		std::unique_lock<std::mutex> lock(mutex);
		if (live == Slots) throw std::logic_error("Compute executor exceeded its deferred batch horizon");
		// Validate before touching executor state: a partial submission would
		// leave lane sequences nobody completes. Due order never contradicts
		// lane order, so an inline owner can run lanes in due order.
		for (const auto& group : groups)
			if (group.lane != NoLane && laneCompleted[group.lane] < laneIssued[group.lane] && due < laneDue[group.lane])
				throw std::logic_error("Compute lane job due before an earlier one");
		std::size_t index = 0;
		while (slots[index].serial) ++index;
		auto& slot = slots[index];
		slot.serial = nextSerial++;
		slot.due = due;
		slot.groups.assign(groups.begin(), groups.end());
		slot.starts.clear(); slot.laneBases.clear();
		std::size_t start = 0;
		for (const auto& group : slot.groups)
		{
			slot.starts.push_back(start); start += group.count;
			slot.laneBases.push_back(group.lane == NoLane ? 0 : laneIssued[group.lane]);
			if (group.lane != NoLane) { laneIssued[group.lane] += group.count; laneDue[group.lane] = due; }
		}
		slot.total = slot.unclaimed = total; slot.completed = slot.firstUnclaimed = 0; slot.error = nullptr;
		slot.claimed.assign(total, 0);
		++live;
		++totals.deferredBatches; totals.deferredJobs += total;
		batch.slot = index; batch.serial = slot.serial;
		lock.unlock();
		if (!ownerRunsDeferred()) ready.notify_all();
		return batch;
	}
	// True once every job of the batch has completed (or the batch was joined).
	bool finished(const Batch& batch) const
	{
		if (batch.empty()) return true;
		std::lock_guard<std::mutex> lock(mutex);
		const auto& slot = slots[batch.slot];
		return slot.serial != batch.serial || slot.completed == slot.total;
	}
	// Wait for the batch (with no workers, first run the jobs due no later than
	// it), then rethrow the batch's first error. The batch becomes empty and its
	// slot free.
	void join(Batch& batch)
	{
		if (batch.empty()) return;
		if (active) throw std::logic_error("Deferred batches cannot be joined from inside a job");
		std::unique_lock<std::mutex> lock(mutex);
		auto& slot = slots[batch.slot];
		if (slot.serial != batch.serial) { batch = {}; return; }
		for (Claim claimed; ownerRunsDeferred() && (claimed = claim(true, &slot)).valid; lock.lock())
		{
			lock.unlock();
			execute(claimed, 0);
		}
		if (slot.completed != slot.total)
		{
			const auto waitStart = Clock::now();
			slotDone.wait(lock, [&] { return slot.completed == slot.total; });
			totals.joinWaitNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - waitStart).count();
		}
		const auto failure = slot.error;
		freeSlot(slot);
		batch = {};
		lock.unlock();
		if (failure) std::rethrow_exception(failure);
	}
	// Complete and free every live batch; errors of individual batches are
	// discarded here, since the batches' owners have given up their handles.
	void joinAll()
	{
		if (active) return;
		std::unique_lock<std::mutex> lock(mutex);
		for (Claim claimed; ownerRunsDeferred() && (claimed = claim(true)).valid; lock.lock())
		{
			lock.unlock();
			execute(claimed, 0);
		}
		slotDone.wait(lock, [&] {
			for (const auto& slot : slots) if (slot.serial && slot.completed != slot.total) return false;
			return true;
		});
		for (auto& slot : slots) if (slot.serial) freeSlot(slot);
	}
	std::size_t liveBatches() const { std::lock_guard<std::mutex> lock(mutex); return live; }
};
