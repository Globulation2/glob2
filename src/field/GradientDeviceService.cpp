// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientDeviceService.h"
#include "OpenCLGradient.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include "common/ThreadCpuClock.h"

namespace gradient_kernel
{
OwnedGradientField::~OwnedGradientField() { releaseReservation(); }
void OwnedGradientField::releaseReservation() noexcept
{
    if(reservedHostBytes) {
        releaseOpenCLHostBytes(reservedHostBytes);
        if(serviceRetained) serviceRetained->fetch_sub(reservedHostBytes);
        reservedHostBytes=0;
    }
}
std::unique_ptr<std::uint16_t[]> OwnedGradientField::takeData()
{
    releaseReservation();
    return std::move(data);
}
void GradientDeviceService::configure(unsigned computeThreads,Backend mode)
{
    stop();
    maximumBatch=8;
    if(const auto* value=std::getenv("GLOB2_GRADIENT_BATCH")) {
        unsigned parsed=0;
        const auto* end=value+std::strlen(value);
        const auto result=std::from_chars(value,end,parsed);
        if(result.ec!=std::errc{} || result.ptr!=end || !parsed || parsed>8)
            throw std::invalid_argument("GLOB2_GRADIENT_BATCH must be 1 through 8");
        maximumBatch=parsed;
    }
    {
        std::lock_guard lock(mutex);
        totals={}; totals.configuredMaxBatch=maximumBatch;
    }
    if constexpr(!GAGCore::ThreadSupport::available) return;
    if(computeThreads<2 || mode==Backend::CPU ||
       (mode==Backend::Automatic && !learningRequested() && !hooks.initialize) ||
       (!prepareAccelerator && !hooks.initialize)) return;
    {
        std::lock_guard lock(mutex);
        stopping=false; started=true; initialized=false;
    }
    try { coordinator=GAGCore::ThreadSupport::launch([this]{run();}); }
    catch(...) { std::lock_guard lock(mutex); started=false; }
}
void GradientDeviceService::stop() noexcept
{
    { std::lock_guard lock(mutex); stopping=true;
      for(auto& observation:observations) observation={};
      observationRead=observationWritten=0;
    }
    wake.notify_one();
    if(coordinator.joinable()) coordinator.join();
    std::lock_guard lock(mutex); started=false; initialized=false;
}
bool GradientDeviceService::submit(const std::shared_ptr<OwnedGradientField>& field) noexcept
{
    try {
        std::lock_guard lock(mutex);
        if(field) field->observer=weak_from_this();
        if(!field || !field->session || !field->data || !field->completion || !field->cpu || !field->costAt) {
            if(field) field->fallbackReason=GradientFallbackReason::InvalidRequest;
            ++totals.declined; return false;
        }
        if(!started || stopping || !initialized) {
            field->fallbackReason=stopping ? GradientFallbackReason::Shutdown : GradientFallbackReason::Unavailable;
            ++totals.declined; return false;
        }
        if(field->decision.plan==Plan::CPU || field->decision.generation!=field->session->currentGeneration()) {
            field->fallbackReason=GradientFallbackReason::StaleGeneration;
            ++totals.stale; ++totals.declined; return false;
        }
        bool unclaimed=false;
        if(!field->admitted.compare_exchange_strong(unclaimed,true)) { field->fallbackReason=GradientFallbackReason::Duplicate; ++totals.declined; return false; }
        const auto cells=field->grid.cells();
        if(cells>(64ull*1024*1024-sizeof(OwnedGradientField))/sizeof(std::uint16_t)) {
            field->admitted=false; field->fallbackReason=GradientFallbackReason::MemoryBudget; ++totals.budgetDeclines; ++totals.declined; return false;
        }
        const auto base=cells*sizeof(std::uint16_t)+sizeof(OwnedGradientField);
        if(field->retainedInputBytes>OpenCLHostBudget-base) {
            field->admitted=false; field->fallbackReason=GradientFallbackReason::MemoryBudget; ++totals.budgetDeclines; ++totals.declined; return false;
        }
        const auto bytes=base+field->retainedInputBytes;
        if(!reserveOpenCLHostBytes(bytes)) { field->admitted=false; field->fallbackReason=GradientFallbackReason::MemoryBudget; ++totals.budgetDeclines; ++totals.declined; return false; }
        field->reservedHostBytes=bytes;
        field->serviceRetained=retained; retained->fetch_add(bytes);
        try { queue.push_back({field,++nextSerial}); }
        catch(...) {
            field->reservedHostBytes=0; field->admitted=false; retained->fetch_sub(bytes); releaseOpenCLHostBytes(bytes);
            field->fallbackReason=GradientFallbackReason::MemoryBudget;
            ++totals.declined; return false;
        }
        ++totals.submitted;
        wake.notify_one(); return true;
    } catch(...) { return false; }
}
void GradientDeviceService::recover(void* context,std::size_t) noexcept
{
    auto& field=*static_cast<OwnedGradientField*>(context);
    const auto cpuStart=glob2::threadCpuNs();
    try { field.cpu(field); }
    catch(...) { field.error=std::current_exception(); }
    field.fallbackCpuNs=glob2::threadCpuNs()-cpuStart;
    // The executor completes the original batch after this callback returns.
    field.inputs.reset(); field.identity={};
    if(auto service=field.observer.lock()) service->recordAccepted(field.session,field.workload,
        field.decision.plan,field.seedCpuNs+field.hostCpuNs+field.fallbackCpuNs,field.tick,true);
}
void GradientDeviceService::fallback(const std::shared_ptr<OwnedGradientField>& field) noexcept
{
    { std::lock_guard lock(mutex); ++totals.fallbacks;
      ++totals.fallbackReasons[unsigned(field->fallbackReason)];
    }
    field->completion->resume({&recover,field.get()});
}
void GradientDeviceService::run() noexcept
{
    const auto start=monotonicNs(), cpuStart=glob2::threadCpuNs();
    bool ready=false;
    try { ready=hooks.initialize ? hooks.initialize() : initializeOpenCL(); } catch(...) {}
    {
        std::lock_guard lock(mutex);
        initialized=ready;
        totals.initializationNs=monotonicNs()-start;
        totals.hostCpuNs+=glob2::threadCpuNs()-cpuStart;
    }
    if(!ready) { std::lock_guard lock(mutex); started=false; return; }
    for(;;) {
        std::array<std::shared_ptr<OwnedGradientField>,8> held;
        std::size_t count=0;
        bool cancel=false;
        {
            std::unique_lock lock(mutex);
            wake.wait(lock,[&]{return stopping || !queue.empty() || observationRead!=observationWritten;});
            if(!stopping && queue.empty() && observationRead!=observationWritten) {
                auto observation=std::move(observations[observationRead++%observations.size()]);
                lock.unlock();
                if(observation.session->currentGeneration()==observation.generation) {
                    if(auto policy=observation.session->learningPolicy())
                        policy->observeAccepted(observation.key,observation.plan,observation.cpuNs,observation.tick,observation.failed);
                    lock.lock(); totals.hostCpuNs=glob2::threadCpuNs()-cpuStart;
                }
                continue;
            }
            if(stopping && queue.empty()) { totals.hostCpuNs=glob2::threadCpuNs()-cpuStart; return; }
            cancel=stopping;
            auto earliest=std::min_element(queue.begin(),queue.end(),[](const auto& a,const auto& b){
                return a.field->due!=b.field->due ? a.field->due<b.field->due : a.serial<b.serial;
            });
            auto first=earliest->field; held[count++]=first; queue.erase(earliest);
            // Gather only requests that are ready now; never delay a singleton
            // or leapfrog an incompatible earlier deadline to fill a batch.
            while(count<maximumBatch && !queue.empty()) {
                auto next=std::min_element(queue.begin(),queue.end(),[](const auto& a,const auto& b){
                    return a.field->due!=b.field->due ? a.field->due<b.field->due : a.serial<b.serial;
                });
                if(next->field->due!=first->due || next->field->workload!=first->workload ||
                   next->field->session!=first->session || next->field->decision.plan!=first->decision.plan) break;
                held[count++]=next->field; queue.erase(next);
            }
            ++totals.batches; totals.maxBatch=std::max<std::uint64_t>(totals.maxBatch,count);
        }
        const std::span fields(held.data(),count);
        const bool stoppingBatch=cancel;
        const auto workStart=monotonicNs(), workCpu=glob2::threadCpuNs();
        bool handled=false;
        cancel=cancel || std::any_of(fields.begin(),fields.end(),[](const auto& field) {
            return field->decision.generation!=field->session->currentGeneration() || field->session->failed.load();
        });
        if(!cancel) {
            try {
                std::vector<BackendRequest> requests; requests.reserve(fields.size());
                for(const auto& field:fields) {
                    requests.push_back({field->data.get(),field->limit,field->grid,*field->session,field.get(),
                        [](void* value,std::size_t cell){auto& field=*static_cast<OwnedGradientField*>(value);return field.costAt(field,cell);},
                        nullptr,field->identity,field->family});
                    requests.back().cpuBuckets=field->cpuBuckets;
                    requests.back().executedOnDevice=&field->executedGPU;
                }
                handled=hooks.execute ? hooks.execute(requests,fields.front()->decision.plan)
                    : executeOpenCLDevice(requests,fields.front()->decision.plan);
            } catch(...) {}
        }
        const auto elapsed=monotonicNs()-workStart, consumed=glob2::threadCpuNs()-workCpu;
        std::size_t totalCells=0; for(const auto& field:fields) totalCells+=field->grid.cells();
        for(const auto& field:fields) {
            field->serviceNs=elapsed; field->hostCpuNs=(consumed/totalCells)*field->grid.cells()
                +(consumed%totalCells)*field->grid.cells()/totalCells;
            if(handled) {
                field->inputs.reset(); field->identity={};
                field->completion->complete();
            } else {
                if(cancel) field->fallbackReason=stoppingBatch ? GradientFallbackReason::Shutdown : GradientFallbackReason::StaleGeneration;
                else field->fallbackReason=field->session->failed.load() ? GradientFallbackReason::DriverFailure : GradientFallbackReason::BackendDecline;
                fallback(field);
            }
        }
        if(handled && std::any_of(fields.begin(),fields.end(),[](const auto& field){return field->executedGPU;})) {
            auto key=fields.front()->workload; key.batch=unsigned(fields.size());
            // Train from the actual homogeneous batch total, including final
            // ticket delivery, rather than inferred per-field attribution.
            std::uint64_t cpu=glob2::threadCpuNs()-workCpu; for(const auto& field:fields) cpu+=field->seedCpuNs;
            recordAccepted(fields.front()->session,key,fields.front()->decision.plan,cpu,fields.front()->tick);
        }
        {
            std::lock_guard lock(mutex);
            totals.hostCpuNs=glob2::threadCpuNs()-cpuStart;
            if(handled) {
                totals.completed+=fields.size();
                for(const auto& field:fields) field->executedGPU ? ++totals.executed : ++totals.trivial;
            }
        }
    }
}
void GradientDeviceService::recordAccepted(std::shared_ptr<BackendSession> session,const WorkloadKey& key,
    Plan plan,std::uint64_t cpuNs,std::uint64_t tick,bool failed) noexcept
{
    std::lock_guard lock(mutex);
    if(!started || stopping || !session || !session->learningPolicy()) return;
    if(observationWritten-observationRead==observations.size()) { ++totals.observationDrops; return; }
    observations[observationWritten++%observations.size()]={session,key,plan,session->currentGeneration(),cpuNs,tick,failed};
    wake.notify_one();
}
GradientDeviceService::Metrics GradientDeviceService::metrics() const
{
    std::lock_guard lock(mutex);
    auto out=totals; out.queued=queue.size(); out.running=started; out.ready=initialized;
    out.retainedHostBytes=retained->load();
    return out;
}
}
