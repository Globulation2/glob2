// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientCpuCapture.h"
#include "OpenCLChargedStorage.h"
#include <mutex>

namespace gradient_kernel
{
class CaptureBatchProbeReplay;
// Optional worker capture only. A group owns one reservation before the first
// clone; required fields never wait for its membership, replay or settlement.
class GradientCpuCaptureGroup
{
public:
    using Receipt=GradientCpuCapture::Receipt;
    using RequiredReceipt=GradientCpuCapture::RequiredReceipt;
    // An injected clock must be thread-safe if members use separate workers.
    using Clock=GradientCpuCapture::Clock;
    struct Input {
        std::span<const std::uint16_t> original;
        WorkloadKey key; // singleton key; only scalar classic Clear/Guard
        std::weak_ptr<const void> sourceIdentity;
        std::size_t weakSourceBytes=0; // complete retained weak control/coallocation
        std::uint64_t revision=0,variant=0,tick=0,due=0,optionalExpiry=0,requiredGeneration=0;
        std::uint64_t commonPreparationCpuNs=0;
        unsigned maximumMembers=8;
        bool scalarClassic=false,alternativeReady=false,backlog=false;
        Plan alternative=Plan::Frozen8;
        std::uint64_t reservedCpuNs=0,captureCpuLimitNs=0,slackNs=0,conservativeElapsedNs=0;
    };
    class Member;
    class Settlement;
    class Record {
        friend class GradientCpuCaptureGroup;
        friend class Member;
        friend class Settlement;
        friend class CaptureBatchProbeReplay;
        // Credit is destroyed last, after every owned payload and policy alias.
        std::shared_ptr<OpenCLProbeBatchStorage> futureStorage;
        std::shared_ptr<PrechargedProbeStorage> storageCredit;
        std::size_t rawLease=0;
        std::shared_ptr<CpuSavingPolicy> policy;
        CpuSavingPolicy::ProbeTicket ticket;
        std::weak_ptr<const void> source;
        Clock clock;
        WorkloadKey workload;
        std::uint64_t costRevision=0,costVariant=0,generation=0,reservedCpu=0,captureLimit=0;
        std::atomic<std::uint64_t> expiry{0};
        unsigned capacity=0,selectedCount=0;
        std::array<unsigned,8> selected{};
        enum class Status {Empty,Copying,Required,Complete,Canceled};
        struct Slot {
            std::unique_ptr<std::uint16_t[]> original,golden;
            std::atomic<Status> status{Status::Empty};
            std::uint64_t tick=0,due=0,reference=0,preparation=0;
        };
        std::array<Slot,8> slots;
        std::size_t cells=0;
        mutable std::mutex mutex;
        Receipt totals;
        std::uint64_t backgroundCpu=0; // Stable coordinator namespace only.
        std::atomic<unsigned> workers{0};
        std::atomic<bool> closed{false},canceled{false};
        bool bound=false,handedOff=false,delegatedSettlement=false,payloadCleaned=false;
        std::thread::id producer=std::this_thread::get_id();
        std::uint64_t backgroundFloor=0;
        explicit Record(std::shared_ptr<CpuSavingPolicy> p,CpuSavingPolicy::ProbeTicket t,
            const Input& input,std::size_t bytes,Clock timer):rawLease(bytes),policy(std::move(p)),ticket(t),source(input.sourceIdentity),clock(std::move(timer)),
            workload(input.key),costRevision(input.revision),costVariant(input.variant),generation(input.requiredGeneration),
            reservedCpu(input.reservedCpuNs),captureLimit(input.captureCpuLimitNs),expiry(input.optionalExpiry),
            capacity(input.maximumMembers),cells(input.original.size()) {totals.reservedBytes=bytes;}
        std::uint64_t readClock() noexcept {
            try {return clock.read?clock.read(clock.owner.get()):glob2::threadCpuNs();}
            catch(...) {return 0;}
        }
        void account(std::uint64_t start,std::uint64_t end,bool oversized=false,bool background=false) noexcept {
            std::lock_guard lock(mutex);
            if(!start || !end || end<start || end-start>UINT64_MAX-totals.optionalCpuNs) {
                totals.clockValid=false;totals.requiresFullReservedCharge=true;++totals.invalidClocks;canceled=true;return;
            }
            totals.optionalCpuNs+=end-start;
            if(background)backgroundCpu+=end-start; // Bounded by the total above.
            if(oversized || end-start>GradientCpuCapture::MaxBlockCpuNs || totals.optionalCpuNs>captureLimit) {
                ++totals.overshoots;totals.requiresFullReservedCharge=true;canceled=true;
            }
        }
        bool same(const Input& input) const noexcept {
            return input.scalarClassic && input.key==workload && input.original.size()==cells &&
                input.requiredGeneration==generation && input.revision==costRevision && input.variant==costVariant &&
                !source.owner_before(input.sourceIdentity) && !input.sourceIdentity.owner_before(source);
        }
        bool backgroundClock(std::uint64_t& now) noexcept {
            now=readClock();
            if(producer!=std::this_thread::get_id() || !now || now<backgroundFloor) {
                account(0,0);return false;
            }
            backgroundFloor=now;return true;
        }
    private:
        // Exclusive terminal cleanup. Keep the receipt and absolute producer
        // floor inspectable until the token freezes all destructor-side flags.
        void cleanupPayloads() noexcept {
            if(payloadCleaned)return;payloadCleaned=true;
            std::uint64_t start=0,end=0;backgroundClock(start);
            for(auto& slot:slots){slot.original.reset();slot.golden.reset();}
            source.reset();backgroundClock(end);account(start,end,false,true);
        }
    public:
        ~Record() {
            cleanupPayloads();
            if(!delegatedSettlement)policy->cancelProbe(ticket,receipt().optionalCpuNs,
                receipt().clockValid && !receipt().requiresFullReservedCharge);
            releaseOpenCLProbeBytes(rawLease);
        }
        unsigned count() const noexcept {return selectedCount;}
        std::span<const std::uint16_t> original(unsigned i) const noexcept {
            return i<selectedCount?std::span<const std::uint16_t>(slots[selected[i]].original.get(),cells):std::span<const std::uint16_t>{};
        }
        std::span<const std::uint16_t> expected(unsigned i) const noexcept {
            return i<selectedCount?std::span<const std::uint16_t>(slots[selected[i]].golden.get(),cells):std::span<const std::uint16_t>{};
        }
        std::uint64_t publicationDue(unsigned i) const noexcept {return i<selectedCount?slots[selected[i]].due:0;}
        const WorkloadKey& key() const noexcept {return workload;}
        int maxCost() const noexcept {return int(workload.limit);}
        int swim() const noexcept {return int(workload.movement);}
        std::uint64_t due() const noexcept {return expiry.load(std::memory_order_relaxed);}
        std::uint64_t requiredGeneration() const noexcept {return generation;}
        CpuSavingPolicy::ProbeTicket probeTicket() const noexcept {return ticket;}
        std::uint64_t reservedCpuNs() const noexcept {return reservedCpu;}
        Receipt receipt() const noexcept {std::lock_guard lock(mutex);return totals;}
        // An enclosing replay bracket may subtract only this producer's owned
        // work, never concurrent late members' aggregate optional CPU.
        Receipt backgroundCallingReceipt() const noexcept {
            std::lock_guard lock(mutex);auto value=totals;value.optionalCpuNs=backgroundCpu;return value;
        }
        bool workerAliasesGone() const noexcept {return workers.load(std::memory_order_acquire)==0;}
        bool isClosed() const noexcept {return closed.load(std::memory_order_acquire);}
        bool captureValid() const noexcept {
            const auto paid=receipt();return !canceled.load(std::memory_order_acquire) &&
                paid.clockValid && !paid.requiresFullReservedCharge;
        }
        void requestCancel() noexcept {canceled=true;closed=true;}
        // No waiting for an in-progress worker. The broker freezes only complete
        // immutable slots and closes enrollment; late slot expense is still paid.
        bool freezeReady(std::uint64_t currentGeneration) noexcept {
            // This explicit handoff starts a new absolute thread-clock namespace.
            // Worker Member clocks remain independent and pay only their deltas.
            if(handedOff)return false;
            handedOff=true;producer=std::this_thread::get_id();backgroundFloor=0;
            std::uint64_t start=0;if(!backgroundClock(start))return false;
            {std::lock_guard lock(mutex);closed=true;}
            if(bound || canceled || currentGeneration!=generation){std::uint64_t end=0;backgroundClock(end);account(start,end,false,true);return false;}
            std::array<unsigned,8> ready{};unsigned n=0;
            for(unsigned i=0;i<capacity;++i)if(slots[i].status.load(std::memory_order_acquire)==Status::Complete)ready[n++]=i;
            unsigned take=n>=8?8:n>=4?4:n>=2?2:n>=1?1:0;
            std::uint64_t reference=0,preparation=0;
            for(unsigned i=0;i<take;++i) {
                const auto& slot=slots[ready[i]];
                if(slot.reference>UINT64_MAX-reference || slot.preparation>UINT64_MAX-preparation){account(0,0);return false;}
                reference+=slot.reference;preparation+=slot.preparation;
            }
            auto key=workload;key.batch=take;
            const auto measured=receipt();
            bool valid=false;
            try {valid=take && policy->bindReadyBatch(ticket,key,generation,reference,preparation,true,
                measured.clockValid && !measured.requiresFullReservedCharge);}catch(...) {account(0,0);}
            if(valid){selected=ready;selectedCount=take;workload=key;bound=true;
                std::lock_guard lock(mutex);totals.referenceBound=true;}
            else canceled=true;
            std::uint64_t end=0;backgroundClock(end);account(start,end,false,true);return valid && !canceled;
        }
        bool adoptProducer() noexcept {
            if(!handedOff || !bound || canceled || producer!=std::this_thread::get_id())return false;
            std::uint64_t now=0;return backgroundClock(now);
        }
        std::optional<CpuSavingPolicy::ProbeAuthorization> authorize(std::uint64_t currentGeneration,
            std::uint64_t tick,std::uint64_t slack,bool backlog) noexcept {
            std::uint64_t start=0,end=0;if(!backgroundClock(start))return {};
            std::optional<CpuSavingPolicy::ProbeAuthorization> result;
            try {result=policy->authorizeOptionalExecution(ticket,currentGeneration,tick,slack,backlog);}catch(...) {account(0,0);}
            if(!backgroundClock(end))return {};account(start,end,false,true);return canceled?std::optional<CpuSavingPolicy::ProbeAuthorization>{}:result;
        }
    };
    class Member {
        friend class GradientCpuCaptureGroup;
        std::shared_ptr<Record> record;
        unsigned index=0;
        std::thread::id producer=std::this_thread::get_id();
        std::uint64_t floor=0,paidCopyCpu=0,paidCallingCpu=0;
        bool callingClockValid=true;
        explicit Member(std::shared_ptr<Record> value,unsigned i):record(std::move(value)),index(i) {}
        bool now(std::uint64_t& value) noexcept {
            value=record->readClock();
            if(producer!=std::this_thread::get_id() || !value || value<floor){callingClockValid=false;record->account(0,0);return false;}
            floor=value;return true;
        }
        void accountOuter(std::uint64_t start,std::uint64_t copiedBefore) noexcept {
            std::uint64_t end=0;
            if(!now(end) || !start || end<start || paidCopyCpu<copiedBefore || paidCopyCpu-copiedBefore>end-start){callingClockValid=false;record->account(0,0);return;}
            const auto disjoint=end-start-(paidCopyCpu-copiedBefore);
            paidCallingCpu+=disjoint;record->account(start,start+disjoint);
        }
        bool copy(std::uint16_t* target,std::span<const std::uint16_t> source) noexcept {
            constexpr auto cellsPerBlock=GradientCpuCapture::CopyBlockBytes/sizeof(std::uint16_t);
            for(std::size_t offset=0;offset<source.size();) {
                std::uint64_t start=0,end=0;
                if(record->canceled || record->closed || !now(start))return false;
                const auto n=std::min(cellsPerBlock,source.size()-offset);
                std::memcpy(target+offset,source.data()+offset,n*sizeof(std::uint16_t));
                if(!now(end))return false;record->account(start,end);paidCopyCpu+=end-start;paidCallingCpu+=end-start;
                {std::lock_guard lock(record->mutex);++record->totals.blocks;record->totals.copiedBytes+=n*sizeof(std::uint16_t);}
                offset+=n;
            }
            return !record->canceled && !record->closed;
        }
    public:
        ~Member() {
            if(!record)return;
            std::uint64_t start=0,end=0;now(start);
            auto& slot=record->slots[index];
            if(slot.status.load()!=Record::Status::Complete)slot.status=Record::Status::Canceled;
            now(end);record->account(start,end);
            record->workers.fetch_sub(1,std::memory_order_release);
        }
        Member(const Member&)=delete;
        // Same worker only. Other members can update the aggregate concurrently;
        // an outer worker bracket must subtract only this member's paid receipt.
        Receipt callingReceipt() const noexcept {
            Receipt value;value.optionalCpuNs=paidCallingCpu;value.clockValid=callingClockValid;
            value.requiresFullReservedCharge=!callingClockValid;return value;
        }
        bool complete(std::span<const std::uint16_t> golden,const RequiredReceipt& required,Receipt& out) noexcept {
            std::uint64_t start=0;if(!now(start)){out=record->receipt();return false;}
            const auto copiedBefore=paidCopyCpu;
            auto& slot=record->slots[index];
            const bool valid=!record->closed && !record->canceled && golden.size()==record->cells &&
                required.successful && required.clockValid && required.propagationCpuNs && required.requiredGeneration==record->generation;
            if(!valid || !copy(slot.golden.get(),golden)) {
                slot.status=Record::Status::Canceled;
                if(!required.clockValid || !required.successful || !required.propagationCpuNs || required.requiredGeneration!=record->generation){record->account(0,0);record->requestCancel();}
                accountOuter(start,copiedBefore);out=record->receipt();return false;
            }
            slot.reference=required.propagationCpuNs;
            accountOuter(start,copiedBefore);
            slot.status.store(Record::Status::Complete,std::memory_order_release);
            out=record->receipt();return true;
        }
    };
    static std::size_t retainedBytes(std::size_t cells,unsigned capacity,std::size_t weakBytes,std::size_t clockBytes=0) noexcept {
        constexpr auto fixed=sizeof(Record)+sizeof(Settlement)+sizeof(CpuSavingPolicy)+1024;
        const auto base=fixed+PrechargedProbeStorage::metadataBytes();
        if(weakBytes>OpenCLProbeBudget-base || clockBytes>OpenCLProbeBudget-base-weakBytes || capacity>8 || !capacity)return 0;
        const auto overhead=base+weakBytes+clockBytes+capacity*(sizeof(Member)+128);
        if(overhead>OpenCLProbeBudget || cells>(OpenCLProbeBudget-overhead)/(capacity*4))return 0;
        return overhead+cells*capacity*4;
    }
    // Production admission includes the complete native replay/accounting/device
    // promise before any original or golden array is allocated or copied.
    static unsigned nativeCapacity(const Input& input,std::size_t clockBytes=0) noexcept {
        if(!input.key.width || !input.key.height || input.key.width>unsigned(INT_MAX) ||
           input.key.height>unsigned(INT_MAX) || input.key.width>unsigned(INT_MAX)/input.key.height ||
           input.original.size()!=std::size_t(input.key.width)*input.key.height)return 0;
        for(const unsigned capacity:{8u,4u,2u,1u})if(capacity<=input.maximumMembers) {
            const auto capture=retainedBytes(input.original.size(),capacity,input.weakSourceBytes,clockBytes);
            const auto future=OpenCLProbeBatchStorage::estimate({int(input.key.width),int(input.key.height)},capacity,input.alternative);
            if(capture && future && future.hostBytes<=OpenCLProbeBudget-capture)return capacity;
        }
        return 0;
    }
    static std::shared_ptr<Record> beginNative(std::shared_ptr<CpuSavingPolicy> policy,const Input& input,Receipt& receipt,Clock timer={}) noexcept {
        return beginOwned(std::move(policy),input,receipt,std::move(timer),true);
    }
    // Independent group ownership/oracle tests use no driver admission. A real
    // native batch adapter requires futureStorage and declines this form.
    static std::shared_ptr<Record> begin(std::shared_ptr<CpuSavingPolicy> policy,const Input& input,Receipt& receipt,Clock timer={}) noexcept {
        return beginOwned(std::move(policy),input,receipt,std::move(timer),false);
    }
private:
    static std::shared_ptr<Record> beginOwned(std::shared_ptr<CpuSavingPolicy> policy,const Input& input,Receipt& receipt,Clock timer,bool native) noexcept {
        receipt={};
        if(!policy || !input.scalarClassic || input.key.batch!=1 || input.key.movementModifiers ||
           (input.key.family!=Family::Clear && input.key.family!=Family::Guard) || input.key.cpuBuckets!=BUCKETS ||
           input.key.width==0 || input.key.height==0 || input.key.width>unsigned(INT_MAX) || input.key.height>unsigned(INT_MAX) ||
           input.key.width>unsigned(INT_MAX)/input.key.height ||
           input.original.size()!=std::size_t(input.key.width)*input.key.height || input.original.size()>std::size_t(INT_MAX) ||
           input.key.limit!=COST_LIMIT || input.key.movement>=std::size(WATER_STEP) || weightedClass(int(input.key.movement)) ||
           input.sourceIdentity.expired() || !input.weakSourceBytes || !input.requiredGeneration ||
           input.due<=input.tick || input.optionalExpiry<=input.tick || !input.reservedCpuNs || !input.captureCpuLimitNs ||
           input.captureCpuLimitNs>=input.reservedCpuNs ||
           (timer.read && (!timer.owner || !timer.retainedBytes)) || (!timer.read && (timer.owner || timer.retainedBytes)) ||
           (input.maximumMembers!=1 && input.maximumMembers!=2 && input.maximumMembers!=4 && input.maximumMembers!=8))return {};
        auto maximum=input.key;maximum.batch=input.maximumMembers;
        const auto bytes=retainedBytes(input.original.size(),input.maximumMembers,input.weakSourceBytes,timer.retainedBytes);if(!bytes)return {};
        const auto future=native?OpenCLProbeBatchStorage::estimate({int(input.key.width),int(input.key.height)},input.maximumMembers,input.alternative):OpenCLProbeBatchStorage::Estimate{};
        if(native && (!future || future.hostBytes>OpenCLProbeBudget-bytes))return {};
        const auto totalBytes=bytes+future.hostBytes;
        auto floor=std::uint64_t(0);
        auto read=[&]() noexcept {std::uint64_t value=0;try {value=timer.read?timer.read(timer.owner.get()):glob2::threadCpuNs();}catch(...) {}
            if(!value || value<floor)return std::uint64_t(0);floor=value;return value;};
        const auto start=read();if(!start){receipt.clockValid=false;receipt.requiresFullReservedCharge=true;return {};}
        std::optional<CpuSavingPolicy::ProbeTicket> ticket;std::shared_ptr<Record> record;
        struct Return {std::size_t bytes=0;~Return(){if(bytes)releaseOpenCLProbeBytes(bytes);}} futureReservation;
        bool reserved=false;
        try {
            ticket=policy->beginAcceptedBatchCapture(maximum,input.alternative,input.tick,input.reservedCpuNs,
                input.slackNs,input.conservativeElapsedNs,input.backlog,input.requiredGeneration,input.alternativeReady);
            if(!ticket)return {};
            if(!reserveOpenCLProbeBytes(totalBytes))throw 0;reserved=true;futureReservation.bytes=future.hostBytes;
            auto futureOwner=native?OpenCLProbeBatchStorage::adopt(futureReservation.bytes,future):std::shared_ptr<OpenCLProbeBatchStorage>{};
            if(native && !futureOwner)throw 0;
            auto allocation=std::unique_ptr<Record>(new Record(policy,*ticket,input,bytes,timer));reserved=false;
            allocation->futureStorage=std::move(futureOwner);allocation->totals.reservedBytes=totalBytes;
            allocation->storageCredit=PrechargedProbeStorage::adopt(allocation->rawLease);
            const auto credit=allocation->storageCredit;
            record=std::shared_ptr<Record>(allocation.release(),[credit](Record* value){delete value;});
            for(unsigned i=0;i<input.maximumMembers;++i) {
                const auto allocationStart=read();if(!allocationStart)throw 0;
                record->slots[i].original.reset(new std::uint16_t[input.original.size()]);
                record->slots[i].golden.reset(new std::uint16_t[input.original.size()]);
                const auto allocationEnd=read();
                if(!allocationStart || !allocationEnd || allocationEnd<allocationStart ||
                   allocationEnd-allocationStart>GradientCpuCapture::MaxBlockCpuNs)throw 0;
            }
            const auto ended=read();record->backgroundFloor=floor;record->account(start,ended);receipt=record->receipt();
            if(record->canceled)throw 0;
            return record;
        }catch(...) {
            if(record){record->account(0,0);receipt=record->receipt();record.reset();}
            else {
                if(reserved)releaseOpenCLProbeBytes(bytes);
                receipt.clockValid=false;receipt.requiresFullReservedCharge=true;
                if(ticket)policy->cancelProbe(*ticket,input.reservedCpuNs,false);
            }
            return {};
        }
    }
public:
    static std::unique_ptr<Member> enroll(const std::shared_ptr<Record>& record,const Input& input,Receipt& receipt) noexcept {
        if(!record || input.due<=input.tick || input.optionalExpiry<=input.tick)return {};
        const auto started=record->readClock();unsigned index=record->capacity;
        {std::lock_guard lock(record->mutex);
            if(record->closed || record->canceled || !record->same(input))return {};
            for(unsigned i=0;i<record->capacity;++i)if(record->slots[i].status==Record::Status::Empty) {
                index=i;record->slots[i].status=Record::Status::Copying;record->slots[i].tick=input.tick;
                record->slots[i].due=input.due;record->slots[i].preparation=input.commonPreparationCpuNs;
                record->expiry.store(std::min(record->due(),input.optionalExpiry),std::memory_order_relaxed);
                record->workers.fetch_add(1,std::memory_order_relaxed);break;
            }
        }
        if(index==record->capacity)return {};
        std::unique_ptr<Member> member;
        try {member.reset(new Member(record,index));}
        catch(...) {record->slots[index].status=Record::Status::Canceled;record->workers.fetch_sub(1,std::memory_order_release);record->account(0,0);return {};}
        member->floor=started;
        if(!started || !member->copy(record->slots[index].original.get(),input.original)){member.reset();receipt=record->receipt();return {};}
        record->slots[index].status.store(Record::Status::Required,std::memory_order_release);
        member->accountOuter(started,0);receipt=record->receipt();return member;
    }
    class Settlement {
        friend class GradientCpuCaptureGroup;
        std::shared_ptr<OpenCLProbeBatchStorage> future;
        std::shared_ptr<PrechargedProbeStorage> credit;
        std::shared_ptr<Record> record;
        std::shared_ptr<CpuSavingPolicy> policy;
        CpuSavingPolicy::ProbeTicket ticket;
        Receipt totals;
        std::uint64_t generation=0,reservedCpu=0;
        std::thread::id producer=std::this_thread::get_id();
        explicit Settlement(std::shared_ptr<Record> value):future(value->futureStorage),credit(value->storageCredit),record(std::move(value)),
            policy(record->policy),ticket(record->ticket),totals(record->receipt()),generation(record->generation),reservedCpu(record->reservedCpu) {
            record->delegatedSettlement=true;
        }
    public:
        Settlement(const Settlement&)=delete;
        Settlement& operator=(const Settlement&)=delete;
        Settlement(Settlement&&)=default;
        Settlement& operator=(Settlement&&)=delete;
        ~Settlement(){settle({},{});}
        Receipt receipt() const noexcept {return totals;}
        bool cleanupComplete() const noexcept {return !record;}
        std::uint64_t requiredGeneration() const noexcept {return generation;}
        CpuSavingPolicy::ProbeTicket probeTicket() const noexcept {return ticket;}
        std::uint64_t reservedCpuNs() const noexcept {return reservedCpu;}
        void addObservedCpu(std::uint64_t ns,bool valid=true) noexcept {
            if(!valid || ns>UINT64_MAX-totals.optionalCpuNs){totals.clockValid=false;totals.requiresFullReservedCharge=true;return;}
            totals.optionalCpuNs+=ns;
        }
        Receipt cleanup() noexcept {
            if(record) {
                auto timer=record->clock;const auto beforeRecord=record->receipt();std::uint64_t start=0;
                const bool startingValid=record->backgroundClock(start);
                record->cleanupPayloads();
                const auto updated=record->receipt();
                const auto floor=record->backgroundFloor;
                const bool recordValid=record->captureValid();
                const bool deltaValid=updated.optionalCpuNs>=beforeRecord.optionalCpuNs &&
                    updated.invalidClocks>=beforeRecord.invalidClocks && updated.overshoots>=beforeRecord.overshoots;
                const auto recordedDelta=deltaValid?updated.optionalCpuNs-beforeRecord.optionalCpuNs:0;
                addObservedCpu(recordedDelta,deltaValid && updated.clockValid);
                totals.requiresFullReservedCharge=totals.requiresFullReservedCharge || updated.requiresFullReservedCharge || !recordValid;
                if(deltaValid){totals.invalidClocks+=updated.invalidClocks-beforeRecord.invalidClocks;
                    totals.overshoots+=updated.overshoots-beforeRecord.overshoots;}
                record.reset();
                std::uint64_t end=0;try {end=timer.read?timer.read(timer.owner.get()):glob2::threadCpuNs();}catch(...) {}
                const bool valid=startingValid && producer==std::this_thread::get_id() &&
                    start && end && end>=start && end>=floor && deltaValid && recordedDelta<=end-start;
                if(valid) {
                    addObservedCpu(end-start-recordedDelta);
                    if(end-start>GradientCpuCapture::MaxBlockCpuNs) {
                        ++totals.overshoots;totals.requiresFullReservedCharge=true;
                    }
                }else addObservedCpu(0,false);
            }
            return totals;
        }
        bool settle(CpuSavingPolicy::ProbeOutcome outcome,CaptureSettlementAuthorization authorization={}) noexcept {
            if(!policy)return false;
            cleanup();
            const bool complete=producer==std::this_thread::get_id() && authorization.permitsInference() && authorization.ticketId==ticket.id &&
                authorization.policyGeneration==ticket.generation && authorization.requiredGeneration==generation &&
                outcome.actualProbeCpuNs>=authorization.minimumCpuNs && outcome.alternativeCpuNs>=authorization.minimumAlternativeCpuNs &&
                outcome.clockValid && totals.clockValid && !totals.requiresFullReservedCharge && outcome.actualProbeCpuNs>=totals.optionalCpuNs;
            outcome.actualProbeCpuNs=std::max(outcome.actualProbeCpuNs,totals.optionalCpuNs);
            bool promoted=false;
            if(complete)promoted=policy->finishAcceptedCapture(ticket,outcome);
            else policy->cancelProbe(ticket,outcome.actualProbeCpuNs,false);
            policy.reset();credit.reset();future.reset();return promoted;
        }
    };
    static std::optional<Settlement> takeSettlement(std::shared_ptr<Record>& record) noexcept {
        if(!record || record.use_count()!=1 || !record->workerAliasesGone())return {};
        return Settlement(std::exchange(record,{}));
    }
};
}
