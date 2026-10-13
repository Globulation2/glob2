// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <bit>
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
#include <PresentationWork.h>
#include <vector>
#include <utility>
#include <cstdlib>
#include <cstring>
#include "ComputeTelemetry.h"

// One executor shares the simulation's workers with optional presentation work.
// Required simulation work has two shapes:
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
// Optional scene preparation is resumable and runs on a designated worker;
// bounded graphics batches share its opportunities without entering simulation
// barriers. Graphics inputs belong to the submitting owner until synchronous
// completion. Worker-only maintenance uses idle opportunities and cannot retain
// game inputs. Neither presentation nor maintenance is pumped by required joins.
class ComputeExecutor : public GAGCore::PresentationWork
{
public:
	// Optional maintenance is never a deferred Group: join/run/owner fallback
	// cannot execute it. A pass must be bounded and must not retain game inputs.
	// No ticket or completion barrier is exposed to required work.
	class WorkerOnly
	{
	public:
		virtual ~WorkerOnly() = default;
		virtual bool pending() const noexcept = 0;
		virtual void process() noexcept = 0;
	};
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
    struct GraphicsMetrics
    {
        std::uint64_t batches = 0, parallelBatches = 0, jobs = 0;
        std::uint64_t queueNs = 0, activeNs = 0, waitNs = 0;
    };
    struct PresentationBatchOptions
    {
        std::size_t maximumConcurrency = 2;
    };
    class PresentationCancellationToken
    {
        const std::atomic<bool> *flag;

    public:
        explicit PresentationCancellationToken(const std::atomic<bool> *flag) : flag(flag) {}
        bool canceled() const { return flag->load(std::memory_order_acquire); }
    };
    struct PresentationJobContext
    {
        std::size_t index;
        PresentationCancellationToken cancellation;
    };

private:
    struct GraphicsBatch
    {
        std::mutex mutex;
        std::condition_variable done;
        std::atomic<bool> canceled{false}, finished{false};
        std::atomic<Presentation::Status> status{Presentation::Status::Pending};
        std::size_t next = 0, count = 0, running = 0, limit = 2;
        bool serial = false, settling = false;
        ComputeExecutor *owner = nullptr;

        std::function<void(PresentationJobContext)> job;
        std::exception_ptr error;
        // The caller holds only the batch mutex. Captured storage is released
        // with neither scheduler nor batch mutex held, before completion.
        void settle(std::unique_lock<std::mutex>& lock)
        {
            if (settling || finished.load(std::memory_order_acquire))
                return;
            if (!running && (canceled.load() || next == count))
            {
                settling = true;
                // A moved-from std::function may keep its inline callable on
                // libc++. Swap guarantees that the retained state is empty.
                std::function<void(PresentationJobContext)> released;
                released.swap(job);
                lock.unlock();
                auto* previous = active;
                active = owner; // Lifecycle barriers remain forbidden in capture destructors.
                released = {};
                active = previous;
                // Completion also wakes an owner waiting for a scene slot.
                // Publish under the scheduler mutex to avoid a lost wakeup;
                // the unfinished batch keeps stop() draining during handoff.
                std::unique_lock schedulerLock(owner->mutex);
                lock.lock();
                status.store(error             ? Presentation::Status::Failed
                             : canceled.load() ? Presentation::Status::Canceled
                                               : Presentation::Status::Complete,
                             std::memory_order_release);
                finished.store(true, std::memory_order_release);
                done.notify_all();
                owner->graphicsReady.notify_all();
                owner->ready.notify_all();
            }
        }
        void cancel()
        {
            std::unique_lock lock(mutex);
            if (finished.load(std::memory_order_acquire))
                return;
            canceled.store(true, std::memory_order_release);
            settle(lock);
        }
        struct DrainObservation {
            bool incomplete = false;
            std::uint64_t lockNs = 0, waitNs = 0, inspected = 0, completed = 0;
        };
        template<ComputeTelemetryMode Mode>
        DrainObservation drainMeasured(std::uint64_t (*clock)() noexcept) {
            DrainObservation result;
            const auto start = [&] { if constexpr (Mode != ComputeTelemetryMode::Off) return clock(); else return std::uint64_t(0); }();
            std::unique_lock lock(mutex);
            if constexpr (Mode != ComputeTelemetryMode::Off) {
                result.inspected = clock(); result.lockNs = result.inspected-start;
                result.incomplete = !finished.load(std::memory_order_acquire);
            }
            done.wait(lock, [&] { return finished.load(std::memory_order_acquire); });
            if constexpr (Mode != ComputeTelemetryMode::Off) { result.completed = clock(); result.waitNs = result.completed-result.inspected; }
            return result;
        }
        void drain() { drainMeasured<ComputeTelemetryMode::Off>(nullptr); }
    };

public:
    // Move-only ownership makes abandonment a cancellation barrier. The state
    // survives executor shutdown for failure/status inspection, without inputs.
    class PresentationBatchTicket
    {
        friend class ComputeExecutor;
        std::shared_ptr<GraphicsBatch> work;
        explicit PresentationBatchTicket(std::shared_ptr<GraphicsBatch> work)
            : work(std::move(work))
        {
        }

    public:
        PresentationBatchTicket() = default;
        PresentationBatchTicket(const PresentationBatchTicket &) = delete;
        PresentationBatchTicket &operator=(const PresentationBatchTicket &) = delete;
        PresentationBatchTicket(PresentationBatchTicket &&) = default;
        PresentationBatchTicket &operator=(PresentationBatchTicket &&other)
        {
            if (this != &other)
            {
                dispose();
                work = std::move(other.work);
            }
            return *this;
        }
        ~PresentationBatchTicket() { dispose(); }
        void dispose()
        {
            if (work && !finished())
            {
                // Destruction inside a callback could wait for itself.
                if (ComputeExecutor::active)
                    std::terminate();
                work->cancel();
                work->drain();
            }
        }
        void cancel()
        {
            if (work)
                work->cancel();
        }
        bool finished() const { return !work || work->finished.load(std::memory_order_acquire); }
        Presentation::Status status() const
        {
            return work ? work->status.load(std::memory_order_acquire)
                        : Presentation::Status::Complete;
        }
        void rethrowFailure() const
        {
            if (status() == Presentation::Status::Failed)
                std::rethrow_exception(work->error);
        }
    };
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
	// A suspended deferred job keeps its batch and lane incomplete while its
	// worker runs other work. Resolution is one-shot, including early resolution
	// before the invoking callback returns. No completion retains game inputs.
	struct CompletionLifetime {
		std::mutex mutex;
		ComputeExecutor* executor;
		explicit CompletionLifetime(ComputeExecutor* executor):executor(executor) {}
	};
	class Completion
	{
		friend class ComputeExecutor;
		std::atomic<ComputeExecutor*> owner;
		std::shared_ptr<CompletionLifetime> lifetime;
		std::size_t slot, index;
		std::uint64_t serial;
		bool running = true, resolved = false;
		Job continuation;
		std::exception_ptr error;
		Completion(ComputeExecutor* owner, std::size_t slot, std::size_t index, std::uint64_t serial)
			: owner(owner), lifetime(owner->completionLifetime), slot(slot), index(index), serial(serial) {}
	public:
		bool complete(std::exception_ptr error = {}) {
			std::lock_guard lock(lifetime->mutex);
			auto* target = lifetime->executor == owner.load(std::memory_order_acquire) ? lifetime->executor : nullptr;
			return target && target->resolve(*this, {}, error);
		}
		bool resume(Job continuation) {
			if (!continuation.invoke) throw std::invalid_argument("Completion needs a nonempty continuation");
			std::lock_guard lock(lifetime->mutex);
			auto* target = lifetime->executor == owner.load(std::memory_order_acquire) ? lifetime->executor : nullptr;
			return target && target->resolve(*this, continuation, {});
		}
	};
	using CompletionTicket = std::shared_ptr<Completion>;
	static constexpr unsigned NoLane = ~0u;
	// AI occupies player lanes 0–15; all statistics batches share lane 16.
	static constexpr unsigned StatisticsLaneBase = 16;
	static constexpr unsigned HiringLaneBase = 32;
	static constexpr unsigned Lanes = 48;
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
	// gradients (16), building gradients (8), growth (16), statistics (8), hiring (4),
	// plus headroom for tests and teardown. Producers assert their horizons.
	static constexpr std::size_t AIHorizon = 8, GradientHorizon = 16, BuildingHorizon = 8, GrowthHorizon = 16, StatisticsHorizon = 8, HiringHorizon = 4;
	static constexpr std::size_t Slots = (AIHorizon + 1) + (GradientHorizon + 1) + (BuildingHorizon + 1) + (GrowthHorizon + 1) + (StatisticsHorizon + 1) + (HiringHorizon + 1) + 13;
private:
	using Clock = std::chrono::steady_clock;
	inline static thread_local ComputeExecutor *active = nullptr;
	inline static thread_local std::size_t activeSlot = 0;
	inline static thread_local std::size_t deferredSlot = 0, deferredIndex = 0;
	inline static thread_local std::uint64_t deferredSerial = 0;
	struct Slot
	{
		std::uint64_t serial = 0; // zero: free
		std::uint64_t due = 0;
		std::vector<Group> groups;
		std::vector<std::size_t> starts; // first job index of each group
		std::vector<std::uint64_t> laneBases; // lane sequence of each group's first job
		std::vector<char> claimed;
		std::vector<CompletionTicket> completions;
		std::vector<Job> continuations;
		std::size_t total = 0, unclaimed = 0, completed = 0, firstUnclaimed = 0;
		std::exception_ptr error;
	};
	struct Claim { std::size_t slot = 0, index = 0; bool valid = false; };
	std::shared_ptr<CompletionLifetime> completionLifetime = std::make_shared<CompletionLifetime>(this);
	std::vector<std::thread> workers;
	mutable std::mutex mutex;
	// Only workers wait on ready: a targeted deferred-work wakeup must not
	// be consumed by an owner waiting for a presentation slot or trace drain.
	std::condition_variable ready, graphicsReady, runFinished, slotDone;
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
	// Under mutex. Pick visits only batches with unclaimed work, rather than
	// branching over the entire horizon (including completed retained tickets).
	std::array<std::uint64_t, (Slots + 63) / 64> unclaimedSlots{};
	std::size_t unclaimedBatches = 0;
	void markUnclaimed(std::size_t index, bool value)
	{
		const auto bit = std::uint64_t(1) << (index % 64);
		if (value) unclaimedSlots[index / 64] |= bit;
		else unclaimedSlots[index / 64] &= ~bit;
		unclaimedBatches += value ? 1 : std::size_t(-1);
	}
	std::size_t live = 0;
	std::uint64_t nextSerial = 1;
	std::array<std::uint64_t, Lanes> laneIssued{}, laneCompleted{}, laneDue{};

    ComputeTelemetryMode telemetryModeValue = ComputeTelemetryMode::Off, requestedTelemetryMode = ComputeTelemetryMode::Off;
    using DiagnosticClock = std::uint64_t (*)() noexcept;
    static std::uint64_t realDiagnosticClock() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    }
    struct DiagnosticsState {
        struct BatchRecord { std::uint64_t serial = 0; BatchDiagnostics tags; };
        std::array<BatchRecord, Slots> batches{};
        ComputeTelemetry::Summary summary;
        std::array<std::uint64_t, unsigned(ComputeProducer::Count)> lastProducerTick;
        std::uint64_t lastBackgroundTick = ComputeTelemetry::NoTick, lastAdvanceTick = ComputeTelemetry::NoTick;
        std::uint64_t currentTick = ComputeTelemetry::NoTick, tickWaitNs = 0;
        bool tickWaited = false;
        DiagnosticClock clock = realDiagnosticClock;
        std::vector<ComputeTelemetry::Stream> streams;
        std::size_t traceBytes = 0;
        std::uint64_t nextPresentation = 1, currentPresentation = 0, pendingPresentation = 0;
        std::uint64_t runSerial = 0, graphicsSerial = 0, graphicsSubmitted = 0, maintenanceSerial = 0;
        std::size_t traceGraphicsInFlight = 0;
        bool traceRunActive = false, graphicsJoined = false, traceCoverageGap = false;
        std::uint64_t epoch = 0;
        BatchDiagnostics runTags;
        DiagnosticsState() { lastProducerTick.fill(ComputeTelemetry::NoTick); }
        void selectTick(std::uint64_t tick) noexcept {
            if (currentTick == tick) return;
            if (currentTick != ComputeTelemetry::NoTick && tickWaited) summary.tickBackground.add(tickWaitNs);
            currentTick = tick; tickWaitNs = 0; tickWaited = false;
        }
        void waited(ComputeProducer producer, std::uint64_t tick, std::uint64_t ns) noexcept {
            selectTick(tick);
            tickWaitNs += ns; tickWaited = true;
            if (lastBackgroundTick != tick) { ++summary.backgroundTicks; lastBackgroundTick = tick; }
            auto& last = lastProducerTick[unsigned(producer)];
            if (last != tick) { ++summary.affectedTicks[unsigned(producer)]; last = tick; }
        }
    };
    std::unique_ptr<DiagnosticsState> diagnostics;
    // Trace writes use the scheduler lock, except run callback starts whose
    // single-writer stream is protected by the run barrier during snapshots.
    template<ComputeTelemetryMode Mode>
    void event(ComputeTelemetry::EventKind kind, std::size_t slotIndex, std::size_t index = 0,
        unsigned thread = 0, JoinContext context = {JoinReason::Teardown, ComputeTelemetry::NoTick}, unsigned classification = 0, std::uint64_t timestamp = ComputeTelemetry::NoTick) noexcept {
        if constexpr (Mode == ComputeTelemetryMode::Trace) {
            const auto& slot = slots[slotIndex];
            const auto& record = diagnostics->batches[slotIndex];
            if (record.serial != slot.serial) return;
            ComputeTelemetry::Event value;
            value.timestampNs = timestamp == ComputeTelemetry::NoTick ? diagnostics->clock() : timestamp; value.serial = slot.serial; value.due = slot.due;
            value.kind = kind; value.job = index; value.thread = thread; value.producer = record.tags.producer;
            value.observationTick = record.tags.observationTick; value.publicationTick = record.tags.publicationTick;
            value.logicalTick = context.logicalTick; value.reason = context.reason; value.classification = classification;
            if (index < slot.total) {
                const auto g = group(slot, index); value.lane = slot.groups[g].lane;
                value.laneSequence = slot.laneBases[g] + index - slot.starts[g];
            }
            // External resolvers share one stream, serialized by the existing mutex.
            const auto stream = thread == ~0u ? diagnostics->streams.size()-1 : thread;
            diagnostics->streams[stream].append(value);
        }
    }

    template<ComputeTelemetryMode Mode>
    void standaloneEvent(ComputeTelemetry::EventKind kind, unsigned executionKind, std::uint64_t serial,
        std::size_t index, unsigned thread, JoinContext context = {JoinReason::Teardown, ComputeTelemetry::NoTick},
        unsigned classification = 0, std::uint64_t timestamp = ComputeTelemetry::NoTick) noexcept {
        if constexpr (Mode == ComputeTelemetryMode::Trace) {
            ComputeTelemetry::Event value;
            value.timestampNs = timestamp == ComputeTelemetry::NoTick ? diagnostics->clock() : timestamp; value.serial = serial;
            value.logicalTick = context.logicalTick; value.reason = context.reason; value.classification = classification;
            value.kind = kind; value.job = index; value.thread = thread; value.executionKind = executionKind;
            value.producer = executionKind == 2 || executionKind == 3 ? ComputeProducer::Presentation
                : executionKind == 1 ? diagnostics->runTags.producer : ComputeProducer::Other;
            if (executionKind == 1) {
                value.observationTick = diagnostics->runTags.observationTick;
                value.publicationTick = diagnostics->runTags.publicationTick;
            }
            const auto stream = executionKind >= 2 && !thread ? diagnostics->streams.size()-2 : thread;
            diagnostics->streams[stream].append(value);
        }
    }
    template<ComputeTelemetryMode Mode> std::uint64_t diagnosticStart() const noexcept {
        if constexpr (Mode != ComputeTelemetryMode::Off) return diagnostics->clock(); else return 0;
    }
    template<ComputeTelemetryMode Mode> std::uint64_t diagnosticElapsed(std::uint64_t start) const noexcept {
        if constexpr (Mode != ComputeTelemetryMode::Off) return diagnostics->clock() - start; else return 0;
    }
	Metrics totals;
	struct WorkerMetrics { std::uint64_t jobs = 0, activeNs = 0; };
	std::vector<WorkerMetrics> workerMetrics;
	PresentationTicket presentation, presentationPending;
	bool presentationRunning = false;
    std::shared_ptr<GraphicsBatch> graphicsBatch;
    std::size_t graphicsRunning = 0;
    bool preferGraphics = false;
    GraphicsMetrics graphicsTotals;
	// Set before launching workers, never inferred from the vector while it grows.
	// Zero means there are no workers, so the owner runs deferred jobs itself.
	unsigned presentationWorker = 0;
	bool ownerRunsDeferred() const { return presentationWorker == 0; }
	PresentationMetrics presentationTotals;
	std::condition_variable presentationDone;
	std::shared_ptr<WorkerOnly> maintenance;
	bool maintenanceRunning = false;
	// Optional observations do not wake the pool. Returning workers and normal
	// required-work wakeups provide opportunities; idle tail samples may remain
	// pending until another opportunity or lifecycle disposal. Registration still
	// wakes workers, allowing asynchronous backend preparation before first work.
	bool maintenanceClaimable(std::size_t worker) const
	{
		return worker != presentationWorker && maintenance && !maintenanceRunning && !presentation && !presentationPending && maintenance->pending();
	}

    std::size_t presentationLimit() const
    {
        return graphicsBatch && (!graphicsBatch->finished.load() || graphicsRunning)
                   ? graphicsBatch->limit
                   : 2;
    }
	bool presentationClaimable(std::size_t worker) const
	{
        return worker == presentationWorker && graphicsRunning < presentationLimit() &&
               !presentationRunning && (presentation || presentationPending);
	}
	template<ComputeTelemetryMode Mode>
	PresentationTicket claimPresentation(unsigned thread)
	{
		if (!presentation) {
            presentation = std::move(presentationPending);
            if constexpr (Mode == ComputeTelemetryMode::Trace) diagnostics->currentPresentation = std::exchange(diagnostics->pendingPresentation, 0);
        }
		presentationRunning = true;
		presentation->state.store(Presentation::Status::Running, std::memory_order_release);
		standaloneEvent<Mode>(ComputeTelemetry::EventKind::Claim, 2, diagnosticsSerial<Mode>(), presentation->next, thread);
        standaloneEvent<Mode>(ComputeTelemetry::EventKind::CallbackStart, 2, diagnosticsSerial<Mode>(), presentation->next, thread);
		return presentation;
	}
    template<ComputeTelemetryMode Mode> std::uint64_t graphicsSerial() const noexcept {
        if constexpr (Mode == ComputeTelemetryMode::Trace) return diagnostics->graphicsSerial; else return 0;
    }
    template<ComputeTelemetryMode Mode> std::uint64_t runSerial() const noexcept {
        if constexpr (Mode == ComputeTelemetryMode::Trace) return diagnostics->runSerial; else return 0;
    }
    template<ComputeTelemetryMode Mode> std::uint64_t diagnosticsSerial() const noexcept {
        if constexpr (Mode == ComputeTelemetryMode::Trace) return diagnostics->currentPresentation; else return 0;
    }
	template<ComputeTelemetryMode Mode>
	void executePresentation(const PresentationTicket& work, std::size_t thread)
	{
		auto* previous = active;
		const auto previousSlot = activeSlot;
		active = this; activeSlot = thread;
		const auto start = diagnosticStart<Mode>();
        const auto callbackIndex = [&] { if constexpr (Mode == ComputeTelemetryMode::Trace) return work->next; else return std::size_t(0); }();
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
			if constexpr (Mode == ComputeTelemetryMode::Trace) { presentationTotals.chunks += ran; }
			if constexpr (Mode == ComputeTelemetryMode::Trace) { presentationTotals.activeNs += diagnosticElapsed<Mode>(start); }
			if (done)
			{
				work->state.store(work->error ? Presentation::Status::Failed : canceled ? Presentation::Status::Canceled : Presentation::Status::Complete,
					std::memory_order_release);
				presentation.reset();
			}
            standaloneEvent<Mode>(ComputeTelemetry::EventKind::CallbackEnd, 2, diagnosticsSerial<Mode>(), callbackIndex, unsigned(thread));
            if (done) {
                standaloneEvent<Mode>(ComputeTelemetry::EventKind::Complete, 2, diagnosticsSerial<Mode>(), callbackIndex, unsigned(thread));
                standaloneEvent<Mode>(ComputeTelemetry::EventKind::Retire, 2, diagnosticsSerial<Mode>(), work->count, unsigned(thread));
                if constexpr (Mode == ComputeTelemetryMode::Trace) diagnostics->currentPresentation = 0;
            }
			presentationRunning = false;
		}
		presentationDone.notify_all();
		graphicsReady.notify_all();
		ready.notify_all();
	}

    bool graphicsClaimable(bool owner = false, std::size_t worker = 0) const
    {
        if (!graphicsBatch ||
            graphicsRunning + std::size_t(presentationRunning) >= presentationLimit())
            return false;
        // Keep a slot available for the designated worker when scene work is
        // waiting. It may alternate that slot between the two classes; spare
        // workers cannot repeatedly fill it before scene preparation claims.
        if (!owner && worker != presentationWorker && !presentationRunning &&
            (presentation || presentationPending) && graphicsRunning >= presentationLimit()-1)
            return false;
        std::lock_guard lock(graphicsBatch->mutex);
        return (owner || !graphicsBatch->serial) && !graphicsBatch->canceled.load() &&
               graphicsBatch->next < graphicsBatch->count &&
               graphicsBatch->running < graphicsBatch->limit;
    }
    bool cpuPresentationClaimable(std::size_t worker) const
    {
        return presentationClaimable(worker) || graphicsClaimable(false, worker);
    }
    // Scheduler mutex held on entry and exit. Alternate both presentation
    // classes at the designated worker's reserved opportunity.
    template<ComputeTelemetryMode Mode>
    void executeCpuPresentation(std::unique_lock<std::mutex> &lock, std::size_t worker,
                                bool sceneAllowed = true)
    {
        if (sceneAllowed && presentationClaimable(worker) &&
            (!preferGraphics || !graphicsClaimable(false, worker)))
        {
            preferGraphics = true;
            const auto work = claimPresentation<Mode>(unsigned(worker));
            lock.unlock();
            executePresentation<Mode>(work, worker);
            lock.lock();
            return;
        }
        if (worker == presentationWorker) preferGraphics = false;
        auto work = graphicsBatch;
        std::size_t index;
        {
            std::lock_guard batchLock(work->mutex);
            // Ticket cancellation can race a scheduler opportunity.
            if (work->canceled.load() || work->next == work->count)
                return;
            index = work->next++;
            ++work->running;
            ++graphicsRunning;
            work->status.store(Presentation::Status::Running, std::memory_order_release);
        }
        const auto started = Mode == ComputeTelemetryMode::Trace ? diagnosticStart<Mode>() : 0;
        const auto serial = graphicsSerial<Mode>();
        if constexpr (Mode == ComputeTelemetryMode::Trace) {
            ++diagnostics->traceGraphicsInFlight;
            graphicsTotals.queueNs += started - diagnostics->graphicsSubmitted;
        }
        standaloneEvent<Mode>(ComputeTelemetry::EventKind::Claim, 3, serial, index, unsigned(worker));
        standaloneEvent<Mode>(ComputeTelemetry::EventKind::CallbackStart, 3, serial, index, unsigned(worker));
        lock.unlock();
        auto *previous = active;
        auto previousSlot = activeSlot;
        active = this;
        activeSlot = worker;
        std::exception_ptr failure;
        try
        {
            work->job({index, PresentationCancellationToken(&work->canceled)});
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        const auto callbackEnd = Mode == ComputeTelemetryMode::Trace ? diagnosticStart<Mode>() : 0;
        active = previous;
        activeSlot = previousSlot;
        {
            std::lock_guard batchLock(work->mutex);
            if (failure)
            {
                if (!work->error)
                    work->error = failure;
                work->canceled.store(true);
            }
        }
        lock.lock();
        --graphicsRunning;
        if constexpr (Mode == ComputeTelemetryMode::Trace) {
            ++graphicsTotals.jobs; graphicsTotals.activeNs += callbackEnd - started;
        }
        standaloneEvent<Mode>(ComputeTelemetry::EventKind::CallbackEnd, 3, serial, index, unsigned(worker), {JoinReason::Teardown, ComputeTelemetry::NoTick}, 0, callbackEnd);
        // Publish totals before completion, but release potentially expensive
        // captured storage outside the scheduler mutex.
        lock.unlock();
        bool final = false;
        {
            std::unique_lock batchLock(work->mutex);
            --work->running;
            work->settle(batchLock);
            if constexpr (Mode == ComputeTelemetryMode::Trace) final = work->finished.load();
        }
        lock.lock();
        standaloneEvent<Mode>(ComputeTelemetry::EventKind::Complete, 3, serial, index, unsigned(worker));
        if constexpr (Mode == ComputeTelemetryMode::Trace) --diagnostics->traceGraphicsInFlight;
        if (final) standaloneEvent<Mode>(ComputeTelemetry::EventKind::Retire, 3, serial, work->count, unsigned(worker));
		graphicsReady.notify_all();
		ready.notify_all();
	}

	template<ComputeTelemetryMode Mode>
	std::size_t invoke(std::size_t slot)
	{
		auto *previous = active;
		const auto previousSlot = activeSlot;
		active = this;
		activeSlot = slot;
		const auto started = Mode == ComputeTelemetryMode::Trace ? diagnosticStart<Mode>() : 0;
        std::size_t executed = 0;
		for (;;)
		{
			const auto i = next.fetch_add(1, std::memory_order_relaxed);
			if (i >= count) break;
			if constexpr (Mode != ComputeTelemetryMode::Off) ++executed;
            standaloneEvent<Mode>(ComputeTelemetry::EventKind::Claim, 1, runSerial<Mode>(), i, unsigned(slot));
            standaloneEvent<Mode>(ComputeTelemetry::EventKind::CallbackStart, 1, runSerial<Mode>(), i, unsigned(slot));
			try { job(i); }
			catch (...) { std::lock_guard<std::mutex> lock(mutex); if (!error) error = std::current_exception(); }
			{
				std::lock_guard<std::mutex> lock(mutex);
				standaloneEvent<Mode>(ComputeTelemetry::EventKind::CallbackEnd, 1, runSerial<Mode>(), i, unsigned(slot));
                standaloneEvent<Mode>(ComputeTelemetry::EventKind::Complete, 1, runSerial<Mode>(), i, unsigned(slot));
                if constexpr (Mode == ComputeTelemetryMode::Trace) ++workerMetrics[slot].jobs;
				if (++runDone == count) runFinished.notify_all();
			}
		}
		if constexpr (Mode == ComputeTelemetryMode::Trace) {
            std::lock_guard lock(mutex);
            workerMetrics[slot].activeNs += diagnosticElapsed<Mode>(started);
        }
		active = previous;
		activeSlot = previousSlot;
        return executed;
	}
	// Under mutex: the earliest due, then earliest submitted, live batch with a
	// claimable job. Only workers claim, unless there are none: then the owner
	// claims the jobs due no later than limit. claimable() has no side effect.
	static bool before(const Slot& a, const Slot& b) { return a.due != b.due ? a.due < b.due : a.serial < b.serial; }
	std::size_t group(const Slot& slot, std::size_t index) const
	{
		if (slot.groups.size() == 1) return 0;
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
		const auto consider = [&](std::size_t i) {
			const auto& slot = slots[i];
			if (owner && limit && before(*limit, slot)) return;
			if (best.valid && !before(slot, slots[best.slot])) return;
			const auto index = firstClaimable(slot, owner);
			if (index != slot.total) best = {i, index, true};
		};
		// A dense scan avoids a bit-scan per slot at sustained full occupancy.
		if (unclaimedBatches > Slots / 2)
		{
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
		for (std::size_t word = 0; word < unclaimedSlots.size(); ++word)
		{
			for (auto bits = unclaimedSlots[word]; bits; bits &= bits - 1)
			{
				const auto i = word * 64 + std::countr_zero(bits);
				consider(i);
			}
		}
		return best;
	}
	bool claimable(bool owner, const Slot* limit = nullptr) const { return pick(owner, limit).valid; }
	template<ComputeTelemetryMode Mode>
	Claim claim(bool owner, const Slot* limit = nullptr, unsigned thread = 0)
	{
		assert(owner == ownerRunsDeferred());
		const auto best = pick(owner, limit);
		if (!best.valid) return best;
		auto& slot = slots[best.slot];
		slot.claimed[best.index] = 1; --slot.unclaimed;
		if (!slot.unclaimed) markUnclaimed(best.slot, false);
		while (slot.firstUnclaimed < slot.total && slot.claimed[slot.firstUnclaimed]) ++slot.firstUnclaimed;
		event<Mode>(ComputeTelemetry::EventKind::Claim, best.slot, best.index, thread);
		return best;
	}
	template<ComputeTelemetryMode Mode>
	void freeSlot(Slot& slot)
	{
		assert(slot.unclaimed == 0);
		event<Mode>(ComputeTelemetry::EventKind::Retire, std::size_t(&slot - slots.data()));
		slot.serial = 0; slot.groups.clear(); slot.starts.clear(); slot.laneBases.clear(); slot.claimed.clear(); slot.completions.clear(); slot.continuations.clear();
		slot.total = slot.unclaimed = slot.completed = slot.firstUnclaimed = 0; slot.error = nullptr;
		--live;
	}
	// Under mutex: settle a synchronous or externally completed job.
	template<ComputeTelemetryMode Mode>
	void completeJob(Slot& slot, std::size_t index, std::exception_ptr failure, unsigned thread)
	{
		event<Mode>(ComputeTelemetry::EventKind::Complete, std::size_t(&slot - slots.data()), index, thread);
		if (failure && !slot.error) slot.error = failure;
		if (auto& ticket = slot.completions[index]) ticket->owner.store(nullptr, std::memory_order_release);
		slot.completions[index].reset();
		const auto lane = slot.groups[group(slot, index)].lane;
		if (lane != NoLane) {
			++laneCompleted[lane];
			// A returning worker claims the newly runnable successor itself.
			// External resolution has no returning worker, so wake one.
			if (thread == ~0u && !ownerRunsDeferred()) ready.notify_one();
		}
		if (++slot.completed == slot.total) slotDone.notify_all();
	}
	template<ComputeTelemetryMode Mode>
	void settle(Completion& ticket, unsigned thread)
	{
		auto& slot = slots[ticket.slot];
		if (ticket.continuation.invoke && !ticket.error) {
			slot.continuations[ticket.index] = ticket.continuation;
			slot.claimed[ticket.index] = 0;
			if (++slot.unclaimed == 1) markUnclaimed(ticket.slot, true);
			slot.firstUnclaimed = std::min(slot.firstUnclaimed, ticket.index);
			ticket.owner.store(nullptr, std::memory_order_release);
			slot.completions[ticket.index].reset();
			event<Mode>(ComputeTelemetry::EventKind::Runnable, ticket.slot, ticket.index, thread);
			if (thread == ~0u && !ownerRunsDeferred()) ready.notify_one();
		} else completeJob<Mode>(slot, ticket.index, ticket.error, thread);
	}
	template<ComputeTelemetryMode Mode>
	bool resolveImpl(Completion& ticket, Job continuation, std::exception_ptr failure)
	{
		std::lock_guard<std::mutex> lock(mutex);
		auto& slot = slots[ticket.slot];
		if (slot.serial != ticket.serial || slot.completions[ticket.index].get() != &ticket || ticket.resolved) return false;
		event<Mode>(ComputeTelemetry::EventKind::ExternalResolution, ticket.slot, ticket.index, ~0u);
		ticket.resolved = true; ticket.continuation = continuation; ticket.error = failure;
		if (!ticket.running) settle<Mode>(ticket, ~0u);
		return true;
	}
    bool resolve(Completion& ticket, Job continuation, std::exception_ptr failure) {
        if (telemetryModeValue == ComputeTelemetryMode::Trace) return resolveImpl<ComputeTelemetryMode::Trace>(ticket, continuation, failure);
        return resolveImpl<ComputeTelemetryMode::Off>(ticket, continuation, failure);
    }
	// Outside the mutex: run one claimed deferred job, then record completion.
	template<ComputeTelemetryMode Mode>
	void execute(const Claim& claim, std::size_t thread)
	{
		Group group; std::size_t offset = 0;
		std::uint64_t serial;
		{
			std::lock_guard<std::mutex> lock(mutex);
			auto& slot = slots[claim.slot];
			serial = slot.serial;
			const auto g = this->group(slot, claim.index);
			group = slot.groups[g]; offset = claim.index - slot.starts[g];
			if (slot.continuations[claim.index].invoke) { group.job = slot.continuations[claim.index]; offset = 0; }
			event<Mode>(ComputeTelemetry::EventKind::CallbackStart, claim.slot, claim.index, unsigned(thread));
			assert(group.lane == NoLane || laneCompleted[group.lane] == slot.laneBases[g] + (claim.index - slot.starts[g]));
		}
		auto *previous = active;
		const auto previousSlot = activeSlot;
		active = this; activeSlot = thread;
		const auto previousDeferredSlot = deferredSlot, previousDeferredIndex = deferredIndex;
		const auto previousDeferredSerial = deferredSerial;
		deferredSlot = claim.slot; deferredIndex = claim.index;
		deferredSerial = serial;
		const auto started = Mode == ComputeTelemetryMode::Trace ? diagnosticStart<Mode>() : 0;
		std::exception_ptr failure;
		try { group.job.invoke(group.job.context, offset); }
		catch (...) { failure = std::current_exception(); }
const auto callbackEnd = Mode == ComputeTelemetryMode::Trace ? diagnosticStart<Mode>() : 0;
        const auto elapsed = callbackEnd - started;
		active = previous; activeSlot = previousSlot;
		deferredSlot = previousDeferredSlot; deferredIndex = previousDeferredIndex; deferredSerial = previousDeferredSerial;
		{
			std::lock_guard<std::mutex> lock(mutex);
			auto& slot = slots[claim.slot];
			event<Mode>(ComputeTelemetry::EventKind::CallbackEnd, claim.slot, claim.index, unsigned(thread), {JoinReason::Teardown, ComputeTelemetry::NoTick}, 0, callbackEnd);
            if constexpr (Mode == ComputeTelemetryMode::Trace) { workerMetrics[thread].activeNs += elapsed; ++workerMetrics[thread].jobs; }
			if constexpr (Mode == ComputeTelemetryMode::Trace) { if (thread) ++totals.workerJobs; else ++totals.ownerJobs; }
			if (auto ticket = slot.completions[claim.index]) {
				ticket->running = false;
				if (failure) { ticket->resolved = true; ticket->error = failure; }
				if (ticket->resolved) settle<Mode>(*ticket, unsigned(thread));
			} else completeJob<Mode>(slot, claim.index, failure, unsigned(thread));
		}
	}
	template<ComputeTelemetryMode Mode>
	void worker(std::size_t slot)
	{
		std::size_t seen = 0, simulationClaims = 0;
		std::unique_lock<std::mutex> lock(mutex);
		for (;;)
		{
            ready.wait(lock,
                       [&]
                       {
                           return stopping || generation != seen || claimable(false) ||
                                  cpuPresentationClaimable(slot) || maintenanceClaimable(slot);
                       });
            if (stopping)
                return;
			// The designated worker interleaves presentation chunks with
			// simulation jobs, so neither starves the other: a join waits at
			// most one chunk for it, and other workers keep claiming meanwhile.
            if (slot == presentationWorker && simulationClaims >= 8 &&
                cpuPresentationClaimable(slot))
			{
				simulationClaims = 0;
                executeCpuPresentation<Mode>(lock, slot);
				continue;
			}
			if (generation != seen)
			{
				++simulationClaims;
				seen = generation;
				++inFlight;
				lock.unlock();
				invoke<Mode>(slot);
				lock.lock();
				if (--inFlight == 0) runFinished.notify_all();
				continue;
			}
			const auto claimed = claim<Mode>(false, nullptr, unsigned(slot));
			if (!claimed.valid)
			{
                if (!cpuPresentationClaimable(slot))
				{
                    if (!maintenanceClaimable(slot))
                        continue;
					const auto work = maintenance;
					maintenanceRunning = true;
                    const auto serial = [&] { if constexpr (Mode == ComputeTelemetryMode::Trace) return ++diagnostics->maintenanceSerial; else return std::uint64_t(0); }();
                    const auto epoch = [&] { if constexpr (Mode == ComputeTelemetryMode::Trace) return diagnostics->epoch; else return std::uint64_t(0); }();
                    standaloneEvent<Mode>(ComputeTelemetry::EventKind::Claim, 4, serial, 0, unsigned(slot));
                    standaloneEvent<Mode>(ComputeTelemetry::EventKind::CallbackStart, 4, serial, 0, unsigned(slot));
					lock.unlock();
                    active = this;
                    activeSlot = slot;
					work->process();
                    const auto callbackEnd = Mode == ComputeTelemetryMode::Trace ? diagnosticStart<Mode>() : 0;
                    active = nullptr;
                    activeSlot = 0;
					lock.lock();
					maintenanceRunning = false;
                    if constexpr (Mode == ComputeTelemetryMode::Trace) {
                        if (epoch == diagnostics->epoch) {
                            standaloneEvent<Mode>(ComputeTelemetry::EventKind::CallbackEnd, 4, serial, 0, unsigned(slot), {JoinReason::Teardown, ComputeTelemetry::NoTick}, 0, callbackEnd);
                            standaloneEvent<Mode>(ComputeTelemetry::EventKind::Complete, 4, serial, 0, unsigned(slot));
                            standaloneEvent<Mode>(ComputeTelemetry::EventKind::Retire, 4, serial, 0, unsigned(slot));
                        }
                    }
					continue;
				}
				simulationClaims = 0;
                executeCpuPresentation<Mode>(lock, slot);
				continue;
			}
			++simulationClaims;
			lock.unlock();
			execute<Mode>(claimed, slot);
			lock.lock();
		}
	}
	void stop(JoinReason reason = JoinReason::Teardown)
	{
        if (active)
            throw std::logic_error("Executor lifecycle inside a job");
        std::shared_ptr<GraphicsBatch> pendingGraphics;
        {
            std::lock_guard lock(mutex);
            pendingGraphics = graphicsBatch;
        }
        if (pendingGraphics)
        {
            pendingGraphics->cancel();
            pendingGraphics->drain();
        }
        {
            std::lock_guard lock(mutex);
            graphicsBatch.reset();
        }
		setWorkerOnly({});
		cancelPresentationAndWait();
		joinAll({reason, ComputeTelemetry::NoTick});
		{ std::lock_guard<std::mutex> lock(mutex); stopping = true; }
		ready.notify_all();
		for (auto &worker : workers) worker.join();
		workers.clear();
		stopping = false;
		generation = 0;
	}
    void noSuspensionEvent() noexcept {}
    void traceSuspensionEvent() noexcept { event<ComputeTelemetryMode::Trace>(ComputeTelemetry::EventKind::Suspend, deferredSlot, deferredIndex, unsigned(activeSlot)); }
    void (ComputeExecutor::*recordSuspension)() noexcept = &ComputeExecutor::noSuspensionEvent;
    std::size_t requestedTraceBytes = ComputeTelemetry::DefaultTraceBytes;
    DiagnosticClock requestedClock = realDiagnosticClock;
    void initializeDiagnostics(unsigned threads) noexcept {
        std::vector<WorkerMetrics>().swap(workerMetrics);
        diagnostics.reset(); telemetryModeValue = ComputeTelemetryMode::Off;
        recordSuspension = &ComputeExecutor::noSuspensionEvent;
        if (requestedTelemetryMode == ComputeTelemetryMode::Off) return;
        try { diagnostics = std::make_unique<DiagnosticsState>(); }
        catch (const std::bad_alloc&) { return; }
        diagnostics->clock = requestedClock;
        telemetryModeValue = ComputeTelemetryMode::Waits;
        if (requestedTelemetryMode != ComputeTelemetryMode::Trace) return;
        try {
            // Owner + workers + external completion stream, all fixed at configure.
            workerMetrics.resize(threads);
            diagnostics->streams.resize(std::size_t(threads) + 2);
            const auto capacity = requestedTraceBytes / sizeof(ComputeTelemetry::Event) / diagnostics->streams.size();
            for (auto& stream : diagnostics->streams) stream.storage.resize(capacity);
            diagnostics->traceBytes = capacity * sizeof(ComputeTelemetry::Event) * diagnostics->streams.size();
            telemetryModeValue = ComputeTelemetryMode::Trace;
            recordSuspension = &ComputeExecutor::traceSuspensionEvent;
        } catch (const std::bad_alloc&) { diagnostics->streams.clear(); workerMetrics.clear(); }
        catch (const std::length_error&) { diagnostics->streams.clear(); workerMetrics.clear(); }
    }
    static void validateContext(JoinContext context) {
        if (unsigned(context.reason) >= unsigned(JoinReason::Count)) throw std::invalid_argument("Invalid compute join reason");
    }
public:
	ComputeExecutor() = default;
	ComputeExecutor(const ComputeExecutor &) = delete;
	ComputeExecutor &operator=(const ComputeExecutor &) = delete;
	~ComputeExecutor() {
		stop();
		std::lock_guard lock(completionLifetime->mutex);
		completionLifetime->executor = nullptr;
	}
	// Configure only between batches; pending deferred work is joined first.
	// Thread creation failure retains a usable serial executor; caller reports
	// the actual thread count.
	void configure(unsigned threads,
		const std::function<std::thread(std::function<void()>)> &launch =
			[](std::function<void()> function) { return GAGCore::ThreadSupport::launch(std::move(function)); })
	{
        if (active)
            throw std::logic_error("Executor configuration inside a job");
        if (!threads)
            throw std::invalid_argument("Executor needs at least one thread");
		stop(JoinReason::Reconfigure);
        initializeDiagnostics(threads);
		presentationWorker = threads > 1 ? threads - 1 : 0;
		if constexpr (GAGCore::ThreadSupport::available)
		{
			try
			{
				workers.reserve(threads > 0 ? threads - 1 : 0);
				for (unsigned i = 1; i < threads; ++i)
					workers.push_back(launch([this, i] {
                        if (telemetryModeValue == ComputeTelemetryMode::Trace) worker<ComputeTelemetryMode::Trace>(i);
                        else worker<ComputeTelemetryMode::Off>(i);
                    }));
			}
            catch (...)
            {
                stop();
                presentationWorker = 0;
            }
		}
		else presentationWorker = 0;

		totals = {};
		presentationTotals = {};
        graphicsTotals = {};
    }
    PresentationBatchTicket
    submitPresentationBatch(std::size_t count, PresentationBatchOptions options,
                            std::function<void(PresentationJobContext)> function)
    {
        if (active)
            throw std::logic_error("Graphics batch submission inside a job");
        if (count && !function)
            throw std::invalid_argument("Graphics batch needs work");
        auto work = std::make_shared<GraphicsBatch>();
        work->owner = this;
        work->count = count;
        work->limit = std::clamp(options.maximumConcurrency, std::size_t(1), std::size_t(2));
        work->job = std::move(function);
#if defined(__EMSCRIPTEN__)
        work->serial = true;
#endif
        if (!count)
        {
            std::unique_lock batchLock(work->mutex);
            work->settle(batchLock);
            return PresentationBatchTicket(work);
        }
        {
            std::lock_guard lock(mutex);
            if (graphicsBatch && !graphicsBatch->finished.load())
                throw std::logic_error("Graphics batch already admitted");
            graphicsBatch = work;
            if (diagnostics) diagnostics->graphicsJoined = false;
            if (telemetryModeValue == ComputeTelemetryMode::Trace) {
                ++graphicsTotals.batches;
                graphicsTotals.parallelBatches += count > 1 && threadCount() > 2 && work->limit > 1 && !work->serial;
                diagnostics->graphicsSubmitted = diagnostics->clock(); ++diagnostics->graphicsSerial;
                standaloneEvent<ComputeTelemetryMode::Trace>(ComputeTelemetry::EventKind::Submit, 3, diagnostics->graphicsSerial, count, 0);
            }
        }
        ready.notify_all();
        return PresentationBatchTicket(work);
    }
private:
    template<ComputeTelemetryMode Mode>
    void finishPresentationBatchImpl(PresentationBatchTicket& ticket, JoinContext context) {
        if (active) throw std::logic_error("Graphics batch barrier inside a job");
        if (!ticket.work) return;
        const bool alreadyComplete = ticket.finished();
        if constexpr (Mode == ComputeTelemetryMode::Off) {
            if (alreadyComplete) { ticket.rethrowFailure(); return; }
        }
        if (!alreadyComplete && ticket.work->owner != this)
            throw std::logic_error("Graphics batch belongs to another executor");
        const auto start = diagnosticStart<Mode>();
        std::uint64_t ownerNs = 0, ownerLockNs = 0, ceilingWaitNs = 0;
        bool ownerExecuted = false, ceilingWaited = false;
        if (!alreadyComplete && (!presentationWorker || ticket.work->serial)) {
            const auto ownerStart = diagnosticStart<Mode>();
            std::unique_lock lock(mutex);
            if constexpr (Mode != ComputeTelemetryMode::Off) ownerLockNs = diagnosticElapsed<Mode>(ownerStart);
            while (!ticket.finished()) {
                bool firstInspection = true, blocked = false;
                const auto waitStart = diagnosticStart<Mode>();
                graphicsReady.wait(lock, [&] {
                    const bool runnable = ticket.finished() || graphicsClaimable(true);
                    if constexpr (Mode != ComputeTelemetryMode::Off) {
                        if (firstInspection) { blocked = !runnable; firstInspection = false; }
                    }
                    return runnable;
                });
                if constexpr (Mode != ComputeTelemetryMode::Off) {
                    if (blocked) { ceilingWaited = true; ceilingWaitNs += diagnosticElapsed<Mode>(waitStart); }
                }
                if (!ticket.finished()) {
                    const auto executionStart = diagnosticStart<Mode>();
                    if constexpr (Mode != ComputeTelemetryMode::Off) ownerExecuted = true;
                    executeCpuPresentation<Mode>(lock, 0, false);
                    if constexpr (Mode != ComputeTelemetryMode::Off) ownerNs += diagnosticElapsed<Mode>(executionStart);
                }
            }
        }
        const auto observation = ticket.work->template drainMeasured<Mode>([&] {
            if constexpr (Mode != ComputeTelemetryMode::Off) return diagnostics->clock; else return DiagnosticClock(nullptr);
        }());
        if constexpr (Mode != ComputeTelemetryMode::Off) {
            std::unique_lock lock(mutex);
            if constexpr (Mode == ComputeTelemetryMode::Trace) {
                if (graphicsBatch == ticket.work) graphicsReady.wait(lock, [&] { return diagnostics->traceGraphicsInFlight == 0; });
            }
            if (graphicsBatch == ticket.work && !diagnostics->graphicsJoined && ticket.work->count) {
                auto& value = diagnostics->summary.joins[unsigned(ComputeProducer::Presentation)][unsigned(context.reason)];
                diagnostics->graphicsJoined = true;
                ++value.joined; value.incomplete += ownerExecuted || observation.incomplete || ceilingWaited;
                value.lock.add(ownerLockNs+observation.lockNs);
                if (ownerExecuted) { ++value.ownerExecutions; value.owner.add(ownerNs); }
                if (observation.incomplete || ceilingWaited) {
                    ++value.backgroundAttempts; value.background.add(observation.waitNs + ceilingWaitNs);
                    if (ComputeTelemetry::required(context.reason) && context.logicalTick != ComputeTelemetry::NoTick)
                        diagnostics->waited(ComputeProducer::Presentation, context.logicalTick, observation.waitNs + ceilingWaitNs);
                }
                value.total.add(diagnosticElapsed<Mode>(start));
                graphicsTotals.waitNs += diagnosticElapsed<Mode>(start);
                standaloneEvent<Mode>(ComputeTelemetry::EventKind::JoinInspection, 3, graphicsSerial<Mode>(), ticket.work->count, 0,
                    context, observation.incomplete || ceilingWaited ? 1 : ownerExecuted ? 2 : 0, observation.inspected);
                standaloneEvent<Mode>(ComputeTelemetry::EventKind::JoinEnd, 3, graphicsSerial<Mode>(), ticket.work->count, 0, context);
            }
        }
        ticket.rethrowFailure();
    }
public:
    void finishPresentationBatch(PresentationBatchTicket& ticket,
        JoinContext context = {JoinReason::ImmediateRequired, ComputeTelemetry::NoTick}) {
        validateContext(context);
        switch (telemetryModeValue) {
            case ComputeTelemetryMode::Off: return finishPresentationBatchImpl<ComputeTelemetryMode::Off>(ticket, context);
            case ComputeTelemetryMode::Waits: return finishPresentationBatchImpl<ComputeTelemetryMode::Waits>(ticket, context);
            case ComputeTelemetryMode::Trace: return finishPresentationBatchImpl<ComputeTelemetryMode::Trace>(ticket, context);
        }
    }
    GraphicsMetrics graphicsMetrics() const
    {
        std::lock_guard lock(mutex);
        return graphicsTotals;
    }
    void executePresentationJobs(std::size_t count,
                                 const std::function<void(std::size_t)> &function) override
    {
        if (active)
            throw std::logic_error("Graphics batch submission inside a job");
        if (!count)
            return;
        const auto concurrency = GAGCore::renderConcurrency();
        auto ticket = submitPresentationBatch(count, {concurrency},
                                              [function](PresentationJobContext context)
                                              {
                                                  if (!context.cancellation.canceled())
                                                      function(context.index);
                                              });
        finishPresentationBatch(ticket);
        // Asynchronous callers may inspect a canceled ticket after draining.
        // Synchronous raster callers require every job before publishing pixels.
        if (ticket.status() == Presentation::Status::Canceled)
            throw std::runtime_error("Synchronous graphics batch was canceled");
	}
    // Drains using the old mode before selecting new specializations. Tags and
    // observations never enter scheduler state, save data or checksums.
    void configureTelemetry(ComputeTelemetryMode mode, std::size_t traceBytes = ComputeTelemetry::DefaultTraceBytes,
        DiagnosticClock clock = realDiagnosticClock) {
        if (unsigned(mode) > unsigned(ComputeTelemetryMode::Trace) || !clock) throw std::invalid_argument("Invalid compute telemetry configuration");
        const auto threads = unsigned(threadCount());
        auto service = maintenance;
        stop(JoinReason::Reconfigure);
        requestedTelemetryMode = mode; requestedTraceBytes = traceBytes; requestedClock = clock;
        configure(threads);
        setWorkerOnly(std::move(service));
    }
    ComputeTelemetryMode telemetryMode() const noexcept { return telemetryModeValue; }
    bool telemetryEnabled() const noexcept { return telemetryModeValue != ComputeTelemetryMode::Off; }
    void advancingTick(std::uint64_t tick) {
        if (!diagnostics) return;
        std::lock_guard lock(mutex);
        if (diagnostics->lastAdvanceTick != tick) { ++diagnostics->summary.advancingTicks; diagnostics->lastAdvanceTick = tick; }
    }
    // Reset only with required work drained. Presentation is canceled at the
    // session boundary, and streams remain allocated at their configured sizes.
    void resetTelemetry() {
        if (!diagnostics) return;
        std::lock_guard lock(mutex);
        if (live || inFlight || presentationRunning || graphicsRunning || (graphicsBatch && !graphicsBatch->finished.load()) || (diagnostics && (diagnostics->traceRunActive || diagnostics->traceGraphicsInFlight))) throw std::logic_error("Compute telemetry reset requires a drained executor");
        if (!diagnostics) return;
        diagnostics->summary = {}; diagnostics->batches = {};
        ++diagnostics->epoch; diagnostics->traceCoverageGap = maintenanceRunning;
        diagnostics->lastProducerTick.fill(ComputeTelemetry::NoTick);
        diagnostics->lastBackgroundTick = diagnostics->lastAdvanceTick = diagnostics->currentTick = ComputeTelemetry::NoTick;
        diagnostics->tickWaitNs = 0; diagnostics->tickWaited = false;
        diagnostics->runSerial = 0; diagnostics->runTags = {}; diagnostics->nextPresentation = 1;
        for (auto& stream : diagnostics->streams) { stream.used = 0; stream.lost = 0; }
        totals = {}; presentationTotals = {}; for (auto& value : workerMetrics) value = {};
    }
    ComputeTelemetry::Snapshot telemetrySnapshot(bool includeTrace = false) const {
        std::lock_guard lock(mutex);
        ComputeTelemetry::Snapshot result;
        result.requested = requestedTelemetryMode; result.mode = telemetryModeValue;
        result.available = bool(diagnostics); result.threads = unsigned(threadCount());
        if (!diagnostics) return result;
        result.summary = diagnostics->summary;
        if (diagnostics->tickWaited) result.summary.tickBackground.add(diagnostics->tickWaitNs);
        result.traceBytes = diagnostics->traceBytes;
        result.traceOpenWork = maintenanceRunning || bool(presentation) || bool(presentationPending);
        result.traceCoverageGap = diagnostics->traceCoverageGap;
        if (includeTrace && telemetryModeValue == ComputeTelemetryMode::Trace) {
            if (live || inFlight || runDone != count || presentationRunning || graphicsRunning || (graphicsBatch && !graphicsBatch->finished.load()) || diagnostics->traceRunActive || diagnostics->traceGraphicsInFlight)
                throw std::logic_error("Compute trace snapshot requires drained required work and idle presentation");
            result.streams = diagnostics->streams;
            result.traceIncluded = true;
        } else if (telemetryModeValue == ComputeTelemetryMode::Trace) {
            // Loss counters are written under this same mutex.
            result.streams.resize(diagnostics->streams.size());
            for (std::size_t i = 0; i < result.streams.size(); ++i) result.streams[i].lost = diagnostics->streams[i].lost;
        }
        return result;
    }
    void run(std::size_t n, const std::function<void(std::size_t)>& function,
        BatchDiagnostics tags = {}, JoinContext context = {JoinReason::ImmediateRequired, ComputeTelemetry::NoTick}) {
        if (!n) return;
        if (active) { for (std::size_t i=0;i<n;++i) function(i); return; }
        validateContext(context);
        if (unsigned(tags.producer) >= unsigned(ComputeProducer::Count)) throw std::invalid_argument("Invalid compute producer");
        if (telemetryModeValue == ComputeTelemetryMode::Off) return runImpl<ComputeTelemetryMode::Off>(n, function, tags, context);
        return runEnabled(n, function, tags, context);
    }
private:
    void runEnabled(std::size_t n, const std::function<void(std::size_t)>& function,
        BatchDiagnostics tags, JoinContext context) {
        if (telemetryModeValue == ComputeTelemetryMode::Waits) return runImpl<ComputeTelemetryMode::Waits>(n, function, tags, context);
        return runImpl<ComputeTelemetryMode::Trace>(n, function, tags, context);
    }
public:

    Batch submit(std::span<const Group> groups, std::uint64_t due = 0, BatchDiagnostics tags = {}) {
        if (groups.empty() && !active) return {};
        if (groups.size() == 1 && !groups.front().count && !active) {
            if (groups.front().lane != NoLane && groups.front().lane >= Lanes)
                throw std::invalid_argument("Compute lane index exceeds the lane count");
            return {};
        }
        if (unsigned(tags.producer) >= unsigned(ComputeProducer::Count)) throw std::invalid_argument("Invalid compute producer");
        if (telemetryModeValue == ComputeTelemetryMode::Off) return submitImpl<ComputeTelemetryMode::Off>(groups, due, tags);
        return submitEnabled(groups, due, tags);
        return {};
    }
private:
    Batch submitEnabled(std::span<const Group> groups, std::uint64_t due, BatchDiagnostics tags) {
        if (telemetryModeValue == ComputeTelemetryMode::Waits) return submitImpl<ComputeTelemetryMode::Waits>(groups, due, tags);
        return submitImpl<ComputeTelemetryMode::Trace>(groups, due, tags);
    }
public:

    bool pumpPresentation() {
        if (telemetryModeValue == ComputeTelemetryMode::Trace) return pumpPresentationImpl<ComputeTelemetryMode::Trace>();
        return pumpPresentationImpl<ComputeTelemetryMode::Off>();
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
            if (telemetryModeValue == ComputeTelemetryMode::Trace) {
                if (replaced) standaloneEvent<ComputeTelemetryMode::Trace>(ComputeTelemetry::EventKind::Retire, 2, diagnostics->pendingPresentation, 0, 0);
                diagnostics->pendingPresentation = diagnostics->nextPresentation++;
                standaloneEvent<ComputeTelemetryMode::Trace>(ComputeTelemetry::EventKind::Submit, 2, diagnostics->pendingPresentation, 0, 0);
            }
			if (telemetryEnabled()) ++presentationTotals.submitted;
			if (replaced && telemetryEnabled()) ++presentationTotals.replaced;
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
private:
	template<ComputeTelemetryMode Mode>
	bool pumpPresentationImpl()
	{
		if (active) throw std::logic_error("Presentation cannot be pumped from inside a job");
		PresentationTicket work;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (presentationWorker || !presentationClaimable(0)) return false;
			work = claimPresentation<Mode>(0);
		}
		executePresentation<Mode>(work, 0);
		return true;
	}
public:
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
            if (telemetryModeValue == ComputeTelemetryMode::Trace) {
                if (discarded) standaloneEvent<ComputeTelemetryMode::Trace>(ComputeTelemetry::EventKind::Retire, 2, diagnostics->currentPresentation, 0, 0);
                if (pending) standaloneEvent<ComputeTelemetryMode::Trace>(ComputeTelemetry::EventKind::Retire, 2, diagnostics->pendingPresentation, 0, 0);
                diagnostics->currentPresentation = diagnostics->pendingPresentation = 0;
            }
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
	static std::size_t workerSlot() { return active ? activeSlot : 0; }
	static std::size_t executionThreads() { return active ? active->threadCount() : 1; }
	// Configuration/lifecycle only. Replacing a service does not wait for a
	// running pass; the worker retains its old service until that pass ends.
	void setWorkerOnly(std::shared_ptr<WorkerOnly> service)
	{
		std::shared_ptr<WorkerOnly> old;
		{
			std::lock_guard<std::mutex> lock(mutex);
			old = std::exchange(maintenance, std::move(service));
		}
		ready.notify_all();
	}
	// A consistent copy; workers update the deferred counters under the mutex.
	Metrics metrics() const { std::lock_guard<std::mutex> lock(mutex); return totals; }
	// Sum of active elapsed times, NOT CPU time (the benchmark measures process CPU).
	std::uint64_t activeNs() const
	{
		std::lock_guard lock(mutex);
        std::uint64_t value = 0;
		for (const auto &worker : workerMetrics) value += worker.activeNs;
		return value;
	}
private:
    template<ComputeTelemetryMode Mode>
    void runImpl(std::size_t n, const std::function<void(std::size_t)>& function, BatchDiagnostics tags, JoinContext context) {
        if (!n) return;
        if (active) {
            for (std::size_t i = 0; i < n; ++i) function(i);
            return;
        }
        const auto start = diagnosticStart<Mode>();
        std::uint64_t ownerNs = 0, backgroundNs = 0, lockNs = 0;
        bool incomplete = false;
        std::size_t ownerJobs = 0;
        std::exception_ptr failure;
        // Under the existing completion lock (or enabled-only serial accounting
        // lock). Fork/join owners can both compute and wait in the same call.
        const auto record = [&] {
            if constexpr (Mode == ComputeTelemetryMode::Trace) diagnostics->traceRunActive = false;
            if constexpr (Mode != ComputeTelemetryMode::Off) {
                auto& value = diagnostics->summary.joins[unsigned(tags.producer)][unsigned(context.reason)];
                ++value.joined; value.incomplete += incomplete; value.lock.add(lockNs);
                if (ownerJobs) { ++value.ownerExecutions; value.owner.add(ownerNs); }
                if (incomplete) {
                    ++value.backgroundAttempts; value.background.add(backgroundNs);
                    if (ComputeTelemetry::required(context.reason) && context.logicalTick != ComputeTelemetry::NoTick)
                        diagnostics->waited(tags.producer, context.logicalTick, backgroundNs);
                }
                value.total.add(diagnosticElapsed<Mode>(start));
                ++totals.batches; totals.jobs += n; totals.batchNs += diagnosticElapsed<Mode>(start);
            }
        };
        if (n == 1 || workers.empty()) {
            if constexpr (Mode == ComputeTelemetryMode::Trace) {
                std::lock_guard lock(mutex);
                diagnostics->runTags = tags; ++diagnostics->runSerial; diagnostics->traceRunActive = true;
                standaloneEvent<Mode>(ComputeTelemetry::EventKind::Submit, 1, runSerial<Mode>(), n, 0);
            }
            const auto ownerStart = diagnosticStart<Mode>();
            active = this; activeSlot = 0;
            try {
                for (std::size_t i = 0; i < n; ++i) {
                    if constexpr (Mode != ComputeTelemetryMode::Off) ++ownerJobs;
                    if constexpr (Mode == ComputeTelemetryMode::Trace) {
                        std::lock_guard lock(mutex);
                        standaloneEvent<Mode>(ComputeTelemetry::EventKind::CallbackStart, 1, runSerial<Mode>(), i, 0);
                    }
                    try { function(i); }
                    catch (...) {
                        if constexpr (Mode == ComputeTelemetryMode::Trace) {
                            std::lock_guard lock(mutex);
                            standaloneEvent<Mode>(ComputeTelemetry::EventKind::CallbackEnd, 1, runSerial<Mode>(), i, 0);
                            standaloneEvent<Mode>(ComputeTelemetry::EventKind::Complete, 1, runSerial<Mode>(), i, 0);
                        }
                        throw;
                    }
                    if constexpr (Mode == ComputeTelemetryMode::Trace) {
                        std::lock_guard lock(mutex);
                        standaloneEvent<Mode>(ComputeTelemetry::EventKind::CallbackEnd, 1, runSerial<Mode>(), i, 0);
                        standaloneEvent<Mode>(ComputeTelemetry::EventKind::Complete, 1, runSerial<Mode>(), i, 0);
                    }
                }
            } catch (...) { failure = std::current_exception(); }
            active = nullptr;
            ownerNs = diagnosticElapsed<Mode>(ownerStart);
            if constexpr (Mode != ComputeTelemetryMode::Off) {
                std::lock_guard lock(mutex);
                if constexpr (Mode == ComputeTelemetryMode::Trace) {
                    workerMetrics[0].jobs += ownerJobs; workerMetrics[0].activeNs += ownerNs;
                    standaloneEvent<Mode>(ComputeTelemetry::EventKind::Retire, 1, runSerial<Mode>(), n, 0);
                }
                record();
            }
        } else {
            {
                std::unique_lock lock(mutex);
                runFinished.wait(lock, [&] { return inFlight == 0; });
                job = function; count = n; next = 0; runDone = 0; error = nullptr;
                ++generation;
                if constexpr (Mode == ComputeTelemetryMode::Trace) {
                    diagnostics->runTags = tags; ++diagnostics->runSerial; diagnostics->traceRunActive = true;
                    standaloneEvent<Mode>(ComputeTelemetry::EventKind::Submit, 1, runSerial<Mode>(), n, 0);
                }
            }
            ready.notify_all();
            const auto ownerStart = diagnosticStart<Mode>();
            ownerJobs = invoke<Mode>(0);
            ownerNs = diagnosticElapsed<Mode>(ownerStart);
            const auto lockStart = diagnosticStart<Mode>();
            std::unique_lock lock(mutex);
            const auto acquired = diagnosticStart<Mode>();
            lockNs = acquired-lockStart;
            incomplete = runDone != count || inFlight != 0;
            standaloneEvent<Mode>(ComputeTelemetry::EventKind::JoinInspection, 1, runSerial<Mode>(), n, 0);
            runFinished.wait(lock, [&] { return runDone == count && inFlight == 0; });
            backgroundNs = diagnosticElapsed<Mode>(acquired);
            if constexpr (Mode != ComputeTelemetryMode::Off) { ++totals.parallelBatches; totals.waitNs += backgroundNs; }
            standaloneEvent<Mode>(ComputeTelemetry::EventKind::JoinEnd, 1, runSerial<Mode>(), n, 0);
            standaloneEvent<Mode>(ComputeTelemetry::EventKind::Retire, 1, runSerial<Mode>(), n, 0);
            failure = error; job = {};
            record();
        }
        if (failure) std::rethrow_exception(failure);
    }
	// Queue a batch of groups, due at the given tick key; groups are copied.
	// Returns an empty batch when nothing was submitted. Never call from inside a job.
	template<ComputeTelemetryMode Mode>
	Batch submitImpl(std::span<const Group> groups, std::uint64_t due, BatchDiagnostics tags)
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
		std::size_t start = 0, runnable = 0;
		for (const auto& group : slot.groups)
		{
			slot.starts.push_back(start); start += group.count;
			slot.laneBases.push_back(group.lane == NoLane ? 0 : laneIssued[group.lane]);
			if (group.lane != NoLane) {
				// Only the head of an idle lane can run; queued successors do
				// not need a wakeup until their predecessor completes.
				if (group.count && laneCompleted[group.lane] == laneIssued[group.lane]) ++runnable;
				laneIssued[group.lane] += group.count; laneDue[group.lane] = due;
			} else runnable += group.count;
		}
		slot.total = slot.unclaimed = total; slot.completed = slot.firstUnclaimed = 0; slot.error = nullptr;
		slot.claimed.assign(total, 0);
		slot.completions.assign(total, {}); slot.continuations.assign(total, {});
		markUnclaimed(index, true);
		++live;
		if constexpr (Mode != ComputeTelemetryMode::Off) { ++totals.deferredBatches; totals.deferredJobs += total; }

        if constexpr (Mode != ComputeTelemetryMode::Off) diagnostics->batches[index] = {slot.serial, tags};
        event<Mode>(ComputeTelemetry::EventKind::Submit, index);
		batch.slot = index; batch.serial = slot.serial;
		lock.unlock();
		if (!ownerRunsDeferred()) {
			if (runnable == 1) ready.notify_one();
			else if (runnable) ready.notify_all();
		}
		return batch;
	}
public:
	// Suspend only the currently executing deferred callback. The caller must
	// arrange complete() or resume() on every path, including handoff failures.
	CompletionTicket defer()
	{
		if (active != this || !deferredSerial || !activeSlot)
			throw std::logic_error("Only a worker deferred job can suspend");
		std::lock_guard<std::mutex> lock(mutex);
		auto& slot = slots[deferredSlot];
		if (slot.serial != deferredSerial || slot.completions[deferredIndex])
			throw std::logic_error("Deferred callback already suspended");
		auto ticket = CompletionTicket(new Completion(this, deferredSlot, deferredIndex, deferredSerial));
		slot.completions[deferredIndex] = ticket;
        (this->*recordSuspension)();
		return ticket;
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
private:
    template<ComputeTelemetryMode Mode>
    void joinImpl(Batch& batch, JoinContext context) {
        if (batch.empty()) return;
        if (active) throw std::logic_error("Deferred batches cannot be joined from inside a job");
        const auto start = diagnosticStart<Mode>();
        std::unique_lock<std::mutex> lock(mutex);
        const auto acquired = diagnosticStart<Mode>();
        auto& slot = slots[batch.slot];
        if (slot.serial != batch.serial) { batch = {}; return; }
        const bool incomplete = slot.completed != slot.total;
        const bool owner = ownerRunsDeferred();
        event<Mode>(ComputeTelemetry::EventKind::JoinInspection, batch.slot, slot.total, 0, context, incomplete ? (owner ? 2 : 1) : 0);
        const auto workStart = diagnosticStart<Mode>();
        for (Claim claimed; owner && (claimed = claim<Mode>(true, &slot)).valid; lock.lock()) {
            lock.unlock(); execute<Mode>(claimed, 0);
        }
        if (slot.completed != slot.total) slotDone.wait(lock, [&] { return slot.completed == slot.total; });
        const auto completed = diagnosticStart<Mode>();
        const auto failure = slot.error;
        if constexpr (Mode != ComputeTelemetryMode::Off) {
            const auto producer = diagnostics->batches[batch.slot].tags.producer;
            auto& value = diagnostics->summary.joins[unsigned(producer)][unsigned(context.reason)];
            ++value.joined; value.incomplete += incomplete;
            value.lock.add(acquired - start);
            if (incomplete) {
                if (owner) { ++value.ownerExecutions; value.owner.add(completed - workStart); }
                else {
                    ++value.backgroundAttempts; value.background.add(completed - workStart);
                    totals.joinWaitNs += completed - workStart;
                    if (ComputeTelemetry::required(context.reason) && context.logicalTick != ComputeTelemetry::NoTick)
                        diagnostics->waited(producer, context.logicalTick, completed - workStart);
                }
            }
        }
        event<Mode>(ComputeTelemetry::EventKind::JoinEnd, batch.slot, slot.total, 0, context);
        const auto producer = [&] { if constexpr (Mode != ComputeTelemetryMode::Off) return diagnostics->batches[batch.slot].tags.producer; else return ComputeProducer::Other; }();
        freeSlot<Mode>(slot); batch = {};
        if constexpr (Mode != ComputeTelemetryMode::Off)
            diagnostics->summary.joins[unsigned(producer)][unsigned(context.reason)].total.add(diagnosticElapsed<Mode>(start));
        lock.unlock();
        if (failure) std::rethrow_exception(failure);
    }
public:
    void join(Batch& batch, JoinContext context = {JoinReason::ImmediateRequired, ComputeTelemetry::NoTick}) {
        if (batch.empty()) return;
        validateContext(context);
        if (telemetryModeValue == ComputeTelemetryMode::Off) return joinImpl<ComputeTelemetryMode::Off>(batch, context);
        return joinEnabled(batch, context);
    }
private:
    void joinEnabled(Batch& batch, JoinContext context) {
        if (telemetryModeValue == ComputeTelemetryMode::Waits) return joinImpl<ComputeTelemetryMode::Waits>(batch, context);
        return joinImpl<ComputeTelemetryMode::Trace>(batch, context);
    }
public:

	// Complete and free every live batch; errors of individual batches are
	// discarded here, since the batches' owners have given up their handles.
    void joinAll(JoinContext context = {JoinReason::Teardown, ComputeTelemetry::NoTick}) {
        if (active) return;
        // Owner admission is serialized, as for submit/join. Snapshot live identities
        // under the existing lock; join performs the authoritative inspection.
        std::array<Batch, Slots> batches;
        {
            std::lock_guard lock(mutex);
            for (std::size_t i = 0; i < Slots; ++i) { batches[i].slot = i; batches[i].serial = slots[i].serial; }
        }
        for (auto& batch : batches) { try { join(batch, context); } catch (...) {} }
    }
	std::size_t liveBatches() const { std::lock_guard<std::mutex> lock(mutex); return live; }
};
