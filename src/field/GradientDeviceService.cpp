// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientDeviceService.h"
#include "OpenCLGradient.h"
#include "GradientBatchManifest.h"
#include "CaptureProbeReplay.h"
#include "CaptureProbeAccounting.h"
#include "common/ThreadCpuClock.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <fstream>
#include <cstdio>

namespace gradient_kernel
{
bool gradientDiagnosticsRequested() noexcept {
    const auto* value=std::getenv("GLOB2_GRADIENT_DIAGNOSTICS");return value && std::strcmp(value,"1")==0;
}
struct GradientBatchAdmissionState
{
    // First member releases last, after owned path/profile members die.
    struct Lease {std::size_t bytes=0;~Lease(){if(bytes)releaseOpenCLHostBytes(bytes);}} lease;
    std::string path;
    GradientBatchManifest manifest;
    std::array<GradientCadenceSample,128> cadenceQueue{};
    std::size_t cadenceRead=0,cadenceWritten=0;
    GradientCadenceFloor cadence;
};
// Fixed storage makes queue scans and owner-side admissions bounded and avoids
// allocator nodes escaping the accelerator-owned host reservation.
template<class T,std::size_t N> class BoundedEntries {
    std::array<T,N> entries{};std::size_t count=0;
public:
    auto begin() noexcept {return entries.begin();} auto end() noexcept {return entries.begin()+count;}
    auto begin() const noexcept {return entries.begin();} auto end() const noexcept {return entries.begin()+count;}
    bool empty() const noexcept {return !count;} std::size_t size() const noexcept {return count;}
    bool full() const noexcept {return count==N;}
    T& operator[](std::size_t index) noexcept {return entries[index];}
    void push_back(T value) noexcept {entries[count++]=std::move(value);}
    auto erase(typename std::array<T,N>::iterator at) noexcept {
        auto next=at;for(;next+1!=end();++next)*next=std::move(*(next+1));
        entries[--count]=T{};return at;
    }
};
struct GradientDeviceState
{
    struct StorageLease {std::size_t bytes=0;~StorageLease(){releaseOpenCLHostBytes(bytes);}} storageLease;
    struct Queued {std::shared_ptr<OwnedGradientField> field;std::uint64_t serial;};
    struct Observation {
        std::weak_ptr<BackendSession> session;
        WorkloadKey key;
        Plan plan=Plan::CPU;
        std::uint64_t generation=0,cpuNs=0,tick=0;
        bool failed=false,publicationStall=false,batchOfferFallback=false;
        std::uint64_t offerVersion=0;
    };
    mutable std::mutex mutex;
    GradientDeviceService::Hooks hooks;
    // Kept immutable through fallback continuations; canceled registrations clear
    // backend hooks independently while required worker recovery can still run.
    std::function<std::uint64_t()> cpuClock;
    GradientDeviceService::Metrics totals;
    BoundedEntries<Queued,64> queue;
    std::array<std::shared_ptr<OpenCLRequiredControl>,8> activeControls{};
    std::array<Observation,64> observations;
    struct Capture {std::shared_ptr<GradientCpuCapture::Record> record;std::weak_ptr<BackendSession> session;};
    std::optional<Capture> capture;
    std::optional<Capture> activeCapture; // Coordinator-only after metadata handoff.
    std::atomic<bool> optionalBusy{false},learningAvailable{false};
    bool nativeBackend=true; // Immutable before registration publication; fake hooks only in tests.
    std::unique_ptr<CaptureProbeReplay> probe;
    std::optional<GradientCpuCapture::Settlement> settlement;
    std::unique_ptr<CaptureProbeAccounting> probeAccounting;
    std::weak_ptr<BackendSession> probeSession;
    std::weak_ptr<CpuSavingPolicy> probePolicy;
    CaptureProbeReplay::Receipt probeResult;
    std::uint64_t probeGeneration=0,probeDue=0,probeSlack=0,probeAdmissionNs=0,probeReservedCpuNs=0;
    WorkloadKey probeKey;
    Plan probePlan=Plan::CPU;
    std::uint64_t pendingProbeSchedulerCpuNs=0;
    std::uint64_t optionalReadyNs=0;
    std::size_t observationRead=0,observationWritten=0;
    std::uint64_t nextSerial=0;
    unsigned maximumBatch=8,computeThreads=1;
    Backend mode=Backend::CPU;
    std::shared_ptr<GradientBatchAdmissionState> batching;
    bool started=false,initialized=false,canceled=false,automaticClockUnavailable=false,startupCharged=false;
    std::atomic<std::size_t> retained{0};
};
namespace
{
std::uint64_t serviceCpuNow(const GradientDeviceState* state) noexcept {
    try{return state && state->cpuClock ? state->cpuClock() : glob2::threadCpuNs();}catch(...){return 0;}
}
bool validCpuInterval(std::uint64_t start,std::uint64_t end) noexcept {return start && end>=start;}
struct EnvelopeMemoryLease
{
    std::size_t bytes=0;bool probe=false;
    EnvelopeMemoryLease()=default;
    EnvelopeMemoryLease(std::size_t size,bool optional):probe(optional) {
        if(optional ? reserveOpenCLProbeBytes(size) : reserveOpenCLHostBytes(size))bytes=size;
    }
    EnvelopeMemoryLease(EnvelopeMemoryLease&& other) noexcept:bytes(std::exchange(other.bytes,0)),probe(other.probe){}
    EnvelopeMemoryLease& operator=(EnvelopeMemoryLease&& other) noexcept {
        if(this!=&other){if(bytes){if(probe)releaseOpenCLProbeBytes(bytes);else releaseOpenCLHostBytes(bytes);}
            bytes=std::exchange(other.bytes,0);probe=other.probe;}return *this;
    }
    EnvelopeMemoryLease(const EnvelopeMemoryLease&)=delete;
    ~EnvelopeMemoryLease(){if(bytes){if(probe)releaseOpenCLProbeBytes(bytes);else releaseOpenCLHostBytes(bytes);}}
};
struct EnvelopeRegistryStorage {
    EnvelopeMemoryLease memory;glob2::CpuClockRegistry registry;
    explicit EnvelopeRegistryStorage(EnvelopeMemoryLease&& lease):memory(std::move(lease)){}
};
std::atomic<std::shared_ptr<glob2::CpuClockRegistry>> envelopeRegistry;
std::mutex envelopeRegistryInitialization;
std::atomic<std::uint64_t> envelopeRegistrySetupCpuNs{0},envelopeRoleRegistrationCpuNs{0};
std::shared_ptr<glob2::CpuClockRegistry> lookupEnvelopeRegistry() noexcept {return envelopeRegistry.load(std::memory_order_acquire);}
bool registerEnvelopeRole(glob2::CpuThreadRole role,bool initialize) noexcept {
    if(unsigned(role)>=4)return true;
    auto registry=lookupEnvelopeRegistry();
    if(!registry && initialize) {
        std::lock_guard lock(envelopeRegistryInitialization);registry=lookupEnvelopeRegistry();
        if(!registry) {
            const auto started=glob2::threadCpuNs();
            try {
                EnvelopeMemoryLease memory(sizeof(EnvelopeRegistryStorage)+128,false);
                if(memory.bytes) {
                    auto storage=std::make_shared<EnvelopeRegistryStorage>(std::move(memory));
                    registry=std::shared_ptr<glob2::CpuClockRegistry>(storage,&storage->registry);
                    envelopeRegistry.store(registry,std::memory_order_release);
                }
            } catch(...) {}
            envelopeRegistrySetupCpuNs.fetch_add(glob2::threadCpuDeltaNs(started,glob2::threadCpuNs()),std::memory_order_relaxed);
        }
    }
    if(!registry)return false; // Owner never initializes or waits for initialization.
    struct ThreadRegistration {
        // First member releases last, after clock leases retire and registry ownership dies.
        EnvelopeMemoryLease memory;
        std::shared_ptr<glob2::CpuClockRegistry> registry;
        std::array<glob2::CpuClockRegistry::Lease,4> roles;
        unsigned attempted=0;
    };
    thread_local ThreadRegistration current;
    const auto bit=1u<<unsigned(role);
    if(current.attempted&bit)return true;
    const auto started=glob2::threadCpuNs();
    if(!current.memory.bytes)current.memory=EnvelopeMemoryLease(sizeof(ThreadRegistration),false);
    const bool admitted=current.memory.bytes!=0;
    if(admitted){current.registry=registry;current.roles[unsigned(role)]=registry->registerCurrent(role);current.attempted|=bit;}
    envelopeRoleRegistrationCpuNs.fetch_add(glob2::threadCpuDeltaNs(started,glob2::threadCpuNs()),std::memory_order_relaxed);
    return admitted;
}
const bool installedEnvelopeBridge=[] {
    glob2::cpuEnvelopeRoleRegistrar=&registerEnvelopeRole;glob2::cpuEnvelopeRegistryLookup=&lookupEnvelopeRegistry;return true;
}();
struct BatchCpuEnvelope {
    std::shared_ptr<glob2::CpuClockRegistry> registry;
    EnvelopeMemoryLease memory;
    glob2::ProcessCpuEnvelope envelope;
    BatchCpuEnvelope(std::shared_ptr<glob2::CpuClockRegistry> clocks,EnvelopeMemoryLease&& lease)
        :registry(std::move(clocks)),memory(std::move(lease)),envelope(*registry) {}
};
std::unique_ptr<BatchCpuEnvelope> beginBatchCpuEnvelope() noexcept {
    auto registry=lookupEnvelopeRegistry();if(!registry)return {};
    try {
        EnvelopeMemoryLease memory(sizeof(BatchCpuEnvelope)+128,true);if(!memory.bytes)return {};
        auto result=std::make_unique<BatchCpuEnvelope>(std::move(registry),std::move(memory));
        for(unsigned advance=0;advance<4;++advance)result->envelope.advance();return result;
    } catch(...) {return {};}
}
void finishBatchCpuEnvelope(glob2::ProcessCpuEnvelope& envelope) noexcept {
    if(envelope.finish())for(unsigned advance=0;advance<4;++advance)envelope.advance();
}
std::atomic<std::uint64_t> brokerCpuNs{0};
std::atomic<unsigned> brokerThreads{0};
std::atomic<std::uint64_t> brokerThreadId{0};
void recoverField(void* context,std::size_t) noexcept
{
    auto& field=*static_cast<OwnedGradientField*>(context);
    const auto cpuStart=serviceCpuNow(field.executionState.get());
    try {field.cpu(field);} catch(...) {field.error=std::current_exception();}
    const auto cpuEnd=serviceCpuNow(field.executionState.get());
    field.fallbackCpuNs=glob2::threadCpuDeltaNs(cpuStart,cpuEnd);
    const bool batchOffer=field.minimumBatchCount>1 && field.executedBatchCount<field.minimumBatchCount &&
        (field.fallbackReason==GradientFallbackReason::BatchNotReady ||
         field.fallbackReason==GradientFallbackReason::MemoryBudget || field.fallbackReason==GradientFallbackReason::Unavailable);
    const bool measureCleanup=field.diagnostics || batchOffer;
    const auto cleanupStart=measureCleanup ? serviceCpuNow(field.executionState.get()) : 0;
    field.inputs.reset();field.identity={};
    const auto cleanupEnd=measureCleanup ? serviceCpuNow(field.executionState.get()) : 0;
    if(measureCleanup)field.fallbackCleanupCpuNs=glob2::threadCpuDeltaNs(cleanupStart,cleanupEnd);
    bool measured=field.preparationCpuClockValid && field.hostCpuClockValid && validCpuInterval(cpuStart,cpuEnd);
    if(batchOffer)measured=measured && validCpuInterval(cleanupStart,cleanupEnd) && cleanupStart>=cpuEnd;
    auto cpu=field.seedCpuNs;
    for(const auto part:{field.hostCpuNs,field.fallbackCpuNs}){if(part>UINT64_MAX-cpu)measured=false;else cpu+=part;}
    if(batchOffer){if(field.fallbackCleanupCpuNs>UINT64_MAX-cpu)measured=false;else cpu+=field.fallbackCleanupCpuNs;}
    if(!measured && field.executionState){std::lock_guard lock(field.executionState->mutex);++field.executionState->totals.clockInvalidMeasurements;}
    if(auto service=field.observer.lock()) {
        auto key=field.workload;
        if(batchOffer) {
            key.batch=field.minimumBatchCount;
            service->recordBatchOfferFallback(field.session,key,field.decision.plan,field.decision.version,cpu,field.tick,measured && !field.error);
        }else {
            key.batch=std::max(field.executedBatchCount,field.minimumBatchCount);
            service->recordAccepted(field.session,key,field.decision.plan,measured ? cpu : 0,field.tick,true);
        }
    }
}
void fallbackField(const std::shared_ptr<OwnedGradientField>& field) noexcept
{
    if(auto state=field->executionState) {
        std::lock_guard lock(state->mutex);++state->totals.fallbacks;
        ++state->totals.fallbackReasons[unsigned(field->fallbackReason)];
    }
    field->completion->resume({&recoverField,field.get()});
}
bool observe(const std::shared_ptr<GradientDeviceState>& state,std::shared_ptr<BackendSession> session,
    const WorkloadKey& key,Plan plan,std::uint64_t cpuNs,std::uint64_t tick,bool failure=false,bool publicationStall=false,bool batchOfferFallback=false,std::uint64_t offerVersion=0) noexcept
{
    std::lock_guard lock(state->mutex);
    if(!state->initialized || !state->started || state->canceled || !session || !session->learningPolicy()) return false;
    if(state->observationWritten-state->observationRead==state->observations.size()) {
        ++state->totals.observationDrops;
        if(!publicationStall && !failure)return false;
        ++state->observationRead; // Retain failure/stall demotion over an older optional sample.
    }
    if(publicationStall)++state->totals.publicationStalls;
    state->observations[state->observationWritten++%state->observations.size()]={
        session,key,plan,session->currentGeneration(),cpuNs,tick,failure,publicationStall,batchOfferFallback,offerVersion};return true;
}

// The broker is process-owned. Only process exit joins its thread. Holding the
// backend lease until AFTER that join pins Device/API/library even if their
// static root is destroyed first. No callback borrows a service, Map or pipeline.
class DeviceBroker
{
    EnvelopeMemoryLease storageLease{sizeof(DeviceBroker)+128,false};
    std::shared_ptr<const void> backendLifetime=retainOpenCLLifetime();
    std::mutex mutex;
    std::condition_variable wake;
    BoundedEntries<std::shared_ptr<GradientDeviceState>,64> registrations;
    std::thread coordinator;
    std::atomic<bool> stopping{false};bool realInitialized=false,realReady=false;
    bool pending() const {
        for(const auto& state:registrations) {
            std::lock_guard lock(state->mutex);
            if((state->canceled && !state->optionalBusy.load()) || (!state->started && !state->optionalBusy.load()) || !state->initialized || !state->queue.empty() ||
               state->observationRead!=state->observationWritten || (state->batching && state->batching->cadenceRead!=state->batching->cadenceWritten) ||
               (state->optionalBusy.load() && monotonicNs()>=state->optionalReadyNs)) return true;
        }
        return false;
    }
    std::chrono::nanoseconds optionalWait() const noexcept {
        auto next=UINT64_MAX;const auto now=monotonicNs();
        for(const auto& state:registrations) {
            std::lock_guard lock(state->mutex);
            if(state->optionalBusy.load())next=std::min(next,state->optionalReadyNs);
        }
        if(next==UINT64_MAX)return std::chrono::nanoseconds::max();
        return std::chrono::nanoseconds(next>now?std::min(next-now,std::uint64_t(250000000)):0);
    }
    static bool knownBatchShape(const OwnedGradientField& field,unsigned threads) noexcept {
        const auto& key=field.workload;const auto& shape=field.seedShape;
        return shape.known && shape.cells==field.grid.cells() && shape.sources<=shape.cells && shape.blockers<=shape.cells-shape.sources &&
            key.width==unsigned(field.grid.width()) && key.height==unsigned(field.grid.height()) && key.threads==threads &&
            key.cpuBuckets==field.cpuBuckets && field.limit>=0 && key.limit==unsigned(field.limit) && key.family==field.family &&
            (key.family==Family::Clear || key.family==Family::Guard) && key.seedDensity==shape.seedDensity() &&
            key.blockerDensity==shape.blockerDensity() && field.identity.owner && field.identity.allCells;
    }
    static void cadenceLocked(GradientDeviceState& state) noexcept {
        if(!state.batching)return;
        while(state.batching->cadenceRead!=state.batching->cadenceWritten)
            state.batching->cadence.record(state.batching->cadenceQueue[state.batching->cadenceRead++%state.batching->cadenceQueue.size()]);
        state.totals.cadenceFloorNs=state.batching->cadence.floorNs();state.totals.cadenceRevision=state.batching->cadence.version();
    }
    static void loadManifest(GradientDeviceState& state) noexcept {
        if(!state.totals.crossDueRequested || !state.batching)return;
        constexpr std::size_t transient=8*1024*1024;
        const auto decline=[&]{std::lock_guard lock(state.mutex);++state.totals.batchProfileDeclines;};
        // File reads, hashing and parsing belong exclusively to the broker.
        if(!reserveOpenCLHostBytes(transient)){decline();return;}
        struct Release {std::size_t bytes;~Release(){releaseOpenCLHostBytes(bytes);}} release{transient};
        try {
            std::ifstream file(state.batching->path,std::ios::binary);
            if(!file)throw std::invalid_argument("batch manifest unavailable");
            std::array<char,GradientBatchManifest::MaxBytes+1> buffer;
            file.read(buffer.data(),std::streamsize(buffer.size()));const auto count=file.gcount();
            if(!file.eof() || count<=0 || std::size_t(count)>GradientBatchManifest::MaxBytes)
                throw std::invalid_argument("batch manifest size");
            const auto backend=state.hooks.status ? state.hooks.status() : openCLStatus();
            const auto manifest=GradientBatchManifest::parse(std::string_view(buffer.data(),std::size_t(count)),backend,
                backend.parityBound,gradientNativeBuildIdentity());
            std::lock_guard lock(state.mutex);state.batching->manifest=manifest;
            state.totals.batchProfiles=manifest.count;state.totals.batchManifestHash=manifest.hash;
            state.totals.batchSourceHash=manifest.sourceHash;state.totals.crossDueReady=true;
        } catch(...) {
            decline();
        }
    }
    void initialize(const std::shared_ptr<GradientDeviceState>& state) noexcept {
        const auto start=monotonicNs();bool ready=false;
        const auto startupClock=glob2::CpuClockReader::native();
        const auto startupBegin=state->totals.automaticTuning && startupClock.process?startupClock.process(startupClock.context):glob2::CpuClockSample{};
        if(state->totals.cpuEnvelopeRequested)glob2::registerCpuEnvelopeThread(glob2::CpuThreadRole::Coordinator,true);
        try {
            if(state->hooks.initialize) ready=state->hooks.initialize();
            else {
                if(!realInitialized) {realReady=initializeOpenCL();realInitialized=true;}
                ready=realReady && readyPlans.load(std::memory_order_acquire)!=0;
            }
        } catch(...) {}
        if(ready)loadManifest(*state);
        const auto startupEnd=state->totals.automaticTuning && startupClock.process?startupClock.process(startupClock.context):glob2::CpuClockSample{};
        std::lock_guard lock(state->mutex);state->initialized=true;state->started=ready && !state->canceled;
        state->totals.initializationNs=monotonicNs()-start;
        // Cold compiler/driver work has no accepted reference yet. Charge the
        // enclosing whole-process CPU upper bound as startup debt, separately
        // from sustained paired observations. Concurrent work is overcharged.
        state->totals.tuningStartupClockValid=startupBegin.valid && startupEnd.valid && startupBegin.ns && startupEnd.ns && startupEnd.ns>=startupBegin.ns;
        if(state->totals.automaticTuning)state->totals.tuningStartupUpperCpuNs=state->totals.tuningStartupClockValid?
            startupEnd.ns-startupBegin.ns:UINT64_MAX;
        state->learningAvailable.store(state->totals.automaticTuning && state->started &&
            state->totals.tuningStartupClockValid && !state->automaticClockUnavailable,std::memory_order_release);
    }
    void advanceProbe(const std::shared_ptr<GradientDeviceState>& state) noexcept {
        const auto stepStart=glob2::threadCpuNs();
        CaptureProbeReplay::Admission admission;
        {
            std::lock_guard lock(state->mutex);
            if(state->capture && !state->activeCapture) {
                state->activeCapture.emplace(std::move(*state->capture));state->capture.reset();
            }
        }
        if(state->activeCapture && !state->probeAccounting) {
            auto& capture=*state->activeCapture;
            state->probeGeneration=capture.record->requiredGeneration();
            state->probeDue=capture.record->due();state->probeSession=capture.session;
            state->probeKey=capture.record->key();state->probeReservedCpuNs=capture.record->reservedCpuNs();
            state->probePlan=Plan::CPU;
            state->probeAdmissionNs=monotonicNs();
            CaptureProbeAccounting::Receipt attempted;
            // This protocol is experimental while tuning is opt-in. The
            // release default is changed only after tuning-on/off evidence.
            state->probeAccounting=CaptureProbeAccounting::begin(lookupEnvelopeRegistry(),
                capture.record->receipt().optionalCpuNs+state->pendingProbeSchedulerCpuNs,
                state->probeGeneration,capture.record->probeTicket(),true,attempted);
            state->pendingProbeSchedulerCpuNs=0;
            if(!state->probeAccounting) {
                capture.record.reset();state->activeCapture.reset();state->probeSession.reset();
                std::lock_guard lock(state->mutex);state->optionalBusy=false;++state->totals.probesCanceled;return;
            }
        }
        {
            std::lock_guard lock(state->mutex);
            if(state->batching) {
                const auto latest=state->batching->cadence.last();admission.tick=latest.tick;
                const auto floor=state->batching->cadence.floorNs(),now=monotonicNs();
                if(floor && state->probeDue>latest.tick && state->probeDue-latest.tick<=(UINT64_MAX-latest.startedNs)/floor) {
                    const auto due=latest.startedNs+(state->probeDue-latest.tick)*floor;
                    if(due>now)admission.slackNs=due-now;
                }
            }
            admission.requiredBacklog=state->canceled || !state->queue.empty();
        }
        if(auto session=state->probeSession.lock())admission.requiredGeneration=session->currentGeneration();
        auto& accounting=*state->probeAccounting;
        if(!accounting.envelopeReady() && !state->probe && !state->settlement && !accounting.failed()) {
            accounting.advanceEnvelope();
        }else if(state->activeCapture && !state->probe && !state->settlement) {
            CaptureProbeReplay::Receipt attempted;
            state->probeSlack=admission.slackNs;
            if(!accounting.failed())state->probe=CaptureProbeReplay::begin(std::move(state->activeCapture->record),admission,attempted,
                CaptureProbeReplay::Clock{},state->hooks.probe);
            state->probeResult=attempted;
            if(state->probe)if(auto authorization=state->probe->authorization())state->probePlan=authorization->plan;
            if(!state->probe) {
                if(auto token=GradientCpuCapture::takeSettlement(state->activeCapture->record))state->settlement.emplace(std::move(*token));
                // A failed adapter may have consumed its record and performed
                // full-credit cancellation. No fabricated successful outcome.
                if(!state->settlement) {
                    state->activeCapture.reset();state->probeAccounting.reset();state->probeSession.reset();
                    std::lock_guard lock(state->mutex);state->optionalBusy=false;++state->totals.probesCanceled;return;
                }
            }
            state->activeCapture.reset();
            std::lock_guard lock(state->mutex);if(state->probe)++state->totals.probesStarted;
        }else if(state->probe) {
            const auto progress=state->probe->advance(admission);
            state->probeResult=state->probe->receipt();
            if(progress==CaptureProbeReplay::Progress::ReadyToSettle || progress==CaptureProbeReplay::Progress::Declined) {
                // Final qualification uses actual completion slack, before the
                // accounting tails. Require another full measured completion
                // interval remaining, then retain the conservative total window.
                const auto elapsed=state->probeResult.elapsedNs;
                state->probeSlack=elapsed && admission.slackNs>=elapsed && elapsed<=UINT64_MAX-admission.slackNs?
                    std::min(state->probeSlack,admission.slackNs+elapsed):0;
                if(auto token=state->probe->takeSettlement())state->settlement.emplace(std::move(*token));
            }
            else if(progress==CaptureProbeReplay::Progress::Quarantined && stopping.load()) {
                // Process shutdown cannot join an ambiguous native event.
                // During normal map cancellation this independent registration
                // continues background reaping without retaining the Map. At
                // shutdown the backend retains its complete capture graph and
                // global ticket until positive proof (or process reclamation).
                if(state->probeResult.executionFailed)
                    if(auto policy=state->probePolicy.lock())policy->observeAccepted(state->probeKey,Plan::CPU,0,admission.tick,true);
                state->probe.reset();state->probeAccounting.reset();state->probeSession.reset();state->probePolicy.reset();
                std::lock_guard lock(state->mutex);state->optionalBusy=false;++state->totals.probesCanceled;return;
            }
            if(state->settlement)accounting.markDeviceTerminal(monotonicNs());
        }
        if(state->settlement && !accounting.receipt().cleanupComplete) {
            accounting.markDeviceTerminal(monotonicNs());
            state->settlement->cleanup();state->probe.reset();
            accounting.markCleanup(*state->settlement,true,monotonicNs());
        }
        if(state->settlement && (accounting.failed() || accounting.advanceTail(monotonicNs()))) {
            accounting.accountStep(stepStart,glob2::threadCpuNs());
            const auto closingStart=glob2::threadCpuNs();
            auto result=accounting.finishResult(*state->settlement,admission.requiredGeneration,
                state->probeResult.nativeBackend,state->probeResult.exact,state->probeResult.exact,
                state->probeResult.elapsedNs,state->probeSlack);
            result=CaptureProbeAccounting::close(state->probeAccounting,std::move(result),closingStart);
            // Settlement/statistics/diagnostics occur after the final envelope.
            // Include a predictable full chunk allowance in the candidate and
            // paid credits; an overshoot demotes rather than hiding that CPU.
            constexpr std::uint64_t retirementAllowance=GradientCpuCapture::MaxBlockCpuNs;
            auto addRetirement=[](std::uint64_t ns){return ns>UINT64_MAX-retirementAllowance?UINT64_MAX:ns+retirementAllowance;};
            result.outcome.alternativeCpuNs=addRetirement(result.outcome.alternativeCpuNs);
            result.outcome.actualProbeCpuNs=addRetirement(result.outcome.actualProbeCpuNs);
            const auto finalMetadataStart=glob2::threadCpuNs();
            auto policy=state->probePolicy.lock();
            if(state->totals.diagnostics) {
                const auto& r=result.receipt;
                const auto& replay=state->probeResult;const auto& native=replay.nativeMetrics;const auto& key=state->probeKey;
                const char* reason=!replay.clockValid || r.invalidClocks ? "clock" : replay.overshoots || r.overshoots ? "overshoot" :
                    replay.executionFailed ? "driver" : replay.budgetCanceled ? "budget" : replay.requiredCanceled ? "required" :
                    replay.explicitCanceled ? "explicit" : replay.firstMismatch!=SIZE_MAX ? "mismatch" :
                    replay.setupDeclined ? "setup" : !r.inferenceEligible ? "accounting" : "complete";
                std::fprintf(stderr,"gradient_probe ticket=%llu policy_gen=%llu required_gen=%llu admission=%llu device_terminal=%llu cleanup=%llu tail1=%llu tail2=%llu process_begin=%llu process_end=%llu process_cpu=%llu known_inner=%llu unknown_upper=%llu capture_cpu=%llu calling_cpu=%llu closing_cpu=%llu alternative_cpu=%llu comparison_cpu=%llu elapsed=%llu slack=%llu reserve_cpu=%llu charge_cpu=%llu retired_allowance=%llu exact=%u inference=%u omitted=%zu churn=%u invalid=%llu overshoots=%llu"
                    " width=%u height=%u family=%u swim=%llu limit=%u cpu_buckets=%u threads=%u batch=%u plan=%u seed_density=%u blocker_density=%u movement_modifiers=%u due_tick=%llu"
                    " accepted_reference_cpu=%llu common_preparation_cpu=%llu authorization_reserve_cpu=%llu reason=%s budget_canceled=%u required_canceled=%u explicit_canceled=%u setup_declined=%u execution_failed=%u"
                    " native_metrics=%u native_stage=%u native_dispatches=%llu native_checks=%llu native_copied_cells=%llu native_cost_cells=%llu native_cost_bytes=%llu native_cost_uploads=%llu native_setup_cpu=%llu native_cpu=%llu\n",
                    (unsigned long long)r.ticketId,(unsigned long long)r.policyGeneration,(unsigned long long)r.requiredGeneration,
                    (unsigned long long)state->probeAdmissionNs,(unsigned long long)r.deviceTerminalNs,(unsigned long long)r.cleanupNs,
                    (unsigned long long)r.firstTailNs,(unsigned long long)r.secondTailNs,
                    (unsigned long long)r.processBeginNs,(unsigned long long)r.processEndNs,(unsigned long long)r.processCpuNs,
                    (unsigned long long)r.knownInnerCpuNs,(unsigned long long)r.unknownUpperCpuNs,
                    (unsigned long long)r.capturedCpuNs,(unsigned long long)r.optionalCallingCpuNs,(unsigned long long)r.closingCpuNs,
                    (unsigned long long)state->probeResult.alternativeCallingCpuNs,(unsigned long long)state->probeResult.comparisonCpuNs,
                    (unsigned long long)state->probeResult.elapsedNs,(unsigned long long)state->probeSlack,
                    (unsigned long long)state->probeReservedCpuNs,(unsigned long long)result.outcome.actualProbeCpuNs,
                    (unsigned long long)retirementAllowance,unsigned(state->probeResult.exact),unsigned(r.inferenceEligible),
                    r.omittedThreads,unsigned(r.churn),(unsigned long long)r.invalidClocks,(unsigned long long)r.overshoots,
                    key.width,key.height,unsigned(key.family),(unsigned long long)key.movement,key.limit,key.cpuBuckets,key.threads,key.batch,
                    unsigned(state->probePlan),unsigned(key.seedDensity),unsigned(key.blockerDensity),unsigned(key.movementModifiers),
                    (unsigned long long)state->probeDue,(unsigned long long)replay.acceptedReferenceCpuNs,
                    (unsigned long long)replay.commonPreparationCpuNs,(unsigned long long)replay.reservedCpuNs,reason,
                    unsigned(replay.budgetCanceled),unsigned(replay.requiredCanceled),unsigned(replay.explicitCanceled),
                    unsigned(replay.setupDeclined),unsigned(replay.executionFailed),unsigned(replay.nativeMetricsAvailable),unsigned(native.stage),
                    (unsigned long long)native.dispatches,(unsigned long long)native.hostChecks,(unsigned long long)native.copiedCells,
                    (unsigned long long)native.costCellsPrepared,(unsigned long long)native.costPreparedBytes,(unsigned long long)native.costUploads,
                    (unsigned long long)native.setupCpuNs,(unsigned long long)native.threadCpuNs);
            }
            // Publication never waits for this settlement or diagnostic row.
            state->settlement->settle(result.outcome,result.authorization);state->settlement.reset();state->probeSession.reset();state->probePolicy.reset();
            const auto& r=result.receipt;
            {std::lock_guard lock(state->mutex);
                ++state->totals.probeRetired;state->totals.probesExact+=state->probeResult.exact;
                state->totals.probesCanceled+=!r.inferenceEligible;
                state->totals.probeCallingCpuNs+=r.optionalCallingCpuNs+r.closingCpuNs;
                state->totals.probeUnknownCpuNs+=r.unknownUpperCpuNs;state->totals.probeProcessCpuNs+=r.processCpuNs;
                state->optionalBusy=false;state->optionalReadyNs=0;
            }
            if(policy) {
                if(state->probeResult.executionFailed || state->probeResult.firstMismatch!=SIZE_MAX)policy->observeAccepted(state->probeKey,Plan::CPU,0,admission.tick,true);
                {const auto metrics=policy->metrics();std::lock_guard lock(state->mutex);state->totals.automaticPolicy=metrics;}
                const auto end=glob2::threadCpuNs();
                const auto cost=glob2::threadCpuDeltaNs(finalMetadataStart,end);
                if(!finalMetadataStart || end<finalMetadataStart || cost>retirementAllowance) {
                    policy->observeAccepted(state->probeKey,Plan::CPU,0,admission.tick,true);
                    policy->recordTuningOverhead(admission.tick,cost>retirementAllowance?cost-retirementAllowance:0,
                        finalMetadataStart && end>=finalMetadataStart);
                }
            }
            return;
        }
        accounting.accountStep(stepStart,glob2::threadCpuNs());
        // A tail hold sleeps; it never continuously polls a GPU or a clock.
        const auto tailWake=accounting.nextTailWakeNs();
        state->optionalReadyNs=tailWake?tailWake:monotonicNs()+(state->probe?state->probe->nextAdvanceDelayNs():0);
    }
    void execute(const std::shared_ptr<GradientDeviceState>& state) noexcept {
        const auto started=monotonicNs(),cpuStart=serviceCpuNow(state.get());
        std::array<std::shared_ptr<OwnedGradientField>,8> held;std::size_t count=0;bool canceled=false,batchNotReady=false;std::uint64_t batchVersion=0;
        {
            std::lock_guard lock(state->mutex);canceled=state->canceled;
            const auto earlier=[](const auto& a,const auto& b) {
                return a.field->due!=b.field->due ? a.field->due<b.field->due : a.serial<b.serial;
            };
            auto earliest=std::min_element(state->queue.begin(),state->queue.end(),earlier);
            if(earliest==state->queue.end()) return;
            if(!state->totals.crossDueRequested && state->mode!=Backend::Automatic) {
                auto first=earliest->field;held[count++]=first;state->queue.erase(earliest);
                while(count<state->maximumBatch && !state->queue.empty()) {
                    auto next=std::min_element(state->queue.begin(),state->queue.end(),earlier);const auto& field=next->field;
                    if(field->due!=first->due || field->session!=first->session || field->decision.plan!=first->decision.plan ||
                       !(field->workload==first->workload))break;
                    held[count++]=field;state->queue.erase(next);
                }
            } else {
                if(state->totals.crossDueRequested || state->mode==Backend::Automatic)cadenceLocked(*state);
                auto first=earliest->field;
                // Gather only already-ready compatible entries. Never wait for a
                // second field, and never extrapolate a batch bound from singleton.
                std::array<GradientDeviceState::Queued*,8> candidates{};
                std::array<std::size_t,8> indices{};std::size_t available=0;
                for(std::size_t i=0;i<state->queue.size();++i) {
                    auto& queued=state->queue[i];const auto& field=queued.field;
                    if(field->session!=first->session || field->decision.plan!=first->decision.plan ||
                       !(field->workload==first->workload) ||
                       (field->due!=first->due && !state->totals.crossDueRequested && state->mode!=Backend::Automatic))continue;
                    if((state->totals.crossDueRequested || state->mode==Backend::Automatic) && (field->identity.owner!=first->identity.owner ||
                       field->identity.revision!=first->identity.revision || field->identity.variant!=first->identity.variant ||
                       !knownBatchShape(*field,state->computeThreads)))continue;
                    std::size_t insert=available;
                    while(insert && earlier(queued,*candidates[insert-1]))--insert;
                    if(insert>=state->maximumBatch)continue;
                    if(available<state->maximumBatch)++available;
                    for(std::size_t j=available-1;j>insert;--j){candidates[j]=candidates[j-1];indices[j]=indices[j-1];}
                    candidates[insert]=&queued;indices[insert]=i;
                }
                count=1;
                for(const unsigned n:{8u,4u,2u}) {
                    if(n>available || n>state->maximumBatch)continue;
                    auto key=first->workload;key.batch=n;
                    const GradientBatchBound* bound=nullptr;GradientBatchBound measured;std::uint64_t candidateVersion=0;
                    if(state->mode==Backend::Automatic) {
                        const auto policy=first->session->learningPolicy();
                        if(!policy || !state->batching || !knownBatchShape(*first,state->computeThreads))continue;
                        const auto choice=policy->lookup(key);
                        if(choice.plan!=first->decision.plan || !choice.maximumElapsedNs)continue;
                        bool qualified=true;
                        for(unsigned i=0;i<n;++i) {
                            const auto& field=candidates[i]->field;
                            auto offered=field->workload;offered.batch=field->minimumBatchCount;
                            const auto current=policy->lookup(offered);
                            qualified=qualified && field->minimumBatchCount<=n && current.plan==field->decision.plan &&
                                current.version==field->decision.version && (!field->offeredBatchElapsedNs ||
                                current.maximumElapsedNs==field->offeredBatchElapsedNs);
                        }
                        if(!qualified)continue;
                        candidateVersion=choice.version;
                        measured={key,choice.plan,first->identity.revision,first->identity.variant,choice.maximumElapsedNs,500000};
                        bound=&measured;
                    }else if(state->totals.crossDueRequested) {
                        if(state->mode!=Backend::OpenCL || !state->totals.crossDueReady || !knownBatchShape(*first,state->computeThreads))continue;
                        for(unsigned i=0;i<state->batching->manifest.count;++i) {
                            const auto& entry=state->batching->manifest.bounds[i];
                            if(entry.workload==key && entry.plan==first->decision.plan &&
                               entry.costRevision==first->identity.revision && entry.costVariant==first->identity.variant){bound=&entry;break;}
                        }
                    }
                    if(!bound)continue;
                    const auto now=monotonicNs();bool fits=true;
                    for(unsigned i=0;i<n;++i) {
                        const auto& field=candidates[i]->field;
                        fits=fits && state->batching->cadence.fits(field->publicationTick,field->cadenceRevision,field->cadenceFloorNs,*bound,now);
                    }
                    if(!fits)continue;
                    count=n;
                    if(state->mode==Backend::Automatic)batchVersion=candidateVersion;
                    // Polling can return to untouched-seed CPU recovery before
                    // the estimated publication boundary. Actual due keys and
                    // original completion tickets remain unchanged.
                    for(unsigned i=0;i<n;++i)if(auto control=candidates[i]->field->requiredControl) {
                        const auto& field=candidates[i]->field;const auto latest=state->batching->cadence.last();
                        const auto floor=std::min(state->batching->cadence.floorNs(),field->cadenceFloorNs);
                        const auto due=latest.startedNs+(field->publicationTick-latest.tick)*floor;
                        control->setDeadlineNs(due-bound->completionMarginNs);
                    }
                    break;
                }
                batchNotReady=count<first->minimumBatchCount;
                if(count==1) {held[0]=first;state->queue.erase(earliest);}
                else {
                    for(std::size_t i=0;i<count;++i)held[i]=candidates[i]->field;
                    const bool crossDue=std::any_of(held.begin(),held.begin()+count,[&](const auto& field){return field->due!=first->due;});
                    if(crossDue)++state->totals.crossDueBatches;
                    std::sort(indices.begin(),indices.begin()+count,std::greater<>());
                    for(std::size_t i=0;i<count;++i)state->queue.erase(state->queue.begin()+indices[i]);
                }
            }
            state->activeControls.fill({});
            for(std::size_t i=0;i<count;++i)state->activeControls[i]=held[i]->requiredControl;
            ++state->totals.batches;state->totals.maxBatch=std::max<std::uint64_t>(state->totals.maxBatch,count);
        }
        const std::span fields(held.data(),count);bool handled=false,submissionStarted=false;
        const bool diagnostics=state->totals.diagnostics;
        const bool trackCompletion=diagnostics || bool(fields.front()->session->learningPolicy());
        std::uint64_t preparationEnd=cpuStart,submissionEnd=cpuStart;
        // An insufficient ready batch submits no driver work. Its exact CPU
        // continuation pays the calling coordinator CPU, without initializing
        // optional diagnostic registry/envelope storage on that required path.
        const bool envelopeRequested=state->totals.cpuEnvelopeRequested && !batchNotReady;
        const auto envelopeSetupStarted=envelopeRequested ? glob2::threadCpuNs() : 0;
        auto envelope=envelopeRequested ? beginBatchCpuEnvelope() : nullptr;
        const auto envelopeSetupCpu=envelopeRequested ? glob2::threadCpuDeltaNs(envelopeSetupStarted,glob2::threadCpuNs()) : 0;
        const bool missingPreparationClock=std::any_of(fields.begin(),fields.end(),[](const auto& field){return !field->preparationCpuClockValid;});
        const bool clockUnavailable=state->mode==Backend::Automatic && (state->automaticClockUnavailable || !cpuStart || missingPreparationClock);
        const bool stale=std::any_of(fields.begin(),fields.end(),[&](const auto& field) {
            if(field->decision.generation!=field->session->currentGeneration() || field->session->failed.load())return true;
            if(state->mode==Backend::Automatic && !batchNotReady) {
                const auto policy=field->session->learningPolicy();if(!policy)return true;
                auto key=field->workload;key.batch=unsigned(count);const auto choice=policy->lookup(key);
                return choice.plan!=field->decision.plan || choice.version!=(count>1?batchVersion:field->decision.version);
            }
            return false;
        });
        if(!canceled && !stale && !clockUnavailable && !batchNotReady) {
            try {
                std::vector<BackendRequest> requests;requests.reserve(count);
                for(const auto& field:fields) {
                    requests.push_back({field->data.get(),field->limit,field->grid,*field->session,field.get(),
                        [](void* value,std::size_t cell){auto& field=*static_cast<OwnedGradientField*>(value);return field.costAt(field,cell);},
                        nullptr,field->identity,field->family});
                    requests.back().cpuBuckets=field->cpuBuckets;requests.back().executedOnDevice=&field->executedGPU;
                    requests.back().deviceExecutionObserved=&field->deviceExecutionObserved;
                    requests.back().requiredControl=field->requiredControl;
                }
                if(diagnostics)preparationEnd=serviceCpuNow(state.get());
                if(trackCompletion) {
                    const auto stamp=monotonicNs();for(const auto& field:fields) field->deviceStartedWallNs=stamp;
                }
                submissionStarted=true;
                handled=state->hooks.execute ? state->hooks.execute(requests,fields.front()->decision.plan)
                    : executeOpenCLDevice(requests,fields.front()->decision.plan);
                if(diagnostics) submissionEnd=serviceCpuNow(state.get());
            } catch(...) {
                handled=false;
                if(diagnostics) {submissionEnd=serviceCpuNow(state.get());if(!submissionStarted)preparationEnd=submissionEnd;}
            }
        }
        const auto cpuEnd=serviceCpuNow(state.get());
        const bool hostClockValid=validCpuInterval(cpuStart,cpuEnd) && (!diagnostics ||
            (validCpuInterval(cpuStart,preparationEnd) && validCpuInterval(preparationEnd,submissionEnd) && cpuEnd>=submissionEnd));
        bool batchTimingInvalid=!hostClockValid || missingPreparationClock;
        const auto elapsed=monotonicNs()-started,consumed=glob2::threadCpuDeltaNs(cpuStart,cpuEnd);
        if(trackCompletion) {
            const auto stamp=monotonicNs();for(const auto& field:fields)field->deviceCompletedWallNs=stamp;
        }
        std::size_t cells=0;for(const auto& field:fields) cells+=field->grid.cells();
        for(const auto& field:fields) {
            field->executedBatchCount=unsigned(count);field->hostCpuClockValid=hostClockValid;
            field->serviceNs=elapsed;field->hostCpuNs=(consumed/cells)*field->grid.cells()+(consumed%cells)*field->grid.cells()/cells;
            if(handled) {
                field->inputs.reset();field->identity={};
                field->completion->complete();
            }
            else {
                field->fallbackReason=canceled ? GradientFallbackReason::Shutdown : batchNotReady ? GradientFallbackReason::BatchNotReady : stale ? GradientFallbackReason::StaleGeneration
                    : clockUnavailable ? GradientFallbackReason::CpuClockUnavailable
                    : field->requiredControl && field->requiredControl->shouldCancel(monotonicNs()) ? GradientFallbackReason::Cancellation
                    : field->session->failed.load() ? GradientFallbackReason::DriverFailure : GradientFallbackReason::BackendDecline;
            }
        }
        const auto envelopeFinishStarted=envelopeRequested ? glob2::threadCpuNs() : 0;
        if(envelope)finishBatchCpuEnvelope(envelope->envelope);
        // Retain only bounded scalar metadata; release optional storage before
        // publishing telemetry. The original completion ticket is already done.
        struct Summary {bool present=false,valid=false,churn=false;std::uint64_t process=0,known=0,unknown=0,sampler=0,omitted=0,resolution=0;} envelopeResult;
        if(envelope){const auto& result=envelope->envelope.metrics();envelopeResult={true,result.valid,result.churn,
            result.processCpuNs,result.knownInnerCpuNs,result.unknownUpperCpuNs,result.samplerCpuNs,result.omittedThreads,result.maxResolutionNs};}
        envelope.reset();
        const auto envelopeFinishCpu=envelopeRequested ? glob2::threadCpuDeltaNs(envelopeFinishStarted,glob2::threadCpuNs()) : 0;
        if(handled && std::any_of(fields.begin(),fields.end(),[](const auto& field){return field->executedGPU;})) {
            auto key=fields.front()->workload;key.batch=unsigned(count);
            const auto observationEnd=serviceCpuNow(state.get());
            bool measured=hostClockValid && !missingPreparationClock && validCpuInterval(cpuStart,observationEnd) && observationEnd>=cpuEnd;
            auto cpu=glob2::threadCpuDeltaNs(cpuStart,observationEnd);
            if(state->mode==Backend::Automatic) {
                measured=measured && envelopeResult.present && envelopeResult.valid && !envelopeResult.churn && !envelopeResult.omitted;
                if(envelopeResult.unknown>UINT64_MAX-cpu)measured=false;else cpu+=envelopeResult.unknown;
            }
            for(const auto& field:fields){if(field->seedCpuNs>UINT64_MAX-cpu)measured=false;else cpu+=field->seedCpuNs;}
            observe(state,fields.front()->session,key,fields.front()->decision.plan,measured ? cpu : 0,fields.front()->tick,!measured);
            if(!measured){batchTimingInvalid=true;if(state->mode==Backend::Automatic)state->automaticClockUnavailable=true;}
        }
        {
        std::lock_guard lock(state->mutex);
        if(batchTimingInvalid){++state->totals.clockInvalidMeasurements;
            if(state->mode==Backend::Automatic)state->automaticClockUnavailable=true;}
        state->totals.automaticClockUnavailable=state->automaticClockUnavailable;
        if(state->automaticClockUnavailable)state->learningAvailable.store(false,std::memory_order_release);
        if(envelopeRequested) {
            state->totals.cpuEnvelopeLifecycleNs+=envelopeSetupCpu+envelopeFinishCpu;
            state->totals.cpuEnvelopeSetupOvershoots+=envelopeSetupCpu>500000;
            state->totals.cpuEnvelopeFinishOvershoots+=envelopeFinishCpu>500000;
            if(!envelopeResult.present)++state->totals.cpuEnvelopeDeclines;
            else {
                state->totals.cpuEnvelopeSamplerNs+=envelopeResult.sampler;
                state->totals.cpuEnvelopeOmittedThreads+=envelopeResult.omitted;
                state->totals.cpuEnvelopeChurn+=envelopeResult.churn;
                state->totals.cpuEnvelopeMaxResolutionNs=std::max(state->totals.cpuEnvelopeMaxResolutionNs,envelopeResult.resolution);
                if(!envelopeResult.valid)++state->totals.cpuEnvelopeInvalid;
                else {
                    ++state->totals.cpuEnvelopeWindows;
                    state->totals.cpuEnvelopeProcessNs+=envelopeResult.process;
                    state->totals.cpuEnvelopeKnownInnerNs+=envelopeResult.known;
                    state->totals.cpuEnvelopeUnknownUpperNs+=envelopeResult.unknown;
                }
            }
        }
        if(diagnostics) {
            state->totals.batchPreparationCpuNs+=glob2::threadCpuDeltaNs(cpuStart,preparationEnd);
            state->totals.batchSubmissionCpuNs+=glob2::threadCpuDeltaNs(preparationEnd,submissionEnd);
            state->totals.batchCompletionCpuNs+=glob2::threadCpuDeltaNs(submissionEnd,serviceCpuNow(state.get()));
        }
        for(const auto& field:fields)if(field->deviceExecutionObserved){
            ++state->totals.deviceCompletedFields;
            if(!handled)++state->totals.fallbackAfterDeviceCompletionFields;
        }
        state->activeControls.fill({});
        if(handled) {
            state->totals.completed+=count;
            for(const auto& field:fields) field->executedGPU ? ++state->totals.executed : ++state->totals.trivial;
        }
        }
        if(!handled) {
            // Freeze failed-offer CPU after coordinator cleanup, before any
            // worker continuation can read it. Attribution among fields is
            // diagnostic; batch offer demotion sums only actual paid receipts.
            const auto finishedCpu=serviceCpuNow(state.get());
            const bool valid=hostClockValid && validCpuInterval(cpuStart,finishedCpu) && finishedCpu>=cpuEnd;
            const auto completeCpu=glob2::threadCpuDeltaNs(cpuStart,finishedCpu);
            for(const auto& field:fields) {
                field->hostCpuClockValid=valid;
                field->hostCpuNs=(completeCpu/cells)*field->grid.cells()+(completeCpu%cells)*field->grid.cells()/cells;
                fallbackField(field);
            }
        }
    }
    void run() noexcept {
        brokerThreadId=glob2::nativeThreadId();brokerThreads=1;const auto cpuStart=glob2::threadCpuNs();
        for(;;) {
            const auto schedulerStart=glob2::threadCpuNs();
            std::shared_ptr<GradientDeviceState> selected,optionalOwner;
            enum class Work {Initialize,Required,Observation,Cadence,Probe};Work work=Work::Initialize;
            {
                std::unique_lock lock(mutex);
                const auto wait=optionalWait();
                if(wait==std::chrono::nanoseconds::max())wake.wait(lock,[&]{return stopping || pending();});
                else wake.wait_for(lock,wait,[&]{return pending();});
                for(auto at=registrations.begin();at!=registrations.end();) {
                    auto state=*at;std::lock_guard stateLock(state->mutex);
                    if(stopping) {state->canceled=true;state->learningAvailable.store(false,std::memory_order_release);}
                    if((state->canceled || (state->initialized && !state->started)) && state->queue.empty() && !state->optionalBusy.load()) {
                        // Destroy retained observations/hooks on the background thread.
                        for(auto& observation:state->observations) observation={};
                        state->observationRead=state->observationWritten=0;state->hooks={};
                        state->batching.reset();
                        at=registrations.erase(at);
                    } else ++at;
                }
                if(stopping && registrations.empty()) break;
                std::uint64_t due=std::numeric_limits<std::uint64_t>::max();
                for(const auto& state:registrations) {
                    std::lock_guard stateLock(state->mutex);
                    for(const auto& queued:state->queue) if(!selected || queued.field->due<due) {
                        selected=state;due=queued.field->due;work=Work::Required;
                    }
                }
                if(!selected) for(const auto& state:registrations) {
                    std::lock_guard stateLock(state->mutex);
                    if(!state->initialized) {selected=state;work=Work::Initialize;break;}
                }
                if(!selected) for(const auto& state:registrations) {
                    std::lock_guard stateLock(state->mutex);
                    if(state->batching && state->batching->cadenceRead!=state->batching->cadenceWritten) {selected=state;work=Work::Cadence;break;}
                }
                if(!selected) for(const auto& state:registrations) {
                    std::lock_guard stateLock(state->mutex);
                    if(state->observationRead!=state->observationWritten) {selected=state;work=Work::Observation;break;}
                }
                if(!selected) for(const auto& state:registrations) {
                    std::lock_guard stateLock(state->mutex);
                    if(state->optionalBusy.load() && monotonicNs()>=state->optionalReadyNs) {selected=state;work=Work::Probe;break;}
                }
                for(const auto& state:registrations)if(state->optionalBusy.load()){optionalOwner=state;break;}
            }
            const auto schedulerEnd=glob2::threadCpuNs();
            if(optionalOwner) {
                if(optionalOwner->probeAccounting)optionalOwner->probeAccounting->accountStep(schedulerStart,schedulerEnd);
                else optionalOwner->pendingProbeSchedulerCpuNs+=glob2::threadCpuDeltaNs(schedulerStart,schedulerEnd);
            }
            brokerCpuNs=glob2::threadCpuDeltaNs(cpuStart,glob2::threadCpuNs());
            if(!selected) continue;
            if(work==Work::Required) execute(selected);
            else if(work==Work::Initialize) initialize(selected);
            else if(work==Work::Cadence){std::lock_guard lock(selected->mutex);cadenceLocked(*selected);}
            else if(work==Work::Probe) advanceProbe(selected);
            else {
                const auto optionalStart=glob2::threadCpuNs();
                GradientDeviceState::Observation observation;
                {std::lock_guard lock(selected->mutex);observation=std::move(selected->observations[
                    selected->observationRead++%selected->observations.size()]);}
                if(auto session=observation.session.lock();session && session->currentGeneration()==observation.generation)
                    if(auto policy=session->learningPolicy()) {
                        if(!selected->startupCharged) {
                            policy->recordTuningOverhead(observation.tick,selected->totals.tuningStartupUpperCpuNs,
                                selected->totals.tuningStartupClockValid);
                            selected->startupCharged=true;
                        }
                        if(observation.batchOfferFallback)
                            policy->observeBatchOfferFallback(observation.key,observation.plan,observation.offerVersion,
                                observation.cpuNs,observation.tick,!observation.failed);
                        else {
                            policy->observeAccepted(observation.key,observation.plan,observation.cpuNs,observation.tick,observation.failed,observation.publicationStall);
                            if(observation.key.batch>1 && (observation.failed || observation.publicationStall)) {
                                auto singleton=observation.key;singleton.batch=1;
                                policy->observeBatchFailure(singleton,observation.key.batch,observation.tick);
                            }
                        }
                        // Frozen8 has an offline exactness corpus covering the
                        // supported classic complete-field representation.
                        if(observation.key.cpuBuckets==BUCKETS && !observation.key.movementModifiers &&
                           observation.key.batch==1 && (readyPlans.load()&(1u<<unsigned(Plan::Frozen8))))
                            policy->qualify(observation.key,Plan::Frozen8,true);
                        const auto end=glob2::threadCpuNs();
                        policy->recordTuningOverhead(observation.tick,glob2::threadCpuDeltaNs(optionalStart,end),optionalStart && end>=optionalStart);
                        const auto metrics=policy->metrics();
                        std::lock_guard lock(selected->mutex);selected->totals.automaticPolicy=metrics;
                    }
            }
            brokerCpuNs=glob2::threadCpuDeltaNs(cpuStart,glob2::threadCpuNs());
        }
        brokerCpuNs=glob2::threadCpuDeltaNs(cpuStart,glob2::threadCpuNs());brokerThreads=0;
    }
public:
    ~DeviceBroker() {
        {std::lock_guard lock(mutex);stopping=true;}wake.notify_one();if(coordinator.joinable()) coordinator.join();
    }
    bool add(const std::shared_ptr<GradientDeviceState>& state) noexcept {
        try {
            std::lock_guard lock(mutex);if(stopping || !storageLease.bytes || registrations.full()) return false;
            if(!coordinator.joinable()) coordinator=GAGCore::ThreadSupport::launch([this]{run();});
            registrations.push_back(state);wake.notify_one();return true;
        } catch(...) {return false;}
    }
    // Producers release the session lock before taking this lock. Sharing the
    // wait mutex prevents a notification being lost between predicate and wait.
    void notify() noexcept {std::lock_guard lock(mutex);wake.notify_one();}
};
DeviceBroker& broker(){static DeviceBroker value;return value;}
}
OwnedGradientField::~OwnedGradientField(){releaseReservation();}
void OwnedGradientField::releaseReservation() noexcept {
    if(reservedHostBytes) {
        releaseOpenCLHostBytes(reservedHostBytes);if(serviceRetained) serviceRetained->fetch_sub(reservedHostBytes);reservedHostBytes=0;
    }
}
std::unique_ptr<std::uint16_t[]> OwnedGradientField::takeData(){releaseReservation();return std::move(data);}
std::shared_ptr<GradientDeviceState> GradientDeviceService::state() const {std::lock_guard lock(mutex);return registration;}
void GradientDeviceService::configure(unsigned computeThreads,Backend mode) {
    stop();const auto bytes=mode!=Backend::CPU && computeThreads>=2?
        sizeof(GradientDeviceState)+128+64*(sizeof(BackendSession)+128)+8*sizeof(BackendRequest):0;
    if(hooks.probe.retainedBytes>OpenCLHostBudget-bytes)return;
    const auto ownedBytes=bytes?bytes+hooks.probe.retainedBytes:0;
    if(ownedBytes && !reserveOpenCLHostBytes(ownedBytes))return;
    std::shared_ptr<GradientDeviceState> next;
    try {next=std::make_shared<GradientDeviceState>();}catch(...){releaseOpenCLHostBytes(ownedBytes);throw;}
    next->storageLease.bytes=ownedBytes;next->hooks=hooks;next->cpuClock=hooks.cpuClock;next->nativeBackend=!hooks.initialize;
    next->totals.diagnostics=gradientDiagnosticsRequested();
    next->totals.automaticTuning=computeThreads>=2 && mode==Backend::Automatic && learningRequested();
    next->totals.cpuEnvelopeRequested=glob2::cpuEnvelopeRequested();
    if(next->totals.cpuEnvelopeRequested)glob2::registerCpuEnvelopeThread(glob2::CpuThreadRole::OtherOwned);
    next->mode=mode;next->computeThreads=computeThreads;
    if(const auto* value=std::getenv("GLOB2_GRADIENT_CROSS_DUE");value && *value) {
        if(std::strcmp(value,"0") && std::strcmp(value,"1"))throw std::invalid_argument("GLOB2_GRADIENT_CROSS_DUE must be 0 or 1");
        if(std::strcmp(value,"1")==0 && computeThreads>=2 && mode!=Backend::CPU) {
            const auto* path=std::getenv("GLOB2_GRADIENT_BATCH_PROFILE");
            std::size_t pathLength=0;if(path)while(pathLength<=4096 && path[pathLength])++pathLength;
            if(!path || !pathLength || pathLength>4096)throw std::invalid_argument("cross-due batching requires a bounded batch profile path");
            next->totals.crossDueRequested=true;
            constexpr auto bytes=sizeof(GradientBatchAdmissionState)+8192+128;
            if(!reserveOpenCLHostBytes(bytes))++next->totals.batchProfileDeclines;
            else {
                try {
                    next->batching=std::make_shared<GradientBatchAdmissionState>();next->batching->lease.bytes=bytes;
                    next->batching->path.assign(path,pathLength);
                } catch(...) {
                    if(next->batching)next->batching.reset();else releaseOpenCLHostBytes(bytes);
                    ++next->totals.batchProfileDeclines;
                }
            }
        }
    }
    if(next->totals.automaticTuning && !next->batching) {
        constexpr auto bytes=sizeof(GradientBatchAdmissionState)+128;
        if(reserveOpenCLHostBytes(bytes)) {
            try {next->batching=std::make_shared<GradientBatchAdmissionState>();next->batching->lease.bytes=bytes;}
            catch(...){releaseOpenCLHostBytes(bytes);next->totals.automaticTuning=false;}
        }else next->totals.automaticTuning=false;
    }
    if(const auto* value=std::getenv("GLOB2_GRADIENT_BATCH")) {
        unsigned parsed=0;const auto* end=value+std::strlen(value);const auto result=std::from_chars(value,end,parsed);
        if(result.ec!=std::errc{} || result.ptr!=end || !parsed || parsed>8)
            throw std::invalid_argument("GLOB2_GRADIENT_BATCH must be 1 through 8");
        next->maximumBatch=parsed;
    }
    next->totals.configuredMaxBatch=next->maximumBatch;{std::lock_guard lock(mutex);registration=next;}
    if constexpr(!GAGCore::ThreadSupport::available) return;
    if(computeThreads<2 || mode==Backend::CPU || (mode==Backend::Automatic && !learningRequested() && !hooks.initialize) ||
       (!prepareAccelerator && !hooks.initialize)) return;
    {std::lock_guard lock(next->mutex);next->started=true;}
    if(!broker().add(next)) {std::lock_guard lock(next->mutex);next->started=false;}
}
void GradientDeviceService::stop() noexcept {
    if(auto current=state()) {
        bool registered=false;{std::lock_guard lock(current->mutex);registered=current->started;current->canceled=true;current->learningAvailable.store(false,std::memory_order_release);
            for(const auto& control:current->activeControls)if(control)control->requestCancel();}
        if(registered) broker().notify();
    }
}
bool GradientDeviceService::submit(const std::shared_ptr<OwnedGradientField>& field) noexcept {
    try {
        auto current=state();if(!current) return false;const auto admissionStart=glob2::threadCpuNs();std::unique_lock lock(current->mutex);
        if(field) {field->observer=weak_from_this();field->executionState=current;field->diagnostics=current->totals.diagnostics;}
        // Freeze the actual worker admission cost before queued fields become
        // visible to the coordinator. A decline has no coordinator/device work.
        const auto finishAdmission=[&](bool queued) {
            if(!field)return;
            const auto end=glob2::threadCpuNs(),cpu=glob2::threadCpuDeltaNs(admissionStart,end);
            field->preparationCpuClockValid=field->preparationCpuClockValid && validCpuInterval(admissionStart,end) && cpu<=UINT64_MAX-field->seedCpuNs;
            if(field->preparationCpuClockValid)field->seedCpuNs+=cpu;
            if(!queued){field->hostCpuClockValid=field->preparationCpuClockValid;field->hostCpuNs=0;}
        };
        if(!field || !field->session || !field->data || !field->completion || !field->cpu || !field->costAt) {
            if(field) field->fallbackReason=GradientFallbackReason::InvalidRequest;
            ++current->totals.declined;finishAdmission(false);return false;
        }
        if(!current->started || current->canceled || !current->initialized) {
            field->fallbackReason=current->canceled ? GradientFallbackReason::Shutdown : GradientFallbackReason::Unavailable;
            ++current->totals.declined;finishAdmission(false);return false;
        }
        if(field->requiredControl && field->requiredControl->shouldCancel(monotonicNs())) {
            field->fallbackReason=GradientFallbackReason::Cancellation;++current->totals.declined;finishAdmission(false);return false;
        }
        if(field->decision.plan==Plan::CPU || field->decision.generation!=field->session->currentGeneration()) {
            field->fallbackReason=GradientFallbackReason::StaleGeneration;
            ++current->totals.stale;++current->totals.declined;finishAdmission(false);return false;
        }
        bool unused=false;
        if(!field->admitted.compare_exchange_strong(unused,true)) {
            field->fallbackReason=GradientFallbackReason::Duplicate;++current->totals.declined;return false;
        }
        const auto cells=field->grid.cells();
        const auto declineBudget=[&] {
            field->admitted=false;field->fallbackReason=GradientFallbackReason::MemoryBudget;
            ++current->totals.budgetDeclines;++current->totals.declined;finishAdmission(false);return false;
        };
        if(current->queue.full() || cells>(OpenCLHostBudget-sizeof(OwnedGradientField))/sizeof(std::uint16_t)) return declineBudget();
        const auto sessionBytes=field->session->metrics().retainedBytes+128;
        if(sessionBytes>OpenCLHostBudget-cells*sizeof(std::uint16_t)-sizeof(OwnedGradientField))return declineBudget();
        const auto base=cells*sizeof(std::uint16_t)+sizeof(OwnedGradientField)+sessionBytes;
        if(field->retainedInputBytes>OpenCLHostBudget-base) return declineBudget();
        const auto bytes=base+field->retainedInputBytes;if(!reserveOpenCLHostBytes(bytes)) return declineBudget();
        field->reservedHostBytes=bytes;field->serviceRetained=std::shared_ptr<std::atomic<std::size_t>>(current,&current->retained);current->retained.fetch_add(bytes);
        try {current->queue.push_back({field,++current->nextSerial});}
        catch(...) {
            field->reservedHostBytes=0;current->retained.fetch_sub(bytes);releaseOpenCLHostBytes(bytes);return declineBudget();
        }
        if(current->batching){field->cadenceRevision=current->batching->cadence.version();field->cadenceFloorNs=current->batching->cadence.floorNs();}
        finishAdmission(true);
        ++current->totals.submitted;lock.unlock();broker().notify();return true;
    } catch(...) {return false;}
}
void GradientDeviceService::fallback(const std::shared_ptr<OwnedGradientField>& field) noexcept {fallbackField(field);}
void GradientDeviceService::recordAccepted(std::shared_ptr<BackendSession> session,const WorkloadKey& key,
    Plan plan,std::uint64_t cpuNs,std::uint64_t tick,bool failed,bool publicationStall) noexcept {
    if(auto current=state()) if(observe(current,std::move(session),key,plan,cpuNs,tick,failed,publicationStall)) broker().notify();
}
bool GradientDeviceService::learningActive() const noexcept {
    if(auto current=state())return current->learningAvailable.load(std::memory_order_acquire) &&
        (!current->nativeBackend || readyPlans.load(std::memory_order_acquire)!=0);
    return false;
}
GradientBatchChoice GradientDeviceService::acceptedBatchChoice(const std::shared_ptr<BackendSession>& session,
    const WorkloadKey& singleton) const noexcept {
    if(!ComputeExecutor::workerSlot() || !session || singleton.batch!=1 || singleton.movement || singleton.movementModifiers ||
       singleton.cpuBuckets!=BUCKETS || singleton.seedDensity>16 || singleton.blockerDensity>16 ||
       (singleton.family!=Family::Clear && singleton.family!=Family::Guard))return {};
    auto current=state();const auto policy=session->learningPolicy();if(!current || !policy)return {};
    unsigned maximum=1;
    {std::lock_guard lock(current->mutex);
        if(current->mode!=Backend::Automatic || !current->learningAvailable.load(std::memory_order_acquire) ||
           current->canceled || !current->started || !current->batching)return {};
        maximum=current->maximumBatch;
    }
    const auto generation=session->currentGeneration();
    for(const unsigned count:{2u,4u,8u})if(count<=maximum) {
        auto key=singleton;key.batch=count;const auto choice=policy->lookup(key);
        if(choice.plan==Plan::CPU || !choice.maximumElapsedNs ||
           (current->nativeBackend && !(readyPlans.load(std::memory_order_acquire)&(1u<<unsigned(choice.plan)))))continue;
        if(session->failed.load() || session->currentGeneration()!=generation)return {};
        return {{choice.plan,choice.version,generation},count,choice.maximumElapsedNs};
    }
    return {};
}
void GradientDeviceService::recordBatchOfferFallback(std::shared_ptr<BackendSession> session,const WorkloadKey& key,
    Plan plan,std::uint64_t version,std::uint64_t cpuNs,std::uint64_t tick,bool clockValid) noexcept {
    if(auto current=state())if(observe(current,std::move(session),key,plan,clockValid?cpuNs:0,tick,!clockValid,false,true,version))broker().notify();
}
std::unique_ptr<GradientCpuCapture> GradientDeviceService::beginCapture(std::shared_ptr<BackendSession> session,
    GradientCpuCapture::Input input,GradientCpuCapture::Receipt& receipt) noexcept {
    receipt={};if(!ComputeExecutor::workerSlot() || !session)return {};
    auto policy=session->learningPolicy();auto current=state();if(!policy || !current)return {};
    const auto started=glob2::threadCpuNs();
    std::unique_ptr<GradientCpuCapture> capture;
    try { [&] {
        const auto offer=policy->probeOffer(input.key);
        {std::lock_guard lock(current->mutex);
            if(!current->totals.automaticTuning || !current->started || !current->initialized || current->canceled ||
               !current->queue.empty() || !current->batching || !current->batching->cadence.valid() ||
               !offer.reservedCpuNs || !offer.elapsedNs) return;
            const auto latest=current->batching->cadence.last();const auto floor=current->batching->cadence.floorNs(),now=monotonicNs();
            if(input.due<=latest.tick || (input.due-latest.tick)>(UINT64_MAX-latest.startedNs)/floor)return;
            const auto dueNs=latest.startedNs+(input.due-latest.tick)*floor;
            if(now>=dueNs)return;
            input.slackNs=dueNs-now;input.backlog=false;
        }
        input.alternative=offer.plan;input.reservedCpuNs=offer.reservedCpuNs;
        input.captureCpuLimitNs=std::min(offer.reservedCpuNs/4,std::uint64_t(500000));
        input.conservativeElapsedNs=offer.elapsedNs;
        input.alternativeReady=(readyPlans.load(std::memory_order_acquire)&(1u<<unsigned(offer.plan)))!=0;
        capture=GradientCpuCapture::begin(policy,input,receipt);
    }();
    }catch(...) {}
    const auto end=glob2::threadCpuNs();
    // A successful capture owns its admission/clone charge; add only the
    // disjoint outer offer/queue handling. Rejected attempts pay their entirety.
    const bool valid=started && end>=started;const auto total=glob2::threadCpuDeltaNs(started,end);
    policy->recordTuningOverhead(input.tick,capture && receipt.clockValid && total>=receipt.optionalCpuNs?
        total-receipt.optionalCpuNs:total,valid);
    {std::lock_guard lock(current->mutex);capture?++current->totals.captures:++current->totals.captureDeclines;}
    return capture;
}
bool GradientDeviceService::submitCapture(std::shared_ptr<GradientCpuCapture::Record> record,std::shared_ptr<BackendSession> session) noexcept {
    auto current=state();if(!current || !record || !session)return false;
    {std::lock_guard lock(current->mutex);
        if(current->canceled || !current->started || current->optionalBusy.exchange(true))return false;
        current->probePolicy=session->learningPolicy();
        current->capture.emplace(GradientDeviceState::Capture{std::move(record),session});current->optionalReadyNs=0;
    }
    broker().notify();return true;
}
bool GradientDeviceService::crossDueRequested() const noexcept {
    if(auto current=state()){std::lock_guard lock(current->mutex);return current->totals.crossDueRequested && current->batching && current->started && !current->canceled;}
    return false;
}
void GradientDeviceService::recordCadence(GradientCadenceSample sample) noexcept {
    auto current=state();if(!current)return;
    {std::lock_guard lock(current->mutex);
        if((!current->totals.crossDueRequested && !current->totals.automaticTuning) || !current->batching || current->canceled || !current->started)return;
        if(current->batching->cadenceWritten-current->batching->cadenceRead==current->batching->cadenceQueue.size()) {
            ++current->totals.cadenceDrops;++current->batching->cadenceRead;sample.nonWaitNs=0;
        }
        current->batching->cadenceQueue[current->batching->cadenceWritten++%current->batching->cadenceQueue.size()]=sample;++current->totals.cadenceSamples;
    }
    broker().notify();
}
GradientDeviceService::Metrics GradientDeviceService::metrics() const {
    auto current=state();Metrics out;
    if(current) {
        std::lock_guard lock(current->mutex);out=current->totals;out.queued=current->queue.size();
        out.running=current->started && !current->canceled;out.ready=out.running && current->initialized;
        out.retainedHostBytes=current->retained.load();
    }
    out.cpuEnvelopeRegistryReady=bool(lookupEnvelopeRegistry());out.cpuEnvelopeRegistrySetupCpuNs=envelopeRegistrySetupCpuNs.load();
    out.cpuEnvelopeRoleRegistrationCpuNs=envelopeRoleRegistrationCpuNs.load();
    out.hostCpuNs=brokerCpuNs.load();out.coordinatorThreads=brokerThreads.load();
    out.coordinatorThreadId=brokerThreadId.load();return out;
}
}
