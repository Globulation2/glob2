// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/GradientWorkspace.h"
#include "field/GradientDeviceService.h"
#include "common/ThreadCpuClock.h"
#include "map/TerrainType.h"
#include "map/TerrainRegistry.h"
#include "sim/snapshot/WorldSnapshot.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
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
    enum class CPUReason : unsigned {ExplicitCPU,OwnerExcluded,Unavailable,AutomaticPolicy,FailedSession,Trivial,Count};
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
		std::uint64_t preparationNs = 0, submittedNs = 0, executorDue = 0;
        std::shared_ptr<gradient_kernel::OwnedGradientField> deviceField;
        gradient_kernel::GradientSeedShape seedShape;
        bool captureSeedShape=false;
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
    using BatchWork = std::function<void(std::span<Job* const>, std::span<GradientWorkspace>)>;
    using AsyncWork = std::function<std::shared_ptr<gradient_kernel::OwnedGradientField>(Job&,gradient_kernel::PlanDecision)>;
	// Simulation-owner callback observes publication, never computation.
	std::function<void(std::uint16_t**)> onPublished;
	// Executor due key of a job published `remaining` advances from now. The
	// Map maps it onto simulation ticks; standalone use orders by this
	// pipeline's own ticks.
	std::function<std::uint64_t(unsigned remaining)> deadline;
	static constexpr unsigned MaxDelay = 16;
	static_assert(MaxDelay <= ComputeExecutor::GradientHorizon);
	struct Metrics {
        std::uint64_t jobs=0,published=0,discarded=0,waitNs=0,maxPending=0,preparationNs=0,publicationWaitNs=0;
        std::uint64_t gpuPublicationWaitCount=0,gpuPublicationWaitNs=0,gpuDeviceOverlapWaitNs=0;
        std::uint64_t ownerCompletionCpuNs=0,ownerJoinCpuNs=0,lastPublicationWaitNs=0,lastGpuPublicationWaitNs=0,lastGpuDeviceOverlapWaitNs=0;
    } metrics;
private:
	std::deque<std::unique_ptr<Job>> pending;
	std::vector<std::unique_ptr<Job>> spare;
	ComputeExecutor* executor = nullptr;
	bool shared = true;
	struct Workspace { GradientWorkspace propagation; gradient_preparation::CrowdingScratch crowding; };
	std::vector<Workspace> workspaces;
	std::shared_ptr<gradient_kernel::BackendSession> backendSession = std::make_shared<gradient_kernel::BackendSession>();
	unsigned delay = 0;
	std::uint64_t tick = 0, lastSubmission = 0;
	std::size_t cells = 0;
	Work work;
    BatchWork batchWork;
    AsyncWork asyncWork;
    std::shared_ptr<gradient_kernel::GradientDeviceService> deviceService;
	std::atomic<std::uint64_t> activeNs{0}, seedCpu{0}, propagationCpu{0}, cpuFields{0}, gpuFields{0}, selectedGpu{0}, requestedGpu{0};
    std::atomic<std::uint64_t> ownedInputCpu{0},handoffCpu{0},cleanupCpu{0};
    bool diagnostics=false;
    bool workerNoopBypass=false,crossDueTiming=false,cpuEnvelopeTiming=false,cpuEnvelopeOwnerRegistered=false;
    std::uint64_t cadenceStartedNs=0,cadenceOwnerCpuNs=0,cadenceWaitNs=0;
    std::array<std::atomic<std::uint64_t>,unsigned(CPUReason::Count)> cpuReasons{};
	using Clock = std::chrono::steady_clock;
	static std::uint64_t ns(Clock::time_point start) {
		return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
	}
	void execute(Job &job, Workspace &scratch) noexcept {
		const auto start = Clock::now();
		gradient_kernel::JobTiming timing{job.submittedNs, job.submittedNs ? gradient_kernel::monotonicNs() : 0,0};
		gradient_kernel::JobTimingScope timingScope(timing);
		job.crowding = &scratch.crowding;
		try
		{
			const auto preparationStart = Clock::now();
            const auto preparationCpu = glob2::threadCpuNs();
			try { if (job.seed) job.seed(job); }
			catch (...) { job.preparationNs = ns(preparationStart); throw; }
			job.preparationNs = ns(preparationStart);
            seedCpu.fetch_add(glob2::threadCpuNs()-preparationCpu,std::memory_order_relaxed);
            timing.preparation=job.preparationNs;
            const auto handoffStart=diagnostics ? glob2::threadCpuNs() : 0;
            const auto choice = gradient_kernel::backend();
            if(choice==gradient_kernel::Backend::OpenCL) requestedGpu.fetch_add(1,std::memory_order_relaxed);
            const auto family=gradient_preparation::backendFamily(job.request.kind);
            const bool learningEnabled=bool(backendSession->learningPolicy());
            gradient_kernel::WorkloadKey key;
            if(learningEnabled || choice==gradient_kernel::Backend::OpenCL) {
                key.width=job.snapshotLease ? job.snapshotLease->width : unsigned(cells);
                key.height=job.snapshotLease ? job.snapshotLease->height : 1;
                key.family=family; key.cpuBuckets=job.request.terrainBuckets;
                key.threads=unsigned(executor->threadCount()); key.limit=gradient_kernel::COST_LIMIT;
                key.movement=unsigned(job.request.swim);
                key.seedDensity=job.seedShape.seedDensity();key.blockerDensity=job.seedShape.blockerDensity();
                key.movementModifiers=job.snapshotLease && job.snapshotLease->terrain && job.snapshotLease->terrain->movementModifiers;
            }
            const auto decision = choice==gradient_kernel::Backend::CPU ? gradient_kernel::PlanDecision{}
                : asyncWork && deviceService ? backendSession->chooseWorkload(key,choice)
                : backendSession->choose(family,1,choice);
            const bool selectedGPU = decision.plan != gradient_kernel::Plan::CPU;
            if(selectedGPU) selectedGpu.fetch_add(1,std::memory_order_relaxed);
            if(selectedGPU && workerNoopBypass && gradient_kernel::alreadyFixedGradient(std::span(job.data.get(),cells))) {
                // Use the existing callback even for exact fixed seeds: it
                // retains movement/queue validation and the shared CPU shortcut.
                // Avoid constructing an accelerator DTO and waking the broker.
                const auto cpuStart=glob2::threadCpuNs();
                if(diagnostics)handoffCpu.fetch_add(glob2::threadCpuDeltaNs(handoffStart,cpuStart),std::memory_order_relaxed);
                work(job,scratch.propagation);
                propagationCpu.fetch_add(glob2::threadCpuDeltaNs(cpuStart,glob2::threadCpuNs()),std::memory_order_relaxed);
                cpuReasons[unsigned(CPUReason::Trivial)].fetch_add(1,std::memory_order_relaxed);
                cpuFields.fetch_add(1,std::memory_order_relaxed);
            } else if (selectedGPU && deviceService && asyncWork && executor->slot()) {
                const auto ownedStart=diagnostics ? glob2::threadCpuNs() : 0;
                job.deviceField=asyncWork(job,decision);
                job.deviceField->workload=key;
                job.deviceField->seedShape=job.seedShape;job.deviceField->publicationTick=job.due;
                job.deviceField->tick=job.snapshotLease ? job.snapshotLease->tick : job.due-delay;
                job.deviceField->seedCpuNs=glob2::threadCpuNs()-preparationCpu;
                const auto ownedEnd=diagnostics ? glob2::threadCpuNs() : 0;
                job.deviceField->completion=executor->defer();
                if(!deviceService->submit(job.deviceField)) {
                    // Admission failure still uses the original batch and worker.
                    deviceService->recoverOnWorker(job.deviceField);
                }
                if(diagnostics) {
                    ownedInputCpu.fetch_add(ownedEnd-ownedStart,std::memory_order_relaxed);
                    handoffCpu.fetch_add(ownedStart-handoffStart+glob2::threadCpuNs()-ownedEnd,std::memory_order_relaxed);
                }
            } else if (selectedGPU && batchWork && gradient_kernel::canBatch(*backendSession)) {
                if(diagnostics)handoffCpu.fetch_add(glob2::threadCpuNs()-handoffStart,std::memory_order_relaxed);
                const std::array jobs{&job};
                batchWork(jobs, std::span(&scratch.propagation, 1));
            } else {
                const auto reason=choice==gradient_kernel::Backend::CPU ? CPUReason::ExplicitCPU
                    : !executor->slot() ? CPUReason::OwnerExcluded
                    : backendSession->failed.load() ? CPUReason::FailedSession
                    : choice==gradient_kernel::Backend::Automatic ? CPUReason::AutomaticPolicy
                    : CPUReason::Unavailable;
                cpuReasons[unsigned(reason)].fetch_add(1,std::memory_order_relaxed);
                const auto cpuStart=glob2::threadCpuNs();
                if(diagnostics)handoffCpu.fetch_add(cpuStart-handoffStart,std::memory_order_relaxed);
                work(job,scratch.propagation);
                const auto consumed=glob2::threadCpuNs()-cpuStart;
                propagationCpu.fetch_add(consumed,std::memory_order_relaxed);
                if(learningEnabled && deviceService && executor->slot()) deviceService->recordAccepted(backendSession,key,
                    gradient_kernel::Plan::CPU,glob2::threadCpuNs()-preparationCpu,
                    job.snapshotLease ? job.snapshotLease->tick : job.due-delay);
                cpuFields.fetch_add(1,std::memory_order_relaxed);
            }
		}
		catch (...)
		{
			job.error = std::current_exception();
		}
		// Completion releases all borrowed immutable inputs, including failures.
        const auto cleanupStart=diagnostics ? glob2::threadCpuNs() : 0;
		job.water.reset(); job.terrain.reset(); job.registry.reset(); job.profiles.reset();
		job.snapshotLease.reset(); job.seed = {}; job.crowding = nullptr;
		activeNs.fetch_add(ns(start), std::memory_order_relaxed);
		job.done = true;
        if(diagnostics)cleanupCpu.fetch_add(glob2::threadCpuNs()-cleanupStart,std::memory_order_relaxed);
	}
	static void run(void* context, std::size_t) {
		auto& job = *static_cast<Job*>(context);
		auto& pipeline = *job.owner;
		pipeline.execute(job, pipeline.workspaces[pipeline.executor->slot()]);
	}
	void wait(Job &job, bool publication = false) {
		const auto start = Clock::now();
        const bool learningEnabled=bool(backendSession->learningPolicy());
        const bool trackStalls=diagnostics || learningEnabled;
        const auto waitStart=trackStalls ? gradient_kernel::monotonicNs() : 0;
        const auto joinCpuStart=diagnostics ? glob2::threadCpuNs() : 0;
        const bool incomplete=trackStalls && publication && executor && !executor->finished(job.batch);
		if (executor) executor->join(job.batch);
        const auto joined=diagnostics ? gradient_kernel::monotonicNs() : 0;
        const auto completionCpuStart=diagnostics ? glob2::threadCpuNs() : 0;
        // join may execute required work in serial mode: its CPU is inclusive,
        // not an exclusive overhead to add to propagation accounting.
        if(diagnostics)metrics.ownerJoinCpuNs+=glob2::threadCpuDeltaNs(joinCpuStart,completionCpuStart);
        if(job.deviceField) {
            // The ticket includes owned input release and service completion work.
            // Any incomplete GPU outcome can block its fixed publication, even
            // after the backend call has finished. Backend overlap is telemetry.
            if(incomplete && job.deviceField->executedGPU) {
                if(learningEnabled && deviceService) {
                    auto key=job.deviceField->workload;key.batch=job.deviceField->executedBatchCount;
                    deviceService->recordAccepted(job.deviceField->session,key,job.deviceField->decision.plan,
                        0,job.deviceField->tick,false,true);
                }
            }
            if(diagnostics && incomplete && job.deviceField->executedGPU) {
                const auto waited=joined-waitStart;
                const auto from=std::max(waitStart,job.deviceField->deviceStartedWallNs);
                const auto until=std::min(joined,job.deviceField->deviceCompletedWallNs);
                const auto overlap=until>from ? until-from : 0;
                ++metrics.gpuPublicationWaitCount;metrics.gpuPublicationWaitNs+=waited;
                metrics.gpuDeviceOverlapWaitNs+=overlap;metrics.lastGpuPublicationWaitNs+=waited;
                metrics.lastGpuDeviceOverlapWaitNs+=overlap;
            }
            job.data=job.deviceField->takeData();
            if(job.deviceField->error) job.error=job.deviceField->error;
            propagationCpu.fetch_add(job.deviceField->fallbackCpuNs,std::memory_order_relaxed);
            if(diagnostics)cleanupCpu.fetch_add(job.deviceField->fallbackCleanupCpuNs,std::memory_order_relaxed);
            (job.deviceField->executedGPU ? gpuFields : cpuFields).fetch_add(1,std::memory_order_relaxed);
            job.deviceField.reset();
        }
		metrics.preparationNs += std::exchange(job.preparationNs, 0);
        const auto waited=ns(start);
		metrics.waitNs += waited;
        if(publication) {metrics.publicationWaitNs += waited;metrics.lastPublicationWaitNs+=waited;}
        if(diagnostics)metrics.ownerCompletionCpuNs+=glob2::threadCpuDeltaNs(completionCpuStart,glob2::threadCpuNs());
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
        cadenceStartedNs=cadenceOwnerCpuNs=cadenceWaitNs=0;
	}
	void configure(ComputeExecutor& target, bool sharedExecution, unsigned ticks, std::size_t size, Work callback) {
		reset(); batchWork = {}; asyncWork = {}; metrics = {}; activeNs = 0; seedCpu=0; propagationCpu=0; cpuFields=0; gpuFields=0; selectedGpu=0; requestedGpu=0;
        for(auto& reason:cpuReasons) reason=0; cells = size; work = std::move(callback);
        diagnostics=gradient_kernel::gradientDiagnosticsRequested();
        cpuEnvelopeTiming=glob2::cpuEnvelopeRequested();cpuEnvelopeOwnerRegistered=false;ownedInputCpu=0;handoffCpu=0;cleanupCpu=0;
        workerNoopBypass=false;
        if(const auto* value=std::getenv("GLOB2_GRADIENT_WORKER_NOOP");value && *value) {
            if(std::strcmp(value,"0") && std::strcmp(value,"1"))
                throw std::invalid_argument("GLOB2_GRADIENT_WORKER_NOOP must be 0 or 1");
            workerNoopBypass=std::strcmp(value,"1")==0;
        }
		executor = &target; shared = sharedExecution;refreshDeviceConfiguration();
		resizeWorkspaces(); delay = ticks;
	}
    void setBatchWork(BatchWork callback) { finish(); batchWork=std::move(callback); }
    void setAsyncWork(AsyncWork callback) { finish(); asyncWork=std::move(callback); }
    void setDeviceService(std::shared_ptr<gradient_kernel::GradientDeviceService> service) {
        finish(); deviceService=std::move(service);
        refreshDeviceConfiguration();
    }
    void refreshDeviceConfiguration() noexcept {
        crossDueTiming=deviceService && deviceService->crossDueRequested();
        cadenceStartedNs=cadenceOwnerCpuNs=cadenceWaitNs=0;
    }
    std::shared_ptr<gradient_kernel::BackendSession> session() const { return backendSession; }
    std::uint64_t requiredSeedCpuNs() const { return seedCpu.load(); }
    std::uint64_t requiredPropagationCpuNs() const { return propagationCpu.load(); }
    bool diagnosticsEnabled() const {return diagnostics;}
    bool workerNoopEnabled() const {return workerNoopBypass;}
    std::uint64_t requiredOwnedInputCpuNs() const {return ownedInputCpu.load();}
    std::uint64_t requiredHandoffCpuNs() const {return handoffCpu.load();}
    std::uint64_t requiredCleanupCpuNs() const {return cleanupCpu.load();}
    std::uint64_t cpuCompleteFields() const { return cpuFields.load(); }
    std::uint64_t gpuRequestedFields() const { return requestedGpu.load(); }
    std::uint64_t cpuReason(CPUReason reason) const { return cpuReasons[unsigned(reason)].load(); }
    std::uint64_t gpuSelectedFields() const { return selectedGpu.load(); }
    std::uint64_t gpuCompleteFields() const { return gpuFields.load(); }
	// Share the game choice with all previous work drained.
    void setBackendSession(std::shared_ptr<gradient_kernel::BackendSession> session) {
        backendSession = std::move(session);
        for (auto& workspace : workspaces) workspace.propagation.backendSession = backendSession;
    }
	// Call after the executor is resized, with all previous work drained.
	void resizeWorkspaces() {
        workspaces.resize(executor ? executor->threadCount() : 1);
        // Bound optional seed caches across the entire pool, not per thread.
        for (auto& workspace : workspaces) {
            workspace.propagation.backendSession = backendSession;
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
        if(cpuEnvelopeTiming && !cpuEnvelopeOwnerRegistered)
            cpuEnvelopeOwnerRegistered=glob2::registerCpuEnvelopeThread(glob2::CpuThreadRole::Owner);
        if(crossDueTiming && executor) {
            const auto started=gradient_kernel::monotonicNs(),ownerCpu=glob2::threadCpuNs();
            const auto totals=executor->metrics();const auto waited=totals.waitNs+totals.joinWaitNs;
            const auto elapsed=started>=cadenceStartedNs ? started-cadenceStartedNs : 0;
            const auto blocked=waited>=cadenceWaitNs ? waited-cadenceWaitNs : elapsed;
            const auto cpu=glob2::threadCpuDeltaNs(cadenceOwnerCpuNs,ownerCpu);
            const auto nonWait=cadenceStartedNs && elapsed>blocked ? std::min(elapsed-blocked,cpu) : 0;
            deviceService->recordCadence({tick+1,started,nonWait});
            cadenceStartedNs=started;cadenceOwnerCpuNs=ownerCpu;cadenceWaitNs=waited;
        }
		++tick;
        metrics.lastPublicationWaitNs=metrics.lastGpuPublicationWaitNs=metrics.lastGpuDeviceOverlapWaitNs=0;
		while (!pending.empty() && pending.front()->due <= tick) {
			auto &job = *pending.front(); wait(job,true);
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
		job->deviceField.reset();
        job->seedShape={};job->captureSeedShape=crossDueTiming;
        job->done=false; job->superseded=false; job->error=nullptr;  job->owner=this; job->preparationNs=0;
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
            ptr->submittedNs=backendSession->accountingEnabled() ? gradient_kernel::monotonicNs() : 0;
			// Owner-only execution computes now; publication keeps its deadline.
			if (!shared) { execute(*ptr, workspaces[0]); return; }
			const ComputeExecutor::Group group{1, {&run, ptr}, ComputeExecutor::NoLane};
			const unsigned remaining = unsigned(ptr->due - tick);
            ptr->executorDue=deadline ? deadline(remaining) : ComputeExecutor::advanceDue(ptr->due);
			ptr->batch = executor->submit(std::span(&group, 1),ptr->executorDue);
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
