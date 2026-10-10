// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientDeviceService.h"
#include "OpenCLGradient.h"
#include "common/ThreadCpuClock.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace gradient_kernel
{
struct GradientDeviceState
{
    struct Queued {std::shared_ptr<OwnedGradientField> field;std::uint64_t serial;};
    struct Observation {
        std::shared_ptr<BackendSession> session;
        WorkloadKey key;
        Plan plan=Plan::CPU;
        std::uint64_t generation=0,cpuNs=0,tick=0;
        bool failed=false;
    };
    mutable std::mutex mutex;
    GradientDeviceService::Hooks hooks;
    GradientDeviceService::Metrics totals;
    std::deque<Queued> queue;
    std::array<Observation,64> observations;
    std::size_t observationRead=0,observationWritten=0;
    std::uint64_t nextSerial=0;
    unsigned maximumBatch=8;
    bool started=false,initialized=false,canceled=false;
    std::shared_ptr<std::atomic<std::size_t>> retained=std::make_shared<std::atomic<std::size_t>>(0);
};
namespace
{
std::atomic<std::uint64_t> brokerCpuNs{0};
std::atomic<unsigned> brokerThreads{0};
void recoverField(void* context,std::size_t) noexcept
{
    auto& field=*static_cast<OwnedGradientField*>(context);
    const auto cpuStart=glob2::threadCpuNs();
    try {field.cpu(field);} catch(...) {field.error=std::current_exception();}
    field.fallbackCpuNs=glob2::threadCpuNs()-cpuStart;
    field.inputs.reset();field.identity={};
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
    const WorkloadKey& key,Plan plan,std::uint64_t cpuNs,std::uint64_t tick,bool failure=false) noexcept
{
    std::lock_guard lock(state->mutex);
    if(!state->started || state->canceled || !session || !session->learningPolicy()) return false;
    if(state->observationWritten-state->observationRead==state->observations.size()) {++state->totals.observationDrops;return false;}
    state->observations[state->observationWritten++%state->observations.size()]={
        session,key,plan,session->currentGeneration(),cpuNs,tick,failure};return true;
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
               state->observationRead!=state->observationWritten) return true;
        }
        return false;
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
        std::lock_guard lock(state->mutex);state->initialized=true;state->started=ready && !state->canceled;
        state->totals.initializationNs=monotonicNs()-start;
    }
    void execute(const std::shared_ptr<GradientDeviceState>& state) noexcept {
        std::array<std::shared_ptr<OwnedGradientField>,8> held;std::size_t count=0;bool canceled=false;
        {
            std::lock_guard lock(state->mutex);canceled=state->canceled;
            const auto earlier=[](const auto& a,const auto& b) {
                return a.field->due!=b.field->due ? a.field->due<b.field->due : a.serial<b.serial;
            };
            auto earliest=std::min_element(state->queue.begin(),state->queue.end(),earlier);
            if(earliest==state->queue.end()) return;
            auto first=earliest->field;held[count++]=first;state->queue.erase(earliest);
            while(count<state->maximumBatch && !state->queue.empty()) {
                auto next=std::min_element(state->queue.begin(),state->queue.end(),earlier);const auto& field=next->field;
                if(field->due!=first->due || field->session!=first->session || field->decision.plan!=first->decision.plan ||
                   !(field->workload==first->workload)) break;
                held[count++]=field;state->queue.erase(next);
            }
            ++state->totals.batches;state->totals.maxBatch=std::max<std::uint64_t>(state->totals.maxBatch,count);
        }
        const std::span fields(held.data(),count);const auto started=monotonicNs(),cpuStart=glob2::threadCpuNs();bool handled=false;
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
                handled=state->hooks.execute ? state->hooks.execute(requests,fields.front()->decision.plan)
                    : executeOpenCLDevice(requests,fields.front()->decision.plan);
            } catch(...) {handled=false;}
        }
        const auto elapsed=monotonicNs()-started,consumed=glob2::threadCpuNs()-cpuStart;
        std::size_t cells=0;for(const auto& field:fields) cells+=field->grid.cells();
        for(const auto& field:fields) {
            field->serviceNs=elapsed;field->hostCpuNs=(consumed/cells)*field->grid.cells()+(consumed%cells)*field->grid.cells()/cells;
            if(handled) {field->inputs.reset();field->identity={};field->completion->complete();}
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
        if(handled) {
            state->totals.completed+=count;
            for(const auto& field:fields) field->executedGPU ? ++state->totals.executed : ++state->totals.trivial;
        }
    }
    void run() noexcept {
        brokerThreads=1;const auto cpuStart=glob2::threadCpuNs();
        for(;;) {
            std::shared_ptr<GradientDeviceState> selected;
            enum class Work {Initialize,Required,Observation};Work work=Work::Initialize;
            {
                std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping || pending();});
                for(auto at=registrations.begin();at!=registrations.end();) {
                    auto state=*at;std::lock_guard stateLock(state->mutex);
                    if(stopping) state->canceled=true;
                    if((state->canceled || (state->initialized && !state->started)) && state->queue.empty()) {
                        // Destroy retained observations/hooks on the background thread.
                        for(auto& observation:state->observations) observation={};
                        state->observationRead=state->observationWritten=0;state->hooks={};
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
                    if(state->observationRead!=state->observationWritten) {selected=state;work=Work::Observation;break;}
                }
            }
            brokerCpuNs=glob2::threadCpuNs()-cpuStart;
            if(!selected) continue;
            if(work==Work::Required) execute(selected);
            else if(work==Work::Initialize) initialize(selected);
            else {
                GradientDeviceState::Observation observation;
                {std::lock_guard lock(selected->mutex);observation=std::move(selected->observations[
                    selected->observationRead++%selected->observations.size()]);}
                if(observation.session->currentGeneration()==observation.generation)
                    if(auto policy=observation.session->learningPolicy()) policy->observeAccepted(
                        observation.key,observation.plan,observation.cpuNs,observation.tick,observation.failed);
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
        if(field) {field->observer=weak_from_this();field->executionState=current;}
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
        ++current->totals.submitted;lock.unlock();broker().notify();return true;
    } catch(...) {return false;}
}
void GradientDeviceService::fallback(const std::shared_ptr<OwnedGradientField>& field) noexcept {fallbackField(field);}
void GradientDeviceService::recordAccepted(std::shared_ptr<BackendSession> session,const WorkloadKey& key,
    Plan plan,std::uint64_t cpuNs,std::uint64_t tick,bool failed) noexcept {
    if(auto current=state()) if(observe(current,std::move(session),key,plan,cpuNs,tick,failed)) broker().notify();
}
GradientDeviceService::Metrics GradientDeviceService::metrics() const {
    auto current=state();Metrics out;
    if(current) {
        std::lock_guard lock(current->mutex);out=current->totals;out.queued=current->queue.size();
        out.running=current->started && !current->canceled;out.ready=out.running && current->initialized;
        out.retainedHostBytes=current->retained->load();
    }
    out.hostCpuNs=brokerCpuNs.load();out.coordinatorThreads=brokerThreads.load();return out;
}
}
