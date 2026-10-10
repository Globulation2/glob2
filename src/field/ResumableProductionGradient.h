// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "RuntimeTerrainGradient.h"
#include "OpenCLGradient.h"
#include "common/ThreadCpuClock.h"
#include <atomic>
#include <exception>
#include <limits>
#include <memory>
#include <span>

namespace gradient_kernel
{
// Optional full-field reference using the actual production SIMD bucket
// kernels. Required and building searches retain complete-layer execution.
// Cold copy/allocation, clocks, pauses and retained leases are paid work; this
// object's timing still needs comparison to the strongest warmed eager CPU.
class ResumableProductionGradient
{
public:
    static constexpr std::uint64_t MaxChunkCpuNs=500000;
    static constexpr unsigned MaxDeferredSeeds=1024;
    enum class Status {Pending,Complete,Canceled,Declined,Failed};
    struct Input {
        std::shared_ptr<const std::vector<std::uint16_t>> seeds;
        std::shared_ptr<const void> costsOwner;
        field::Grid grid{1,1};
        int maxCost=COST_LIMIT,swim=0;
        unsigned buckets=BUCKETS;
        // costsOwner must retain this exact immutable compiled Movement.
        // Null selects the classic swimming/uniform kernel. Otherwise classAt
        // is a cheap nonblocking lookup returning a valid movement profile ID,
        // exactly as the runtime eager kernel; it borrows no Map/worker scratch.
        const TerrainRegistry::Movement* movement=nullptr;
        bool (*isWater)(const void*,std::size_t)=nullptr;
        unsigned (*classAt)(const void*,std::size_t)=nullptr;
        std::size_t retainedCostBytes=0;
    };
    // Deterministic correctness/chunk tests may inject a clock. The production
    // broker must use the default native clock for all accounting/qualification.
    struct Clock {void* context=nullptr;std::uint64_t (*read)(void*)=nullptr;};
    struct Metrics {
        // This is paid optional work, including cold copy, allocation, clocks
        // and resumable bookkeeping. Shared SIMD proves array equivalence,
        // not equal CPU cost. Initial promotions MUST use the actual accepted
        // required CPU job cost frozen before admission, never this cpuNs.
        // GPU-accepted counterfactuals provide exact arrays/conservative
        // monitoring only until warm eager calibration proves a valid bound
        // against the strongest production CPU path.
        std::uint64_t cpuNs=0,lastChunkCpuNs=0,maxChunkCpuNs=0,chunks=0,entries=0,overshoots=0;
        std::size_t reservedHostBytes=0,bucketBytes=0,peakBucketBytes=0;
        bool cpuClockAvailable=false,uniformProfile=false,classic=false;
    };
private:
    struct Decline {};
    enum class Phase {Output,Copy,Sort,Sweep,Layer};
    Input input;
    Clock clock;
    std::size_t reserved=0,baseBytes=0,deferredBytes=0;
    std::unique_ptr<std::uint16_t[]> output;
    std::array<GradientBucket,256> buckets;
    std::vector<std::pair<int,int>> deferred;
    std::size_t copied=0,pending=0,nextSeed=0,layerCount=0,layerOffset=0;
    int cur=0,limit;
    unsigned uniformClass=256;
    bool uniform=true;
    Phase phase=Phase::Output;
    Status state=Status::Pending;
    std::atomic<bool> canceled{false};
    std::exception_ptr failure;
    Metrics totals;
    std::uint64_t chunkStarted=0,chunkBudget=MaxChunkCpuNs;
    std::uint64_t readCpu() const noexcept {return clock.read ? clock.read(clock.context) : glob2::threadCpuNs();}
    ResumableProductionGradient(Input value,Clock timer):input(std::move(value)),clock(timer),limit(std::min(input.maxCost,COST_LIMIT)) {
        totals.classic=!input.movement;
    }
    void charge(std::size_t bytes) {
        if(bytes>reserved && !reserveOpenCLProbeBytes(bytes-reserved)) throw Decline{};
        if(bytes<reserved) releaseOpenCLProbeBytes(reserved-bytes);
        reserved=bytes;totals.reservedHostBytes=bytes;
    }
    void clear() noexcept {
        output.reset();for(auto& bucket:buckets) {std::vector<std::uint32_t>{}.swap(bucket.cells);bucket.clear();}
        std::vector<std::pair<int,int>>{}.swap(deferred);input={};
        releaseOpenCLProbeBytes(reserved);reserved=0;totals.reservedHostBytes=0;totals.bucketBytes=0;
    }
    void checkAllocation() {
        const auto now=readCpu();
        if(chunkStarted && now>=chunkStarted && now-chunkStarted>chunkBudget) throw Decline{};
    }
    void reserveBucket(GradientBucket& bucket,std::size_t extra) {
        if(extra>std::numeric_limits<std::size_t>::max()-bucket.size) throw Decline{};
        const auto needed=bucket.size+extra;if(needed<=bucket.cells.size()) return;
        const auto count=std::max(needed,std::max<std::size_t>(256,bucket.cells.size()*2));
        if(count>OpenCLProbeBudget/sizeof(std::uint32_t)) throw Decline{};
        const auto old=bucket.cells.capacity()*sizeof(std::uint32_t);
        if(count<=bucket.cells.capacity()) {bucket.cells.resize(count);return;}
        const auto bytes=count*sizeof(std::uint32_t);
        charge(reserved+bytes); // Old and replacement allocations coexist.
        {std::vector<std::uint32_t> replacement(count);
            std::copy(bucket.cells.begin(),bucket.cells.end(),replacement.begin());bucket.cells.swap(replacement);}
        const auto actual=bucket.cells.capacity()*sizeof(std::uint32_t);
        charge(reserved-bytes-old+actual);
        totals.bucketBytes+=actual-old;totals.peakBucketBytes=std::max(totals.peakBucketBytes,totals.bucketBytes);
        checkAllocation();
    }
    void push(unsigned bucket,std::uint32_t cell) {auto& target=buckets[bucket];reserveBucket(target,1);target.cells[target.size++]=cell;++pending;}
    void defer(int cost,int cell) {
        if(deferred.size()==MaxDeferredSeeds) throw Decline{};
        if(deferred.size()==deferred.capacity()) {
            const auto count=std::min<std::size_t>(MaxDeferredSeeds,std::max<std::size_t>(256,deferred.capacity()*2));
            const auto bytes=count*sizeof(deferred[0]);charge(reserved+bytes);
            {std::vector<std::pair<int,int>> replacement;replacement.reserve(count);
                replacement.insert(replacement.end(),deferred.begin(),deferred.end());deferred.swap(replacement);}
            const auto actual=deferred.capacity()*sizeof(deferred[0]);charge(reserved-bytes-deferredBytes+actual);
            deferredBytes=actual;checkAllocation();
        }
        deferred.push_back({cost,cell});
    }
    template<unsigned Ring> void expand(std::size_t end) {
        const auto reserve=[&](GradientBucket& bucket,std::size_t extra){reserveBucket(bucket,extra);};
        const auto classAt=[&](std::size_t cell){return input.classAt(input.costsOwner.get(),cell);};
        if(uniform && uniformClass<input.movement->profiles.size()) {
            const PreparedTerrainCosts<1> single(std::array<EntrySteps,1>{input.movement->profiles[uniformClass]});
            expandPreparedTerrainBucketRange<Ring>(output.get(),buckets.data(),pending,cur,limit,input.grid,
                single,[](std::size_t){return 0;},layerOffset,end,reserve);
        } else std::visit([&](const auto& profile) {
            expandPreparedTerrainBucketRange<Ring>(output.get(),buckets.data(),pending,cur,limit,input.grid,
                profile,classAt,layerOffset,end,reserve);
        },*input.movement->prepared);
    }
    unsigned step(unsigned allowance) {
        switch(phase) {
        case Phase::Output:output.reset(new std::uint16_t[input.grid.cells()]);checkAllocation();phase=Phase::Copy;return 1;
        case Phase::Copy: {
            const auto begin=copied;
            const auto end=copied+std::min<std::size_t>(allowance,input.grid.cells()-copied);
            for(;copied<end;++copied) {
                const auto value=(*input.seeds)[copied];output[copied]=value;
                if(input.movement && uniform && value!=GRADIENT_FORBIDDEN) {
                    const auto c=input.classAt(input.costsOwner.get(),copied);
                    if(c>=input.movement->profiles.size()) throw std::invalid_argument("Invalid production probe profile");
                    if(uniformClass==256) uniformClass=c;else if(uniformClass!=c) uniform=false;
                }
                if(value<=GRADIENT_UNREACHABLE) continue;
                const int cost=GRADIENT_AT_GOAL-value;
                // Preserve the classic MAX_STEP seed boundary as well as the
                // prepared kernel's wider ring boundary and deferred ordering.
                const bool initial=input.movement ? cost<int(input.buckets) : cost<=MAX_STEP;
                if(initial) push(unsigned(cost)%input.buckets,std::uint32_t(copied));
                else defer(cost,int(copied));
            }
            if(copied==input.grid.cells()) phase=Phase::Sort;
            return unsigned(end-begin);
        }
        case Phase::Sort: {
            const auto start=readCpu();std::sort(deferred.begin(),deferred.end());const auto end=readCpu();
            if(start && end>=start && end-start>MaxChunkCpuNs) throw Decline{};
            totals.uniformProfile=input.movement && uniform && uniformClass<input.movement->profiles.size();
            phase=Phase::Sweep;return 1;
        }
        case Phase::Sweep:
            if(cur>limit || (!pending && nextSeed==deferred.size())) {state=Status::Complete;return 1;}
            if(!pending) cur=deferred[nextSeed].first;
            if(cur>limit) {state=Status::Complete;return 1;}
            if(nextSeed<deferred.size() && deferred[nextSeed].first==cur) {
                push(unsigned(cur)%input.buckets,std::uint32_t(deferred[nextSeed++].second));return 1;
            }
            layerCount=buckets[unsigned(cur)%input.buckets].size;layerOffset=0;
            if(!layerCount) ++cur;else phase=Phase::Layer;
            return 1;
        case Phase::Layer: {
            const auto end=layerOffset+std::min<std::size_t>(allowance,layerCount-layerOffset);
            const auto used=end-layerOffset;
            if(input.movement) {
                if(input.buckets==64) expand<64>(end);else if(input.buckets==128) expand<128>(end);else expand<256>(end);
            } else {
                const auto reserve=[&](GradientBucket& bucket,std::size_t extra){reserveBucket(bucket,extra);};
                if(!weightedClass(input.swim))
                    expandBucketRange<false>(output.get(),buckets.data(),pending,cur,limit,input.grid,LAND_STEPS,
                        [](std::size_t){return false;},layerOffset,end,reserve);
                else expandBucketRange<true>(output.get(),buckets.data(),pending,cur,limit,input.grid,entrySteps(WATER_STEP[input.swim]),
                    [&](std::size_t cell){return input.isWater(input.costsOwner.get(),cell);},layerOffset,end,reserve);
            }
            layerOffset=end;
            if(layerOffset==layerCount) {buckets[unsigned(cur)%input.buckets].clear();++cur;phase=Phase::Sweep;}
            return unsigned(used);
        }
        }
        return 1;
    }
public:
    ~ResumableProductionGradient(){clear();}
    ResumableProductionGradient(const ResumableProductionGradient&)=delete;
    static std::unique_ptr<ResumableProductionGradient> tryCreate(Input value,Clock timer) noexcept {
        const auto started=timer.read ? timer.read(timer.context) : glob2::threadCpuNs();
        try {
            if(!value.seeds || value.seeds->size()!=value.grid.cells() || value.grid.width()<=0 || value.grid.height()<=0 ||
                value.maxCost>COST_LIMIT ||
                value.swim<0 || unsigned(value.swim)>=std::size(WATER_STEP)) return {};
            if(value.movement) {
                if(!value.costsOwner || !value.classAt || !value.movement->prepared || value.movement->profiles.empty() ||
                    value.movement->profiles.size()>256 || (value.buckets!=64&&value.buckets!=128&&value.buckets!=256)) return {};
                for(const auto step:value.movement->profiles)
                    if(!step.cardinal || !step.diagonal || step.cardinal>=value.buckets || step.diagonal>=value.buckets) return {};
                const bool compiled=std::visit([&](const auto& profile) {
                    if(profile.classCount!=value.movement->profiles.size()) return false;
                    for(unsigned i=0;i<profile.classCount;++i) {
                        const auto a=profile.classes[i],b=value.movement->profiles[i];
                        if(a.cardinal!=b.cardinal || a.diagonal!=b.diagonal) return false;
                    }
                    return true;
                },*value.movement->prepared);
                if(!compiled) return {};
            } else if(value.buckets!=BUCKETS || (weightedClass(value.swim)&&(!value.costsOwner || !value.isWater))) return {};
            if(value.grid.cells()>OpenCLProbeBudget/sizeof(std::uint16_t) || value.seeds->capacity()>OpenCLProbeBudget/sizeof(std::uint16_t)) return {};
            const auto buffers=sizeof(ResumableProductionGradient)+sizeof(*value.seeds)
                +value.grid.cells()*sizeof(std::uint16_t)+value.seeds->capacity()*sizeof(std::uint16_t);
            if(buffers>OpenCLProbeBudget || value.retainedCostBytes>OpenCLProbeBudget-buffers) return {};
            auto result=std::unique_ptr<ResumableProductionGradient>(new ResumableProductionGradient(std::move(value),timer));
            result->baseBytes=buffers+result->input.retainedCostBytes;result->charge(result->baseBytes);
            const auto ended=result->readCpu();result->totals.cpuClockAvailable=started && ended>=started;
            if(result->totals.cpuClockAvailable) {
                result->totals.cpuNs=ended-started;
                if(ended-started>MaxChunkCpuNs) ++result->totals.overshoots;
            }
            return result;
        } catch(...) {return {};}
    }
    static std::unique_ptr<ResumableProductionGradient> tryCreate(Input value) noexcept {return tryCreate(std::move(value),Clock{});}
    Status advance(std::uint64_t cpuBudgetNs=MaxChunkCpuNs,unsigned maxEntries=4096) noexcept {
        if(state!=Status::Pending) return state;
        chunkStarted=readCpu();chunkBudget=std::max<std::uint64_t>(1,std::min(cpuBudgetNs,MaxChunkCpuNs));
        if(!chunkStarted) maxEntries=std::min<unsigned>(maxEntries,CHUNK);
        unsigned used=0;
        try {
            while(used<maxEntries && state==Status::Pending) {
                if(canceled.load()) {state=Status::Canceled;clear();break;}
                used+=step(std::min<unsigned>(unsigned(CHUNK),maxEntries-used));
                const auto now=readCpu();
                if(chunkStarted && now>=chunkStarted && now-chunkStarted>=chunkBudget) break;
            }
        } catch(const Decline&) {state=Status::Declined;clear();}
        catch(const std::bad_alloc&) {state=Status::Declined;clear();}
        catch(...) {failure=std::current_exception();state=Status::Failed;clear();}
        const auto ended=readCpu();const auto elapsed=chunkStarted && ended>=chunkStarted ? ended-chunkStarted : 0;
        totals.cpuClockAvailable=totals.cpuClockAvailable || bool(chunkStarted&&ended>=chunkStarted);
        totals.cpuNs+=elapsed;totals.lastChunkCpuNs=elapsed;totals.maxChunkCpuNs=std::max(totals.maxChunkCpuNs,elapsed);
        if(elapsed>MaxChunkCpuNs) ++totals.overshoots;
        totals.entries+=used;++totals.chunks;return state;
    }
    void requestCancel() noexcept {canceled.store(true);}
    Status status() const noexcept {return state;}
    Metrics metrics() const noexcept {return totals;}
    std::exception_ptr error() const noexcept {return failure;}
    std::span<const std::uint16_t> result() const noexcept {
        return state==Status::Complete ? std::span<const std::uint16_t>(output.get(),input.grid.cells()) : std::span<const std::uint16_t>{};
    }
};
} // namespace gradient_kernel
