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
//  * submit()/join(): deferred batches of plain jobs with a due tick decided by
//    the submitter. Workers drain live batches oldest first; the submitting
//    thread participates when it joins, claiming from the oldest live batch
//    forward, so a join never idles while work remains. Lanes serialize the
//    successive jobs of one producer (one AI controller's decisions) without
//    overlap; because claiming is FIFO, a lane wait always waits on a job that
//    is already running, so lanes cannot deadlock.
//
// Only one thread submits and joins deferred batches, and never from inside a
// job. Placement::OwnerOnly batches are invisible to workers: joining executes
// them serially on the submitter in the same order, so a game without compute
// threads follows the identical schedule. A lane's live batches must share one
// placement: a worker holding a Shared lane job would otherwise wait on an
// OwnerOnly predecessor until the submitter joins. submit() enforces this.
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
		std::uint64_t laneWaitNs = 0, joinWaitNs = 0;
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
	enum class Placement { Shared, OwnerOnly };
	class Batch
	{
		friend class ComputeExecutor;
		std::size_t slot = 0;
		std::uint64_t serial = 0;
	public:
		bool empty() const { return serial == 0; }
	};
	// Two deferred producers (AI and gradients), up to sixteen ticks each,
	// plus current submissions and retirement boundary headroom.
	static constexpr std::size_t Slots = 36;
private:
	using Clock = std::chrono::steady_clock;
	inline static thread_local ComputeExecutor *active = nullptr;
	inline static thread_local std::size_t activeSlot = 0;
	struct Slot
	{
		std::uint64_t serial = 0; // zero: free
		Placement placement = Placement::Shared;
		std::vector<Group> groups;
		std::vector<std::size_t> starts; // first job index of each group
		std::vector<std::uint64_t> laneBases; // lane sequence of each group's first job
		std::size_t total = 0, next = 0, completed = 0;
		std::exception_ptr error;
	};
	struct Claim { std::size_t slot = 0, index = 0; bool valid = false; };
	std::vector<std::thread> workers;
	mutable std::mutex mutex;
	std::condition_variable ready, runFinished, slotDone, laneChanged;
	bool stopping = false;
	// run(): one blocking batch at a time. inFlight counts workers inside
	// invoke() for the current generation; the batch state changes only when
	// its jobs are done and no worker is still reading it.
	std::size_t generation = 0, runDone = 0, count = 0, inFlight = 0;
	std::atomic<std::size_t> next{0};
	std::function<void(std::size_t)> job;
	std::exception_ptr error;
	// submit()/join(): a ring of deferred batches, oldest first.
	std::array<Slot, Slots> slots;
	std::size_t oldest = 0, live = 0;
	std::uint64_t nextSerial = 1;
	std::array<std::uint64_t, Lanes> laneIssued{}, laneCompleted{};
	Metrics totals;
	struct WorkerMetrics { std::uint64_t jobs = 0, activeNs = 0; };
	std::vector<WorkerMetrics> workerMetrics{1};
	PresentationTicket presentation, presentationPending;
	bool presentationRunning = false;
	// Set before launching workers, never inferred from the vector while it grows.
	unsigned presentationWorker = 0;
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
	// Under mutex: the oldest live batch with unclaimed jobs, respecting
	// placement; owners may claim from any live batch. claimable() has no side
	// effect and is what wait predicates use.
	bool claimable(bool owner) const
	{
		for (std::size_t n = 0; n < live; ++n)
		{
			const auto& slot = slots[(oldest + n) % Slots];
			if (!owner && slot.placement == Placement::OwnerOnly) continue;
			if (slot.next < slot.total) return true;
		}
		return false;
	}
	Claim claim(bool owner)
	{
		for (std::size_t n = 0; n < live; ++n)
		{
			auto& slot = slots[(oldest + n) % Slots];
			if (!owner && slot.placement == Placement::OwnerOnly) continue;
			if (slot.next < slot.total) return {(oldest + n) % Slots, slot.next++, true};
		}
		return {};
	}
	// Outside the mutex: run one claimed deferred job, then record completion.
	void execute(const Claim& claim, std::size_t thread)
	{
		Group group; std::size_t offset = 0; std::uint64_t laneSequence = 0;
		{
			std::lock_guard<std::mutex> lock(mutex);
			auto& slot = slots[claim.slot];
			const auto g = std::size_t(std::upper_bound(slot.starts.begin(), slot.starts.end(), claim.index) - slot.starts.begin()) - 1;
			group = slot.groups[g]; offset = claim.index - slot.starts[g];
			laneSequence = slot.laneBases[g] + offset;
		}
		if (group.lane != NoLane)
		{
			const auto waitStart = Clock::now();
			std::unique_lock<std::mutex> lock(mutex);
			if (laneCompleted[group.lane] < laneSequence)
			{
				laneChanged.wait(lock, [&] { return laneCompleted[group.lane] >= laneSequence; });
				totals.laneWaitNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - waitStart).count();
			}
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
			if (group.lane != NoLane) { ++laneCompleted[group.lane]; laneChanged.notify_all(); }
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
			// The designated worker lends capacity to simulation, but must not
			// starve presentation under a continuous deferred backlog. The owner
			// and other workers remain available to every simulation barrier.
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
	// Under mutex: release completed batches from the oldest end of the ring.
	void release()
	{
		while (live && slots[oldest].completed == slots[oldest].total)
		{
			auto& slot = slots[oldest];
			slot.serial = 0; slot.groups.clear(); slot.starts.clear(); slot.laneBases.clear();
			slot.total = slot.next = slot.completed = 0; slot.error = nullptr;
			oldest = (oldest + 1) % Slots; --live;
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
		assert(!active && threads >= 1 && threads <= 64);
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
	// Queue a batch of groups; groups are copied. Returns an empty batch when
	// nothing was submitted. Never call from inside a job.
	Batch submit(std::span<const Group> groups, Placement placement = Placement::Shared)
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
		// Validate everything before touching executor state: a partial
		// submission would leave lane sequences nobody completes.
		for (const auto& group : groups)
			if (group.lane != NoLane)
				for (std::size_t n = 0; n < live; ++n)
				{
					const auto& other = slots[(oldest + n) % Slots];
					if (other.placement == placement || other.completed == other.total) continue;
					for (const auto& candidate : other.groups)
						if (candidate.lane == group.lane) throw std::logic_error("Compute lane mixes placements across live batches");
				}
		const auto index = (oldest + live) % Slots;
		auto& slot = slots[index];
		slot.serial = nextSerial++;
		slot.placement = placement;
		slot.groups.assign(groups.begin(), groups.end());
		slot.starts.clear(); slot.laneBases.clear();
		std::size_t start = 0;
		for (const auto& group : slot.groups)
		{
			slot.starts.push_back(start); start += group.count;
			slot.laneBases.push_back(group.lane == NoLane ? 0 : laneIssued[group.lane]);
			if (group.lane != NoLane) laneIssued[group.lane] += group.count;
		}
		slot.total = total; slot.next = 0; slot.completed = 0; slot.error = nullptr;
		++live;
		++totals.deferredBatches; totals.deferredJobs += total;
		batch.slot = index; batch.serial = slot.serial;
		lock.unlock();
		if (placement == Placement::Shared && !workers.empty()) ready.notify_all();
		return batch;
	}
	// True once every job of the batch has completed (or the batch was released).
	bool finished(const Batch& batch) const
	{
		if (batch.empty()) return true;
		std::lock_guard<std::mutex> lock(mutex);
		const auto& slot = slots[batch.slot];
		return slot.serial != batch.serial || slot.completed == slot.total;
	}
	// Complete the batch, executing remaining jobs oldest batch first, then
	// rethrow the batch's first error. The batch becomes empty.
	void join(Batch& batch)
	{
		if (batch.empty()) return;
		if (active) throw std::logic_error("Deferred batches cannot be joined from inside a job");
		std::unique_lock<std::mutex> lock(mutex);
		auto current = [&]() -> Slot* { auto& slot = slots[batch.slot]; return slot.serial == batch.serial ? &slot : nullptr; };
		while (auto* slot = current())
		{
			if (slot->completed == slot->total) break;
			const auto claimed = claim(true);
			if (claimed.valid)
			{
				lock.unlock();
				execute(claimed, 0);
				lock.lock();
				continue;
			}
			const auto waitStart = Clock::now();
			slotDone.wait(lock, [&] { auto* s = current(); return !s || s->completed == s->total || claimable(true); });
			totals.joinWaitNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - waitStart).count();
		}
		std::exception_ptr failure;
		if (auto* slot = current()) failure = slot->error;
		release();
		batch = {};
		lock.unlock();
		if (failure) std::rethrow_exception(failure);
	}
	// Complete every live batch; errors of individual batches are discarded
	// here, since the batches' owners have given up their handles.
	void joinAll()
	{
		if (active) return;
		std::unique_lock<std::mutex> lock(mutex);
		for (;;)
		{
			const auto claimed = claim(true);
			if (claimed.valid) { lock.unlock(); execute(claimed, 0); lock.lock(); continue; }
			release();
			if (!live) break;
			slotDone.wait(lock, [&] { return claimable(true) || slots[oldest].completed == slots[oldest].total; });
		}
	}
	std::size_t liveBatches() const { std::lock_guard<std::mutex> lock(mutex); return live; }
};
