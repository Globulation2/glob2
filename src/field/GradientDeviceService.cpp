// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientDeviceService.h"
#include "OpenCLGradient.h"
#include "GradientBatchManifest.h"
#include "common/ThreadCpuClock.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <fstream>

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
struct GradientDeviceState
{
    struct Queued {std::shared_ptr<OwnedGradientField> field;std::uint64_t serial;};
    struct Observation {
        std::shared_ptr<BackendSession> session;
        WorkloadKey key;
        Plan plan=Plan::CPU;
        std::uint64_t generation=0,cpuNs=0,tick=0;
        bool failed=false,publicationStall=false;
    };
    mutable std::mutex mutex;
    GradientDeviceService::Hooks hooks;
    GradientDeviceService::Metrics totals;
    std::deque<Queued> queue;
    std::array<Observation,64> observations;
    std::size_t observationRead=0,observationWritten=0;
    std::uint64_t nextSerial=0;
    unsigned maximumBatch=8,computeThreads=1;
    Backend mode=Backend::CPU;
    std::shared_ptr<GradientBatchAdmissionState> batching;
    bool started=false,initialized=false,canceled=false;
    std::shared_ptr<std::atomic<std::size_t>> retained=std::make_shared<std::atomic<std::size_t>>(0);
};
namespace
{
std::atomic<std::uint64_t> brokerCpuNs{0};
std::atomic<unsigned> brokerThreads{0};
std::atomic<std::uint64_t> brokerThreadId{0};
void recoverField(void* context,std::size_t) noexcept
{
    auto& field=*static_cast<OwnedGradientField*>(context);
    const auto cpuStart=glob2::threadCpuNs();
    try {field.cpu(field);} catch(...) {field.error=std::current_exception();}
    field.fallbackCpuNs=glob2::threadCpuNs()-cpuStart;
    const auto cleanupStart=field.diagnostics ? glob2::threadCpuNs() : 0;
    field.inputs.reset();field.identity={};
    if(field.diagnostics)field.fallbackCleanupCpuNs=glob2::threadCpuNs()-cleanupStart;
    if(auto service=field.observer.lock()) service->recordAccepted(field.session,field.workload,
        field.decision.plan,field.seedCpuNs+field.hostCpuNs+field.fallbackCpuNs,field.tick,true);
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
    const WorkloadKey& key,Plan plan,std::uint64_t cpuNs,std::uint64_t tick,bool failure=false,bool publicationStall=false) noexcept
{
    std::lock_guard lock(state->mutex);
    if(!state->started || state->canceled || !session || !session->learningPolicy()) return false;
    if(state->observationWritten-state->observationRead==state->observations.size()) {
        ++state->totals.observationDrops;
        if(!publicationStall)return false;
        ++state->observationRead; // Retain a stall demotion instead of an older optional sample.
    }
    if(publicationStall)++state->totals.publicationStalls;
    state->observations[state->observationWritten++%state->observations.size()]={
        session,key,plan,session->currentGeneration(),cpuNs,tick,failure,publicationStall};return true;
}

// The broker is process-owned. Only process exit joins its thread. Holding the
// backend lease until AFTER that join pins Device/API/library even if their
// static root is destroyed first. No callback borrows a service, Map or pipeline.
class DeviceBroker
{
    std::shared_ptr<const void> backendLifetime=retainOpenCLLifetime();
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<std::shared_ptr<GradientDeviceState>> registrations;
    std::thread coordinator;
    bool stopping=false,realInitialized=false,realReady=false;
    bool pending() const {
        for(const auto& state:registrations) {
            std::lock_guard lock(state->mutex);
            if(state->canceled || !state->started || !state->initialized || !state->queue.empty() ||
               state->observationRead!=state->observationWritten || (state->batching && state->batching->cadenceRead!=state->batching->cadenceWritten)) return true;
        }
        return false;
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
        try {
            if(state->hooks.initialize) ready=state->hooks.initialize();
            else {
                if(!realInitialized) {realReady=initializeOpenCL();realInitialized=true;}
                ready=realReady && readyPlans.load(std::memory_order_acquire)!=0;
            }
        } catch(...) {}
        if(ready)loadManifest(*state);
        std::lock_guard lock(state->mutex);state->initialized=true;state->started=ready && !state->canceled;
        state->totals.initializationNs=monotonicNs()-start;
    }
    void execute(const std::shared_ptr<GradientDeviceState>& state) noexcept {
        const auto started=monotonicNs(),cpuStart=glob2::threadCpuNs();
        std::array<std::shared_ptr<OwnedGradientField>,8> held;std::size_t count=0;bool canceled=false;
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
                if(state->totals.crossDueRequested)cadenceLocked(*state);
                auto first=earliest->field;
                // Gather only already-ready compatible entries. Never wait for a
                // second field, and never extrapolate a batch bound from singleton.
                std::array<GradientDeviceState::Queued*,8> candidates{};
                std::array<std::size_t,8> indices{};std::size_t available=0;
                for(std::size_t i=0;i<state->queue.size();++i) {
                    auto& queued=state->queue[i];const auto& field=queued.field;
                    if(field->session!=first->session || field->decision.plan!=first->decision.plan ||
                       !(field->workload==first->workload) ||
                       (field->due!=first->due && !state->totals.crossDueRequested))continue;
                    if(state->totals.crossDueRequested && (field->identity.owner!=first->identity.owner ||
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
                for(std::size_t n=available;n>1;--n) {
                    auto key=first->workload;key.batch=unsigned(n);
                    if(state->totals.crossDueRequested && state->mode!=Backend::OpenCL)continue; // Development forced mode only.
                    if(state->mode==Backend::Automatic) {
                        const auto policy=first->session->learningPolicy();
                        if(!policy || policy->lookup(key).plan!=first->decision.plan)continue;
                    }
                    if(state->totals.crossDueRequested) {
                        if(!state->totals.crossDueReady || !knownBatchShape(*first,state->computeThreads))continue;
                        const GradientBatchBound* bound=nullptr;
                        for(unsigned i=0;i<state->batching->manifest.count;++i) {
                            const auto& entry=state->batching->manifest.bounds[i];
                            if(entry.workload==key && entry.plan==first->decision.plan &&
                               entry.costRevision==first->identity.revision && entry.costVariant==first->identity.variant){bound=&entry;break;}
                        }
                        if(!bound)continue;
                        const auto now=monotonicNs();bool fits=true;
                        for(std::size_t i=0;i<n;++i) {
                            const auto& field=candidates[i]->field;
                            fits=fits && state->batching->cadence.fits(field->publicationTick,field->cadenceRevision,field->cadenceFloorNs,*bound,now);
                        }
                        if(!fits)continue;
                    }
                    count=n;break;
                }
                if(count==1) {held[0]=first;state->queue.erase(earliest);}
                else {
                    for(std::size_t i=0;i<count;++i)held[i]=candidates[i]->field;
                    const bool crossDue=std::any_of(held.begin(),held.begin()+count,[&](const auto& field){return field->due!=first->due;});
                    if(crossDue)++state->totals.crossDueBatches;
                    std::sort(indices.begin(),indices.begin()+count,std::greater<>());
                    for(std::size_t i=0;i<count;++i)state->queue.erase(state->queue.begin()+indices[i]);
                }
            }
            ++state->totals.batches;state->totals.maxBatch=std::max<std::uint64_t>(state->totals.maxBatch,count);
        }
        const std::span fields(held.data(),count);bool handled=false,submissionStarted=false;
        const bool diagnostics=state->totals.diagnostics;
        const bool trackCompletion=diagnostics || bool(fields.front()->session->learningPolicy());
        std::uint64_t preparationEnd=cpuStart,submissionEnd=cpuStart;
        const bool stale=std::any_of(fields.begin(),fields.end(),[](const auto& field) {
            return field->decision.generation!=field->session->currentGeneration() || field->session->failed.load();
        });
        if(!canceled && !stale) {
            try {
                std::vector<BackendRequest> requests;requests.reserve(count);
                for(const auto& field:fields) {
                    requests.push_back({field->data.get(),field->limit,field->grid,*field->session,field.get(),
                        [](void* value,std::size_t cell){auto& field=*static_cast<OwnedGradientField*>(value);return field.costAt(field,cell);},
                        nullptr,field->identity,field->family});
                    requests.back().cpuBuckets=field->cpuBuckets;requests.back().executedOnDevice=&field->executedGPU;
                }
                if(diagnostics)preparationEnd=glob2::threadCpuNs();
                if(trackCompletion) {
                    const auto stamp=monotonicNs();for(const auto& field:fields) field->deviceStartedWallNs=stamp;
                }
                submissionStarted=true;
                handled=state->hooks.execute ? state->hooks.execute(requests,fields.front()->decision.plan)
                    : executeOpenCLDevice(requests,fields.front()->decision.plan);
                if(diagnostics) submissionEnd=glob2::threadCpuNs();
            } catch(...) {
                handled=false;
                if(diagnostics) {submissionEnd=glob2::threadCpuNs();if(!submissionStarted)preparationEnd=submissionEnd;}
            }
        }
        if(diagnostics && submissionEnd<preparationEnd) submissionEnd=glob2::threadCpuNs();
        const auto elapsed=monotonicNs()-started,consumed=glob2::threadCpuNs()-cpuStart;
        if(trackCompletion) {
            const auto stamp=monotonicNs();for(const auto& field:fields)field->deviceCompletedWallNs=stamp;
        }
        std::size_t cells=0;for(const auto& field:fields) cells+=field->grid.cells();
        for(const auto& field:fields) {
            field->executedBatchCount=unsigned(count);
            field->serviceNs=elapsed;field->hostCpuNs=(consumed/cells)*field->grid.cells()+(consumed%cells)*field->grid.cells()/cells;
            if(handled) {
                field->inputs.reset();field->identity={};
                field->completion->complete();
            }
            else {
                field->fallbackReason=canceled ? GradientFallbackReason::Shutdown : stale ? GradientFallbackReason::StaleGeneration
                    : field->session->failed.load() ? GradientFallbackReason::DriverFailure : GradientFallbackReason::BackendDecline;
                fallbackField(field);
            }
        }
        if(handled && std::any_of(fields.begin(),fields.end(),[](const auto& field){return field->executedGPU;})) {
            auto key=fields.front()->workload;key.batch=unsigned(count);
            auto cpu=glob2::threadCpuNs()-cpuStart;for(const auto& field:fields) cpu+=field->seedCpuNs;
            observe(state,fields.front()->session,key,fields.front()->decision.plan,cpu,fields.front()->tick);
        }
        std::lock_guard lock(state->mutex);
        if(diagnostics) {
            state->totals.batchPreparationCpuNs+=preparationEnd-cpuStart;
            state->totals.batchSubmissionCpuNs+=submissionEnd-preparationEnd;
            state->totals.batchCompletionCpuNs+=glob2::threadCpuNs()-submissionEnd;
        }
        if(handled) {
            state->totals.completed+=count;
            for(const auto& field:fields) field->executedGPU ? ++state->totals.executed : ++state->totals.trivial;
        }
    }
    void run() noexcept {
        brokerThreadId=glob2::nativeThreadId();brokerThreads=1;const auto cpuStart=glob2::threadCpuNs();
        for(;;) {
            std::shared_ptr<GradientDeviceState> selected;
            enum class Work {Initialize,Required,Observation,Cadence};Work work=Work::Initialize;
            {
                std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping || pending();});
                for(auto at=registrations.begin();at!=registrations.end();) {
                    auto state=*at;std::lock_guard stateLock(state->mutex);
                    if(stopping) state->canceled=true;
                    if((state->canceled || (state->initialized && !state->started)) && state->queue.empty()) {
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
            }
            brokerCpuNs=glob2::threadCpuNs()-cpuStart;
            if(!selected) continue;
            if(work==Work::Required) execute(selected);
            else if(work==Work::Initialize) initialize(selected);
            else if(work==Work::Cadence){std::lock_guard lock(selected->mutex);cadenceLocked(*selected);}
            else {
                GradientDeviceState::Observation observation;
                {std::lock_guard lock(selected->mutex);observation=std::move(selected->observations[
                    selected->observationRead++%selected->observations.size()]);}
                if(observation.session->currentGeneration()==observation.generation)
                    if(auto policy=observation.session->learningPolicy()) policy->observeAccepted(
                        observation.key,observation.plan,observation.cpuNs,observation.tick,observation.failed,observation.publicationStall);
            }
            brokerCpuNs=glob2::threadCpuNs()-cpuStart;
        }
        brokerCpuNs=glob2::threadCpuNs()-cpuStart;brokerThreads=0;
    }
public:
    ~DeviceBroker() {
        {std::lock_guard lock(mutex);stopping=true;}wake.notify_one();if(coordinator.joinable()) coordinator.join();
    }
    bool add(const std::shared_ptr<GradientDeviceState>& state) noexcept {
        try {
            std::lock_guard lock(mutex);if(stopping || registrations.size()>=64) return false;
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
    stop();auto next=std::make_shared<GradientDeviceState>();next->hooks=hooks;
    next->totals.diagnostics=gradientDiagnosticsRequested();next->mode=mode;next->computeThreads=computeThreads;
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
        bool registered=false;{std::lock_guard lock(current->mutex);registered=current->started;current->canceled=true;}
        if(registered) broker().notify();
    }
}
bool GradientDeviceService::submit(const std::shared_ptr<OwnedGradientField>& field) noexcept {
    try {
        auto current=state();if(!current) return false;std::unique_lock lock(current->mutex);
        if(field) {field->observer=weak_from_this();field->executionState=current;field->diagnostics=current->totals.diagnostics;}
        if(!field || !field->session || !field->data || !field->completion || !field->cpu || !field->costAt) {
            if(field) field->fallbackReason=GradientFallbackReason::InvalidRequest;
            ++current->totals.declined;return false;
        }
        if(!current->started || current->canceled || !current->initialized) {
            field->fallbackReason=current->canceled ? GradientFallbackReason::Shutdown : GradientFallbackReason::Unavailable;
            ++current->totals.declined;return false;
        }
        if(field->decision.plan==Plan::CPU || field->decision.generation!=field->session->currentGeneration()) {
            field->fallbackReason=GradientFallbackReason::StaleGeneration;
            ++current->totals.stale;++current->totals.declined;return false;
        }
        bool unused=false;
        if(!field->admitted.compare_exchange_strong(unused,true)) {
            field->fallbackReason=GradientFallbackReason::Duplicate;++current->totals.declined;return false;
        }
        const auto cells=field->grid.cells();
        const auto declineBudget=[&] {
            field->admitted=false;field->fallbackReason=GradientFallbackReason::MemoryBudget;
            ++current->totals.budgetDeclines;++current->totals.declined;return false;
        };
        if(cells>(OpenCLHostBudget-sizeof(OwnedGradientField))/sizeof(std::uint16_t)) return declineBudget();
        const auto base=cells*sizeof(std::uint16_t)+sizeof(OwnedGradientField);
        if(field->retainedInputBytes>OpenCLHostBudget-base) return declineBudget();
        const auto bytes=base+field->retainedInputBytes;if(!reserveOpenCLHostBytes(bytes)) return declineBudget();
        field->reservedHostBytes=bytes;field->serviceRetained=current->retained;current->retained->fetch_add(bytes);
        try {current->queue.push_back({field,++current->nextSerial});}
        catch(...) {
            field->reservedHostBytes=0;current->retained->fetch_sub(bytes);releaseOpenCLHostBytes(bytes);return declineBudget();
        }
        if(current->batching){field->cadenceRevision=current->batching->cadence.version();field->cadenceFloorNs=current->batching->cadence.floorNs();}
        ++current->totals.submitted;lock.unlock();broker().notify();return true;
    } catch(...) {return false;}
}
void GradientDeviceService::fallback(const std::shared_ptr<OwnedGradientField>& field) noexcept {fallbackField(field);}
void GradientDeviceService::recordAccepted(std::shared_ptr<BackendSession> session,const WorkloadKey& key,
    Plan plan,std::uint64_t cpuNs,std::uint64_t tick,bool failed,bool publicationStall) noexcept {
    if(auto current=state()) if(observe(current,std::move(session),key,plan,cpuNs,tick,failed,publicationStall)) broker().notify();
}
bool GradientDeviceService::crossDueRequested() const noexcept {
    if(auto current=state()){std::lock_guard lock(current->mutex);return current->totals.crossDueRequested && current->batching && current->started && !current->canceled;}
    return false;
}
void GradientDeviceService::recordCadence(GradientCadenceSample sample) noexcept {
    auto current=state();if(!current)return;
    {std::lock_guard lock(current->mutex);
        if(!current->totals.crossDueRequested || !current->batching || current->canceled || !current->started)return;
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
        out.retainedHostBytes=current->retained->load();
    }
    out.hostCpuNs=brokerCpuNs.load();out.coordinatorThreads=brokerThreads.load();
    out.coordinatorThreadId=brokerThreadId.load();return out;
}
}
