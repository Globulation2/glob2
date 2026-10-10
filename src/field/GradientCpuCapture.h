// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientBackend.h"
#include "OpenCLGradient.h"
#include "common/ThreadCpuClock.h"
#include <cstring>
#include <climits>
#include <limits>
#include <thread>

namespace gradient_kernel
{
// The production accounting bridge owns qualification of complete process /
// driver-tail coverage. Diagnostics cannot manufacture this authorization.
class CaptureProbeAccounting;
class CaptureSettlementAuthorization {
    friend class CaptureProbeAccounting;
    bool qualified=false;
    explicit CaptureSettlementAuthorization(bool value):qualified(value) {}
public:
    CaptureSettlementAuthorization()=default;
    bool permitsInference() const noexcept {return qualified;}
};
// Unattached worker-side capture primitive. Nothing in required selection calls
// it. Admission must follow seeding and precede in-place CPU propagation.
// Modified/indirect terrain views deliberately decline: retaining a Terrain
// capacity estimate does not account its complete shared object graph.
class GradientCpuCapture
{
public:
    static constexpr std::size_t CopyBlockBytes=16*1024;
    static constexpr std::uint64_t MaxBlockCpuNs=500000;
    struct Clock {
        std::shared_ptr<const void> owner;
        std::uint64_t (*read)(const void*)=nullptr;
        // Full retained owner graph, not just sizeof a shared_ptr. Zero is
        // allowed only for the native clock (read==nullptr and no owner).
        std::size_t retainedBytes=0;
    };
    struct Input {
        std::span<const std::uint16_t> original;
        std::span<const std::uint8_t> water;
        WorkloadKey key;
        int swim=0,maxCost=COST_LIMIT;
        std::uint64_t tick=0,due=0,requiredGeneration=0;
        std::uint64_t commonPreparationCpuNs=0;
        // Device and qualified program are already ready. This flag is no
        // permission to initialize a device or compiler inside capture.
        bool alternativeReady=false,backlog=false,modifiedCosts=false;
        Plan alternative=Plan::Frozen8;
        std::uint64_t reservedCpuNs=0,captureCpuLimitNs=0;
        std::uint64_t slackNs=0,conservativeElapsedNs=0;
        // Borrowed only during synchronous capture. Packing never retains a
        // terrain/rule graph; unweighted classic fields never call this.
        void* waterContext=nullptr;
        std::uint8_t (*waterAt)(void*,std::size_t)=nullptr;
    };
    struct Receipt {
        // Optional capture/cleanup only. Required propagation is disjoint.
        // On invalid timing this is a lower bound and settlement pays the
        // complete CPU reservation. Rejected admission still returns a receipt.
        // A receipt taken before final destruction excludes final cleanup;
        // policy metrics include cleanup/cancellation. Future outer accounting
        // must also include validation and rejected attempts.
        std::uint64_t optionalCpuNs=0,blocks=0,invalidClocks=0,overshoots=0;
        std::size_t reservedBytes=0,copiedBytes=0;
        bool clockValid=true,requiresFullReservedCharge=false,referenceBound=false;
    };
    struct RequiredReceipt {
        std::uint64_t requiredGeneration=0,propagationCpuNs=0;
        bool successful=false,clockValid=false;
    };
    class Record {
        friend class GradientCpuCapture;
        std::shared_ptr<CpuSavingPolicy> policy;
        CpuSavingPolicy::ProbeTicket ticket;
        Clock clock;
        std::thread::id producer;
        WorkloadKey workload;
        int swimClass=0,cap=COST_LIMIT;
        std::uint64_t deadline=0,sessionGeneration=0,lastCpu=0,captureLimit=0;
        std::size_t count=0,lease=0;
        std::unique_ptr<std::uint16_t[]> originals,golden;
        std::unique_ptr<std::uint8_t[]> waterCells;
        Receipt totals;
        bool invalid=false,declined=false;
        bool handedOff=false,delegatedSettlement=false;
        struct Decline {};
        std::uint64_t readCpu() {
            if(invalid || declined)throw Decline{};
            std::uint64_t now=0;
            try {
                if(std::this_thread::get_id()!=producer)throw Decline{};
                now=clock.read?clock.read(clock.owner.get()):glob2::threadCpuNs();
            } catch(...) {invalidate();throw Decline{};}
            if(!now || now<lastCpu){invalidate();throw Decline{};}
            lastCpu=now;return now;
        }
        void invalidate() noexcept {
            if(!invalid)++totals.invalidClocks;
            invalid=true;totals.clockValid=false;totals.requiresFullReservedCharge=true;
        }
        void account(std::uint64_t start,std::uint64_t end) {
            const auto delta=end-start;
            if(delta>UINT64_MAX-totals.optionalCpuNs){invalidate();throw Decline{};}
            totals.optionalCpuNs+=delta;
            if(delta>MaxBlockCpuNs || totals.optionalCpuNs>captureLimit) {
                ++totals.overshoots;declined=true;throw Decline{};
            }
        }
        template<class Work> void block(Work work) {
            const auto start=readCpu();
            try {work();}catch(...) {const auto end=readCpu();account(start,end);throw;}
            const auto end=readCpu();account(start,end);
        }
        void copy(void* destination,const void* source,std::size_t bytes) {
            auto* out=static_cast<std::byte*>(destination);
            const auto* in=static_cast<const std::byte*>(source);
            for(std::size_t offset=0;offset<bytes;) {
                const auto n=std::min(CopyBlockBytes,bytes-offset);
                block([&]{std::memcpy(out+offset,in+offset,n);});
                offset+=n;totals.copiedBytes+=n;++totals.blocks;
            }
        }
        Record(std::shared_ptr<CpuSavingPolicy> value,CpuSavingPolicy::ProbeTicket admitted,
               const Input& input,Clock timer,std::uint64_t floor,std::size_t bytes)
            :policy(std::move(value)),ticket(admitted),clock(std::move(timer)),
             producer(std::this_thread::get_id()),workload(input.key),swimClass(input.swim),
             cap(input.maxCost),deadline(input.due),sessionGeneration(input.requiredGeneration),
             lastCpu(floor),captureLimit(input.captureCpuLimitNs),count(input.original.size()),lease(bytes) {
            totals.reservedBytes=bytes;
        }
    public:
        Record(const Record&)=delete;
        Record& operator=(const Record&)=delete;
        ~Record() {
            // The last escaped immutable record holds admission until borrowed
            // GPU/reference inputs are gone. This first primitive only cancels;
            // a future adapter must reap device events before dropping aliases.
            // Native cleanup accounting also covers destruction of an injected
            // clock's owned context; no callback can inspect a freed context.
            const auto start=glob2::threadCpuNs();
            if(std::this_thread::get_id()!=producer)invalidate();
            originals.reset();golden.reset();waterCells.reset();
            clock.owner.reset();
            releaseOpenCLProbeBytes(lease);lease=0;
            const auto end=glob2::threadCpuNs();
            if(!start || !end || end<start)invalidate();
            else try {account(start,end);}catch(...) {}
            if(!delegatedSettlement)policy->cancelProbe(ticket,totals.optionalCpuNs,!invalid);
        }
        std::span<const std::uint16_t> original() const noexcept {return {originals.get(),count};}
        std::span<const std::uint16_t> expected() const noexcept {return {golden.get(),count};}
        std::span<const std::uint8_t> water() const noexcept {return {waterCells.get(),waterCells?count:0};}
        const WorkloadKey& key() const noexcept {return workload;}
        int swim() const noexcept {return swimClass;}
        int maxCost() const noexcept {return cap;}
        std::uint64_t due() const noexcept {return deadline;}
        std::uint64_t requiredGeneration() const noexcept {return sessionGeneration;}
        Receipt receipt() const noexcept {return totals;}
        // Exactly one explicit worker -> stable background-producer handoff.
        // The caller synchronizes ownership transfer; no simultaneous methods
        // are allowed. Absolute clock floors never cross thread namespaces.
        bool adoptProducer() noexcept {
            if(invalid || declined || handedOff || !totals.referenceBound)return false;
            handedOff=true;
            const auto nextProducer=std::this_thread::get_id();
            if(nextProducer!=producer)lastCpu=0;
            producer=nextProducer;
            try {const auto start=readCpu();account(start,readCpu());return true;}
            catch(...) {invalidate();return false;}
        }
        // Const immutable data can be shared; this lifecycle operation remains
        // on the same producer as allocation/copy/accounting. No dispatch here.
        std::optional<CpuSavingPolicy::ProbeAuthorization> authorize(
            std::uint64_t generation,std::uint64_t tick,std::uint64_t slack,bool backlog) {
            std::optional<CpuSavingPolicy::ProbeAuthorization> result;
            try {block([&]{result=policy->authorizeOptionalExecution(ticket,generation,tick,slack,backlog);});}
            catch(...) {return {};}
            return result;
        }
    };
    // Move-only terminal owner. Capture/probe aliases must be gone before
    // extraction. Storage, policy and global admission survive until cleanup;
    // destruction without complete-cost authorization pays full reserved credit.
    class Settlement {
        friend class GradientCpuCapture;
        std::shared_ptr<Record> record;
        std::shared_ptr<CpuSavingPolicy> policy;
        CpuSavingPolicy::ProbeTicket ticket;
        Receipt totals;
        std::thread::id producer;
        std::size_t lease=0;
        explicit Settlement(std::shared_ptr<Record> value)
            :record(std::move(value)),policy(record->policy),ticket(record->ticket),
             totals(record->totals),producer(record->producer),lease(record->lease) {
            record->delegatedSettlement=true;record->lease=0;
        }
    public:
        Settlement(const Settlement&)=delete;
        Settlement& operator=(const Settlement&)=delete;
        Settlement(Settlement&& other) noexcept
            :record(std::move(other.record)),policy(std::move(other.policy)),ticket(other.ticket),
             totals(other.totals),producer(other.producer),lease(std::exchange(other.lease,0)) {}
        Settlement& operator=(Settlement&&)=delete;
        ~Settlement(){settle({},{});}
        Receipt receipt() const noexcept {return totals;}
        void addObservedCpu(std::uint64_t ns,bool valid) noexcept {
            if(!valid || ns>UINT64_MAX-totals.optionalCpuNs) {
                totals.clockValid=false;totals.requiresFullReservedCharge=true;++totals.invalidClocks;
            } else totals.optionalCpuNs+=ns;
        }
        bool settle(CpuSavingPolicy::ProbeOutcome outcome,
                    CaptureSettlementAuthorization authorization={}) noexcept {
            if(!policy)return false;
            const auto start=glob2::threadCpuNs();
            const bool sameProducer=std::this_thread::get_id()==producer;
            // Record cannot cancel here: this token retains its lease and
            // admission while arrays, callback owner and holder are destroyed.
            record.reset();
            const auto end=glob2::threadCpuNs();
            addObservedCpu(start && end && end>=start?end-start:0,
                sameProducer && start && end && end>=start);
            const bool complete=authorization.permitsInference() && outcome.clockValid &&
                totals.clockValid && !totals.requiresFullReservedCharge &&
                outcome.actualProbeCpuNs>=totals.optionalCpuNs;
            outcome.actualProbeCpuNs=std::max(outcome.actualProbeCpuNs,totals.optionalCpuNs);
            bool promoted=false;
            // All captured storage is now gone. Policy's internal settlement
            // clocks account its own mutation/statistics CPU before publishing.
            releaseOpenCLProbeBytes(lease);lease=0;totals.reservedBytes=0;
            if(complete)promoted=policy->finishAcceptedCapture(ticket,outcome);
            else {totals.requiresFullReservedCharge=true;policy->cancelProbe(ticket,outcome.actualProbeCpuNs,false);}
            policy.reset();return promoted;
        }
    };
    static std::optional<Settlement> takeSettlement(std::shared_ptr<Record>& value) noexcept {
        // An escaped immutable alias may still be a DMA source. Neither its
        // budget nor policy ticket can move away until exclusive ownership.
        if(!value || value.use_count()!=1 || !value->totals.referenceBound)return {};
        return Settlement(std::exchange(value,{}));
    }
private:
    std::shared_ptr<Record> record;
    explicit GradientCpuCapture(std::shared_ptr<Record> value):record(std::move(value)) {}
public:
    GradientCpuCapture(const GradientCpuCapture&)=delete;
    GradientCpuCapture& operator=(const GradientCpuCapture&)=delete;
    // policy is retained and conservatively charged in each record; sharing a
    // policy or clock owner does not reduce this capture's tracked reservation.
    static std::unique_ptr<GradientCpuCapture> begin(std::shared_ptr<CpuSavingPolicy> policy,
        const Input& input,Receipt& receipt,Clock clock) noexcept {
        receipt={};
        if(!policy || input.modifiedCosts || input.swim<0 || input.swim>=int(std::size(WATER_STEP)) ||
            input.key.width==0 || input.key.height==0 || input.key.batch!=1 ||
           input.key.width>unsigned(INT_MAX) || input.key.height>unsigned(INT_MAX) ||
           input.key.cpuBuckets!=BUCKETS || input.key.movementModifiers ||
           input.key.limit!=unsigned(std::min(input.maxCost,COST_LIMIT)) || input.maxCost<0 ||
           input.original.size()!=std::size_t(input.key.width)*input.key.height ||
           input.original.size()>std::size_t(INT_MAX) ||
           !input.requiredGeneration || !input.reservedCpuNs || !input.captureCpuLimitNs ||
           input.captureCpuLimitNs>=input.reservedCpuNs ||
           (clock.read && (!clock.owner || !clock.retainedBytes)) ||
           (!clock.read && (clock.owner || clock.retainedBytes)))return {};
        const bool weighted=weightedClass(input.swim);
        if(weighted && input.water.size()!=input.original.size() &&
           !(input.water.empty() && input.waterAt && input.waterContext))return {};
        // Fixed arrays avoid vector capacity guesses and full object-graph
        // retention. Charge policy, holder and conservative control overhead.
        constexpr auto fixed=sizeof(Record)+sizeof(GradientCpuCapture)+sizeof(CpuSavingPolicy)+512;
        const auto perCell=2*sizeof(std::uint16_t)+(weighted?sizeof(std::uint8_t):0);
        if(clock.retainedBytes>OpenCLProbeBudget-fixed ||
           input.original.size()>(OpenCLProbeBudget-fixed-clock.retainedBytes)/perCell)return {};
        const auto bytes=fixed+clock.retainedBytes+input.original.size()*perCell;
        std::optional<CpuSavingPolicy::ProbeTicket> ticket;
        std::shared_ptr<Record> owned;
        bool reserved=false;
        std::uint64_t start=0,end=0;
        try {
            start=clock.read?clock.read(clock.owner.get()):glob2::threadCpuNs();
            if(!start)throw Record::Decline{};
            ticket=policy->beginAcceptedCapture(input.key,input.alternative,input.tick,input.reservedCpuNs,
                input.slackNs,input.conservativeElapsedNs,input.backlog,input.commonPreparationCpuNs,
                input.requiredGeneration,input.alternativeReady);
            end=clock.read?clock.read(clock.owner.get()):glob2::threadCpuNs();
            if(!end || end<start)throw Record::Decline{};
            receipt.optionalCpuNs=end-start;
            if(!ticket)return {};
            if(!reserveOpenCLProbeBytes(bytes))throw Record::Decline{};
            reserved=true;
            // Originals, golden and cost storage all reserved before allocation.
            std::unique_ptr<Record> allocation(new Record(policy,*ticket,input,clock,end,bytes));
            reserved=false;
            owned=std::shared_ptr<Record>(std::move(allocation));
            owned->totals.optionalCpuNs=receipt.optionalCpuNs;
            owned->block([&]{
                owned->originals.reset(new std::uint16_t[input.original.size()]);
                owned->golden.reset(new std::uint16_t[input.original.size()]);
                if(weighted)owned->waterCells.reset(new std::uint8_t[input.original.size()]);
            });
            owned->copy(owned->originals.get(),input.original.data(),input.original.size_bytes());
            if(weighted) {
                if(!input.water.empty())owned->copy(owned->waterCells.get(),input.water.data(),input.water.size_bytes());
                else for(std::size_t offset=0;offset<input.original.size();) {
                    const auto n=std::min(CopyBlockBytes,input.original.size()-offset);
                    owned->block([&]{for(std::size_t i=0;i<n;++i)
                        owned->waterCells[offset+i]=input.waterAt(input.waterContext,offset+i);});
                    offset+=n;owned->totals.copiedBytes+=n;++owned->totals.blocks;
                }
            }
            std::unique_ptr<GradientCpuCapture> result;
            owned->block([&]{result.reset(new GradientCpuCapture(owned));});
            // Include admission, reservations, holder/control allocation and
            // clock/bookkeeping gaps. Block deltas only enforce chunk bounds.
            end=owned->readCpu();owned->totals.optionalCpuNs=end-start;
            if(owned->totals.optionalCpuNs>owned->captureLimit) {
                ++owned->totals.overshoots;owned->declined=true;throw Record::Decline{};
            }
            receipt=owned->totals;return result;
        }catch(...) {
            if(owned){
                if(!owned->invalid && owned->lastCpu>=start)owned->totals.optionalCpuNs=owned->lastCpu-start;
                receipt=owned->totals;owned.reset();
            }
            else {
                // An allocation/control-construction failure may occur after
                // the last measured endpoint. Its cost cannot be declared zero.
                if(!start || !end || end<start) {
                    receipt.clockValid=false;receipt.requiresFullReservedCharge=true;++receipt.invalidClocks;
                }
                if(ticket)receipt.requiresFullReservedCharge=true;
                if(reserved)releaseOpenCLProbeBytes(bytes);
                if(ticket)policy->cancelProbe(*ticket,receipt.optionalCpuNs,
                    receipt.clockValid && !receipt.requiresFullReservedCharge);
            }
            return {};
        }
    }
    static std::unique_ptr<GradientCpuCapture> begin(std::shared_ptr<CpuSavingPolicy> policy,
        const Input& input,Receipt& receipt) noexcept {
        return begin(std::move(policy),input,receipt,Clock{});
    }
    // Called only after required CPU propagation, before Job.data can publish
    // or be reused. Reference is propagation alone: preparation and capture
    // never enter it. Gold is cloned before policy binds the reference.
    std::shared_ptr<Record> complete(std::span<const std::uint16_t> result,
        const RequiredReceipt& required,Receipt& receipt) noexcept {
        if(!record)return {};
        const auto before=record->totals.optionalCpuNs;
        std::uint64_t started=0;
        try {
            if(result.size()!=record->count || !required.successful || !required.clockValid ||
               !required.propagationCpuNs || required.requiredGeneration!=record->sessionGeneration)
                throw Record::Decline{};
            started=record->readCpu();
            record->copy(record->golden.get(),result.data(),result.size_bytes());
            bool bound=false;
            record->block([&]{bound=record->policy->bindAcceptedReference(record->ticket,
                required.requiredGeneration,required.propagationCpuNs,true,true);});
            if(!bound)throw Record::Decline{};
            const auto ended=record->readCpu();
            if(ended-started>UINT64_MAX-before) {record->invalidate();throw Record::Decline{};}
            record->totals.optionalCpuNs=before+ended-started;
            if(record->totals.optionalCpuNs>record->captureLimit) {
                ++record->totals.overshoots;record->declined=true;throw Record::Decline{};
            }
            record->totals.referenceBound=true;receipt=record->totals;
            return std::exchange(record,{});
        }catch(...) {
            if(started && !record->invalid && record->lastCpu>=started)
                record->totals.optionalCpuNs=before+record->lastCpu-started;
            if(!required.clockValid || !required.propagationCpuNs)record->invalidate();
            receipt=record->totals;record.reset();return {};
        }
    }
};
}
