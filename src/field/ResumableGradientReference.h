// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientCosts.h"
#include "Grid.h"
#include "OpenCLGradient.h"
#include "common/ThreadCpuClock.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace gradient_kernel
{
// A probe subset of the shared host limit. Source leases are conservatively
// charged even when another owner also retains them. No required-work ticket,
// executor join or production dispatch can depend on this reservation.
class ProbeHostLease
{
    std::size_t bytes=0;
public:
    static constexpr std::size_t Budget=OpenCLProbeBudget;
    ProbeHostLease()=default;
    ProbeHostLease(const ProbeHostLease&)=delete;
    ProbeHostLease& operator=(const ProbeHostLease&)=delete;
    ~ProbeHostLease(){reset();}
    bool acquire(std::size_t value) noexcept {
        if(bytes || value>Budget) return false;
        if(!reserveOpenCLProbeBytes(value)) return false;
        bytes=value;return true;
    }
    void reset() noexcept {
        if(!bytes) return;
        releaseOpenCLProbeBytes(bytes);bytes=0;
    }
    std::size_t size() const noexcept{return bytes;}
    static std::size_t currentBytes() noexcept{return openCLProbeBytes();}
};

// Background-only, scalar exactness reference. The indexed heap has one entry
// per cell, so arbitrary deferred seed costs need neither a whole-array sort
// nor an unbounded duplicate frontier. This solver is deliberately independent
// of the production SIMD implementation: its CPU time MUST NOT stand in for a
// production CPU counterfactual when promoting a GPU plan.
class ResumableGradientReference
{
public:
    static constexpr std::uint64_t MaxChunkCpuNs=500000;
    static constexpr unsigned ClockCheckEntries=64;
    enum class Status {Pending,Complete,Canceled,Failed};
    struct Input {
        std::shared_ptr<const std::vector<std::uint16_t>> seeds;
        std::shared_ptr<const void> costsOwner;
        EntrySteps (*costAt)(const void*,std::size_t)=nullptr;
        field::Grid grid{1,1};
        int maxCost=COST_LIMIT;
        std::size_t retainedCostBytes=0;
    };
    // Injected only by deterministic chunk-bound tests; production uses the
    // native thread CPU clock. costAt must be cheap, immutable and nonblocking.
    struct Clock {
        void* context=nullptr;
        std::uint64_t (*read)(void*)=nullptr;
    };
    struct Metrics {
        std::uint64_t cpuNs=0,lastChunkCpuNs=0,maxChunkCpuNs=0,entries=0,chunks=0;
        std::size_t heapPeak=0,reservedHostBytes=0;
        bool cpuClockAvailable=false;
    };
private:
    enum class Phase {Output,Heap,Positions,Copy,Expand};
    static constexpr std::uint32_t Absent=std::numeric_limits<std::uint32_t>::max();
    Input input;
    Clock clock;
    ProbeHostLease lease;
    std::unique_ptr<std::uint16_t[]> output;
    std::unique_ptr<std::uint32_t[]> heap,positions;
    std::size_t copied=0,heapSize=0;
    unsigned limit;
    Phase phase=Phase::Output;
    Status state=Status::Pending;
    std::atomic<bool> canceled{false};
    std::exception_ptr failure;
    Metrics totals;
    std::uint64_t readCpu() const noexcept {return clock.read ? clock.read(clock.context) : glob2::threadCpuNs();}
    ResumableGradientReference(Input value,Clock timer):input(std::move(value)),clock(timer),
        limit(unsigned(std::max(-1,std::min(input.maxCost,COST_LIMIT)))) {}
    bool before(std::uint32_t a,std::uint32_t b) const noexcept {
        return output[a]!=output[b] ? output[a]>output[b] : a<b;
    }
    void swapHeap(std::size_t a,std::size_t b) noexcept {
        std::swap(heap[a],heap[b]);positions[heap[a]]=std::uint32_t(a);positions[heap[b]]=std::uint32_t(b);
    }
    void enqueue(std::uint32_t cell) noexcept {
        std::size_t position=positions[cell];
        if(position==Absent) {position=heapSize;heap[heapSize++]=cell;positions[cell]=std::uint32_t(position);}
        while(position && before(heap[position],heap[(position-1)/2])) {
            const auto parent=(position-1)/2;swapHeap(position,parent);position=parent;
        }
        totals.heapPeak=std::max(totals.heapPeak,heapSize);
    }
    std::uint32_t pop() noexcept {
        const auto cell=heap[0];positions[cell]=Absent;
        if(--heapSize) {
            heap[0]=heap[heapSize];positions[heap[0]]=0;
            std::size_t at=0;
            while(at*2+1<heapSize) {
                auto next=at*2+1;
                if(next+1<heapSize && before(heap[next+1],heap[next])) ++next;
                if(!before(heap[next],heap[at])) break;
                swapHeap(at,next);at=next;
            }
        }
        return cell;
    }
    void clear() noexcept {
        output.reset();heap.reset();positions.reset();input={};lease.reset();totals.reservedHostBytes=0;
    }
    void step() {
        const auto cells=input.grid.cells();
        switch(phase) {
        // Uninitialized allocations avoid copying/clearing an entire plane in
        // one chunk. Each allocation is followed immediately by a clock check;
        // allocator/OS latency is measured, never presented as a hard guarantee.
        case Phase::Output:output.reset(new std::uint16_t[cells]);phase=Phase::Heap;return;
        case Phase::Heap:heap.reset(new std::uint32_t[cells]);phase=Phase::Positions;return;
        case Phase::Positions:positions.reset(new std::uint32_t[cells]);phase=Phase::Copy;return;
        case Phase::Copy:
            output[copied]=(*input.seeds)[copied];positions[copied]=Absent;
            if(input.maxCost>=0 && output[copied]>GRADIENT_UNREACHABLE &&
               unsigned(GRADIENT_AT_GOAL-output[copied])<=limit) enqueue(std::uint32_t(copied));
            if(++copied==cells) phase=Phase::Expand;
            return;
        case Phase::Expand:
            if(!heapSize) {state=Status::Complete;return;}
            const auto cell=pop();
            const auto cost=unsigned(GRADIENT_AT_GOAL-output[cell]);
            const auto entry=input.costAt(input.costsOwner.get(),cell);
            if(!entry.cardinal || !entry.diagonal) throw std::invalid_argument("Probe reference needs positive entry costs");
            unsigned ordinal=0;
            input.grid.neighborIndices(int(cell),field::Surrounding,[&](int next) {
                const auto offset=field::Surrounding[ordinal++];
                const unsigned step=offset.x && offset.y ? entry.diagonal : entry.cardinal;
                if(step>limit-cost || output[next]==GRADIENT_FORBIDDEN) return;
                const auto value=std::uint16_t(GRADIENT_AT_GOAL-cost-step);
                if(value>output[next]) {output[next]=value;enqueue(std::uint32_t(next));}
            });
            return;
        }
    }
public:
    ~ResumableGradientReference(){clear();}
    ResumableGradientReference(const ResumableGradientReference&)=delete;
    // Call on a background thread. Inputs remain held until cancel/destruction;
    // result bytes remain budgeted until the holder is destroyed.
    static std::unique_ptr<ResumableGradientReference> tryCreate(Input value,Clock timer) noexcept {
        const auto start=timer.read ? timer.read(timer.context) : glob2::threadCpuNs();
        try {
            if(!value.seeds || !value.costsOwner || !value.costAt || value.grid.width()<=0 || value.grid.height()<=0 ||
               value.maxCost>COST_LIMIT || std::size_t(value.grid.width())>ProbeHostLease::Budget/10/std::size_t(value.grid.height())) return {};
            if(value.seeds->size()!=value.grid.cells() || value.grid.cells()>=Absent) return {};
            // Storage is fixed before admission: 2-byte output and two 4-byte
            // indexed-heap arrays, plus the immutable original seed allocation.
            constexpr auto budget=ProbeHostLease::Budget;
            const auto cells=value.grid.cells();
            if(cells>budget/10 || value.seeds->capacity()>budget/sizeof(std::uint16_t)) return {};
            const auto buffers=cells*10+value.seeds->capacity()*sizeof(std::uint16_t)
                +sizeof(ResumableGradientReference)+sizeof(*value.seeds);
            if(buffers>budget || value.retainedCostBytes>budget-buffers) return {};
            auto result=std::unique_ptr<ResumableGradientReference>(new ResumableGradientReference(std::move(value),timer));
            if(!result->lease.acquire(buffers+result->input.retainedCostBytes)) return {};
            result->totals.reservedHostBytes=result->lease.size();
            const auto end=result->readCpu();
            result->totals.cpuClockAvailable=start && end>=start;
            if(result->totals.cpuClockAvailable) result->totals.cpuNs=end-start;
            return result;
        } catch(...) {return {};}
    }
    static std::unique_ptr<ResumableGradientReference> tryCreate(Input value) noexcept {return tryCreate(std::move(value),Clock{});}
    // Never joins anything. At most 64 copies/heap expansions occur between CPU
    // checks, and a smaller deterministic entry limit can pause any phase.
    Status advance(std::uint64_t cpuBudgetNs=MaxChunkCpuNs,unsigned maxEntries=4096) noexcept {
        if(state!=Status::Pending) return state;
        const auto start=readCpu();
        const auto budget=std::min(MaxChunkCpuNs,std::max(std::uint64_t(1),cpuBudgetNs));
        if(!start) maxEntries=std::min(maxEntries,ClockCheckEntries);
        unsigned count=0;
        try {
            while(count<maxEntries && state==Status::Pending) {
                if(canceled.load(std::memory_order_relaxed)) {state=Status::Canceled;clear();break;}
                const auto previous=phase;
                step();++count;
                if(previous==Phase::Output || previous==Phase::Heap || previous==Phase::Positions || count%ClockCheckEntries==0) {
                    const auto now=readCpu();
                    if(start && now>=start && now-start>=budget) break;
                }
            }
        } catch(...) {failure=std::current_exception();state=Status::Failed;clear();}
        const auto end=readCpu();
        totals.cpuClockAvailable=totals.cpuClockAvailable || (start && end>=start);
        totals.lastChunkCpuNs=start && end>=start ? end-start : 0;
        totals.cpuNs+=totals.lastChunkCpuNs;totals.maxChunkCpuNs=std::max(totals.maxChunkCpuNs,totals.lastChunkCpuNs);
        totals.entries+=count;++totals.chunks;
        return state;
    }
    void requestCancel() noexcept {canceled.store(true,std::memory_order_relaxed);}
    Status status() const noexcept{return state;}
    std::exception_ptr error() const noexcept{return failure;}
    Metrics metrics() const noexcept{return totals;}
    std::span<const std::uint16_t> result() const noexcept {
        return state==Status::Complete ? std::span<const std::uint16_t>(output.get(),input.grid.cells()) : std::span<const std::uint16_t>{};
    }
};
} // namespace gradient_kernel
