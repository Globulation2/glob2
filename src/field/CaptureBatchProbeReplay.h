// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CaptureProbeReplay.h"
#include "GradientCpuCaptureGroup.h"
#include <chrono>

namespace gradient_kernel
{
// Background batch adapter. One producer owns every operation except
// cancel(). Optional device/compiler initialization is never performed here.
class CaptureBatchProbeReplay
{
public:
    using Progress=CaptureProbeReplay::Progress;
    using Admission=CaptureProbeReplay::Admission;
    using Receipt=CaptureProbeReplay::Receipt;
    using Clock=CaptureProbeReplay::Clock;
    using SetupDecline=CaptureProbeReplay::SetupDecline;
    using SetupStage=CaptureProbeReplay::SetupStage;
    // Injected backends exercise this adapter's actual ownership/comparator
    // without loading a driver. They are diagnostic-only and never qualify.
    class Probe {
    public:
        virtual ~Probe()=default;
        virtual OpenCLProbeProgress advance() noexcept=0;
        virtual void cancel() noexcept=0;
        // False for ordinary cancellation, setup pressure and unsupported costs.
        virtual bool executionFailed() const noexcept {return false;}
        // Unknown test adapters conservatively retain the event polling delay.
        virtual bool waitingForEvent() const noexcept {return true;}
        virtual bool metrics(OpenCLProbe::Metrics&) const noexcept {return false;}
        virtual std::span<const std::uint16_t> result(unsigned index) const noexcept=0;
    };
    struct Hooks {
        std::shared_ptr<const void> owner;
        std::size_t retainedBytes=0;
        bool (*ready)(const void*) noexcept=nullptr;
        std::unique_ptr<Probe> (*begin)(std::span<const OpenCLProbeInput>,Plan,std::shared_ptr<const void>,const void*)=nullptr;
        bool (*reap)(const void*) noexcept=nullptr;
    };
private:
    std::shared_ptr<PrechargedProbeStorage> wrapperCredit; // Outlives all wrapper payload.
    struct Lease {
        std::size_t bytes=0;
        ~Lease(){releaseOpenCLProbeBytes(bytes);}
    } wrapperLease; // Last destroyed, after wrapper allocation's payload.
    class NativeProbe final : public Probe {
        std::unique_ptr<OpenCLProbeBatch> probe;
    public:
        explicit NativeProbe(std::unique_ptr<OpenCLProbeBatch> value):probe(std::move(value)) {}
        OpenCLProbeProgress advance() noexcept override {return probe->advance();}
        void cancel() noexcept override {probe->cancel();}
        bool executionFailed() const noexcept override {return probe->metrics().executionFailed;}
        bool waitingForEvent() const noexcept override {return probe->waitingForEvent();}
        bool metrics(OpenCLProbe::Metrics& out) const noexcept override {out=probe->metrics();return true;}
        std::span<const std::uint16_t> result(unsigned index) const noexcept override {return probe->result(index);}
    };
    struct State {
        std::shared_ptr<PrechargedProbeStorage> storageCredit; // Destroyed last.
        Lease lease; // Pins reservation through all retained payload destruction.
        std::shared_ptr<GradientCpuCaptureGroup::Record> record;
        BackendSession session; // Private: no accounting/learning heap or map graph.
        Clock clock;
        Hooks hooks;
        std::thread::id producer=std::this_thread::get_id();
        CpuSavingPolicy::ProbeAuthorization authorization;
        Receipt receipt;
        std::atomic<bool> canceled{false};
        std::uint64_t floor=0;
        std::uint64_t initialCaptureCpuNs=0;
        std::uint64_t nativeBeginStart=0;
        bool setupComplete=false;
        std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
        explicit State(std::shared_ptr<GradientCpuCaptureGroup::Record> captured,Clock timer,Hooks backend,std::size_t bytes)
            :record(std::move(captured)),clock(std::move(timer)),hooks(std::move(backend)) {
            lease.bytes=bytes;session.setExternalInitialization(true);
            receipt.reservedBytes=bytes;receipt.nativeBackend=!hooks.begin;
            receipt.nativeClock=!clock.read;
        }
        ~State() {
            // Premature disposal/quarantine may leave this as the last holder.
            // Captured admission cannot settle until the backend drops every
            // keepAlive alias after positive terminal proof.
            if(record) {
                const auto cleanupStart=glob2::threadCpuNs();
                auto observed=additionalBackgroundCpu();
                bool valid=receipt.clockValid;
                const auto duplicate=record->backgroundCallingReceipt().optionalCpuNs-initialCaptureCpuNs;
                auto settlement=GradientCpuCaptureGroup::takeSettlement(record);
                hooks.owner.reset();clock.owner.reset();
                const auto cleanupEnd=glob2::threadCpuNs();
                if(!setupComplete) {
                    valid=valid && nativeBeginStart && cleanupEnd && cleanupEnd>=nativeBeginStart;
                    if(valid)observed=cleanupEnd-nativeBeginStart>duplicate?cleanupEnd-nativeBeginStart-duplicate:0;
                }
                if(settlement) {
                    settlement->addObservedCpu(observed,valid);
                    if(setupComplete)settlement->addObservedCpu(cleanupStart && cleanupEnd && cleanupEnd>=cleanupStart?
                        cleanupEnd-cleanupStart:0,cleanupStart && cleanupEnd && cleanupEnd>=cleanupStart);
                }
                // Unknown driver/tail coverage forces full credit on destruction.
            }
        }
        std::uint64_t additionalBackgroundCpu() const noexcept {
            const auto current=record?record->backgroundCallingReceipt().optionalCpuNs:initialCaptureCpuNs;
            const auto alsoRecorded=current>=initialCaptureCpuNs?current-initialCaptureCpuNs:0;
            return receipt.backgroundCallingCpuNs>alsoRecorded?receipt.backgroundCallingCpuNs-alsoRecorded:0;
        }
        std::uint64_t now() noexcept {
            if(!receipt.clockValid)return 0;
            std::uint64_t value=0;
            try {
                if(producer!=std::this_thread::get_id())throw 0;
                value=clock.read?clock.read(clock.owner.get()):glob2::threadCpuNs();
            } catch(...) {value=0;}
            if(!value || value<floor) {
                receipt.clockValid=false;++receipt.invalidClocks;canceled=true;return 0;
            }
            floor=value;return value;
        }
        void account(std::uint64_t start,std::uint64_t end,bool alternative=false,bool comparing=false) noexcept {
            if(!start || !end || end<start)return;
            const auto delta=end-start;
            if(delta>UINT64_MAX-receipt.backgroundCallingCpuNs) {
                receipt.clockValid=false;++receipt.invalidClocks;canceled=true;return;
            }
            receipt.backgroundCallingCpuNs+=delta;
            if(alternative)receipt.alternativeCallingCpuNs+=delta;
            if(comparing)receipt.comparisonCpuNs+=delta;
            if(delta>GradientCpuCapture::MaxBlockCpuNs){++receipt.overshoots;canceled=true;}
            // Late workers can pay capture expense concurrently. Use one
            // immutable snapshot so the subtraction cannot underflow between
            // two differently timed reads of the aggregate.
            const auto capturedCpu=record->receipt().optionalCpuNs;
            if(authorization.reservedCpuNs &&
               (capturedCpu>=authorization.reservedCpuNs ||
                receipt.backgroundCallingCpuNs>=authorization.reservedCpuNs-capturedCpu)) {
                receipt.budgetCanceled=true;canceled=true;
            }
        }
        static EntrySteps cost(void* value,std::size_t cell) {
            (void)value;(void)cell;return LAND_STEPS;
        }
    };
    std::shared_ptr<State> state;
    std::unique_ptr<Probe> probe; // Outside State: keepAlive never creates a cycle.
    Progress progress=Progress::Pending;
    Receipt finalReceipt;
    CpuSavingPolicy::ProbeAuthorization finalAuthorization;
    WorkloadKey workload;
    std::uint64_t generation=0,deadline=0;
    bool comparing=false;
    unsigned comparedField=0;
    std::size_t comparedOffset=0;
    explicit CaptureBatchProbeReplay(std::shared_ptr<State> value,std::size_t bytes):state(std::move(value)) {
        wrapperLease.bytes=bytes;
        if(state->record) {workload=state->record->key();generation=state->record->requiredGeneration();deadline=state->record->due();}
    }
    void snapshotProbe() noexcept {
        if(probe && state) {
            OpenCLProbe::Metrics latest;
            if(probe->metrics(latest)) {state->receipt.nativeMetrics=latest;state->receipt.nativeMetricsAvailable=true;}
        }
    }
    void dropProbe() noexcept {snapshotProbe();probe.reset();}
    static bool prepared(const Hooks& hooks) {
        if(hooks.begin)return hooks.ready && hooks.ready(hooks.owner.get());
        const auto status=openCLStatus(); // Status only, no driver/compiler setup.
        return status.available && readyPlans.load(std::memory_order_acquire) &&
            !status.parityBound && !status.directSeedUpload && !status.apiCpuMode;
    }
public:
    // Accounting starts before Replay. This reveals only the real opaque
    // promise already admitted by Group; it cannot manufacture byte credits.
    static std::shared_ptr<OpenCLProbeBatchStorage> accountingStorage(
        const std::shared_ptr<GradientCpuCaptureGroup::Record>& record) noexcept {
        return record?record->futureStorage:std::shared_ptr<OpenCLProbeBatchStorage>{};
    }
    CaptureBatchProbeReplay(const CaptureBatchProbeReplay&)=delete;
    ~CaptureBatchProbeReplay() {
        if(probe){probe->cancel();dropProbe();}
        // Pending native disposal retains State in backend quarantine. Normal
        // disposal reaches State's full-credit cancellation only after aliases.
    }
    void cancel() noexcept {if(state)state->canceled.store(true,std::memory_order_release);}
    Receipt receipt() const noexcept {return state?state->receipt:finalReceipt;}
    std::optional<CpuSavingPolicy::ProbeAuthorization> authorization() const noexcept {
        const auto value=state?state->authorization:finalAuthorization;
        if(!value.reservedCpuNs)return {};return value;
    }
    const WorkloadKey& key() const noexcept {return workload;}
    std::uint64_t requiredGeneration() const noexcept {return generation;}
    std::uint64_t due() const noexcept {return deadline;}
    RequiredCandidateReceipt candidateReceipt() const noexcept {
        const auto r=receipt();
        const auto& native=r.nativeMetrics;
        const bool complete=progress==Progress::ReadyToSettle && r.exact && r.nativeBackend && r.nativeClock &&
            (!state || (state->producer==std::this_thread::get_id() && state->record->workerAliasesGone() &&
                state->record->captureValid() && !state->canceled.load(std::memory_order_acquire))) &&
            r.nativeMetricsAvailable &&
            r.clockValid && !r.invalidClocks && !r.overshoots && !r.executionFailed && !r.quarantined &&
            !r.budgetCanceled && !r.requiredCanceled && !r.explicitCanceled &&
            native.batchFields==workload.batch && native.retiredFields==workload.batch &&
            native.dispatches && native.hostChecks && !native.invalidCpuClocks && !native.overshoots &&
            !native.executionFailed && native.stage==OpenCLProbe::Stage::OutputWait;
        // Missing calibration authenticates only complete-debt inference. It
        // never authorizes omitting oracle, capture or administrative expense.
        const bool calibrated=r.requiredAllowanceQualified && r.requiredOnlyAllowanceCpuNs && r.requiredAllowanceCalibration;
        return RequiredCandidateReceipt(std::max(r.alternativeCallingCpuNs,native.threadCpuNs),
            calibrated?r.requiredOnlyAllowanceCpuNs:0,complete,r.ticketId,r.policyGeneration,r.requiredGeneration,
            calibrated?r.requiredAllowanceCalibration:0);
    }
    // No event queries or allocations. Zero requests another bounded broker
    // turn, with required EDF reconsidered first; it never loops within advance.
    std::uint64_t nextAdvanceDelayNs() const noexcept {
        if(progress==Progress::Quarantined)return 100000000;
        return progress==Progress::Pending && !comparing && probe && probe->waitingForEvent()?100000:0;
    }
    static std::unique_ptr<CaptureBatchProbeReplay> begin(std::shared_ptr<GradientCpuCaptureGroup::Record>&& captured,
        Admission admission,Receipt& attempted,Clock clock,Hooks hooks) noexcept {
        attempted={};
        const auto nativeStart=glob2::threadCpuNs();
        struct Attempt {
            Receipt& out;std::uint64_t start;
            ~Attempt(){const auto end=glob2::threadCpuNs();
                if(start && end && end>=start)out.attemptedCallingCpuNs=end-start;
                else {out.clockValid=false;++out.invalidClocks;}}
        } attempt{attempted,nativeStart};
        auto& diagnostic=attempted.setup;
        diagnostic.tick=admission.tick;diagnostic.slackNs=admission.slackNs;
        diagnostic.admissionGeneration=admission.requiredGeneration;diagnostic.backlog=admission.requiredBacklog;
        if(captured){diagnostic.referenceBound=captured->receipt().referenceBound;diagnostic.count=captured->count();
            diagnostic.due=captured->due();diagnostic.capturedGeneration=captured->requiredGeneration();}
        auto reject=[&](SetupDecline reason){attempted.setupDeclined=true;diagnostic.reason=reason;};
        if(!captured){reject(SetupDecline::MissingCapture);return {};}
        if(!diagnostic.referenceBound){reject(SetupDecline::UnboundReference);return {};}
        if(!captured->count() || captured->count()>8){reject(SetupDecline::InvalidCount);return {};}
        if(!(diagnostic.captureValid=captured->captureValid())){reject(SetupDecline::InvalidCapture);return {};}
        if(!(diagnostic.closed=captured->isClosed())){reject(SetupDecline::OpenEnrollment);return {};}
        if(captured->requiredGeneration()!=admission.requiredGeneration){reject(SetupDecline::Generation);return {};}
        if(admission.requiredBacklog){reject(SetupDecline::RequiredBacklog);return {};}
        if(!admission.slackNs){reject(SetupDecline::NoSlack);return {};}
        if(admission.tick>=captured->due()){reject(SetupDecline::Due);return {};}
        if((clock.read && (!clock.owner || !clock.retainedBytes)) ||
           (!clock.read && (clock.owner || clock.retainedBytes))){reject(SetupDecline::ClockConfiguration);return {};}
        if((hooks.begin && (!hooks.owner || !hooks.retainedBytes || !hooks.ready || !hooks.reap)) ||
           (!hooks.begin && (hooks.owner || hooks.retainedBytes || hooks.ready || hooks.reap))){reject(SetupDecline::HooksConfiguration);return {};}
        constexpr std::size_t fixed=sizeof(State)+sizeof(NativeProbe)+512+PrechargedProbeStorage::metadataBytes();
        constexpr std::size_t wrapperBytes=sizeof(CaptureBatchProbeReplay)+256+PrechargedProbeStorage::metadataBytes();
        // Remaining fixed adapter allowance is independent of native arrays and
        // of Accounting's checked allowance. Extra diagnostic owners are explicit.
        static_assert(fixed+wrapperBytes+PrechargedProbeCostOwner::metadataBytes()+
            8*sizeof(OpenCLProbeInput)<16*1024);
        if(clock.retainedBytes>OpenCLProbeBudget-fixed ||
           hooks.retainedBytes>OpenCLProbeBudget-fixed-clock.retainedBytes) {
            reject(SetupDecline::RetainedStorage);return {};
        }
        Lease reservedState,reservedWrapper;
        try {
            diagnostic.stage=SetupStage::Clock;
            const auto beginCpu=clock.read?clock.read(clock.owner.get()):glob2::threadCpuNs();
            if(!beginCpu){reject(SetupDecline::ClockRead);attempted.clockValid=false;++attempted.invalidClocks;return {};}
            const auto capturedCpu=captured->backgroundCallingReceipt().optionalCpuNs;
            const auto bytes=fixed+clock.retainedBytes+hooks.retainedBytes;
            const auto future=captured->futureStorage;
            if(!hooks.begin && !future){reject(SetupDecline::MissingFutureStorage);return {};}
            diagnostic.stage=SetupStage::Storage;
            std::shared_ptr<PrechargedProbeStorage> stateCredit,wrapperCredit;
            if(future) {
                stateCredit=PrechargedProbeStorage::claim(future,bytes);
                wrapperCredit=PrechargedProbeStorage::claim(future,wrapperBytes);
            } else {
                if(!reserveOpenCLProbeBytes(bytes)){reject(SetupDecline::HostStorage);return {};}
                reservedState.bytes=bytes;
                if(!reserveOpenCLProbeBytes(wrapperBytes)){reject(SetupDecline::HostStorage);return {};}
                reservedWrapper.bytes=wrapperBytes;
                stateCredit=PrechargedProbeStorage::adopt(reservedState.bytes);
                wrapperCredit=PrechargedProbeStorage::adopt(reservedWrapper.bytes);
            }
            diagnostic.stage=SetupStage::Readiness;diagnostic.ready=prepared(hooks);
            if(!diagnostic.ready){reject(SetupDecline::NotReady);return {};}
            // Adopt producer before moving ownership. Rejected admission keeps
            // the caller's capture alive for conservative receipt settlement.
            diagnostic.stage=SetupStage::Producer;
            if(!captured->adoptProducer()){reject(SetupDecline::Producer);return {};}
            diagnostic.stage=SetupStage::Allocation;
            auto allocation=std::unique_ptr<State>(new State({},std::move(clock),std::move(hooks),bytes));
            allocation->lease.bytes=0;
            allocation->storageCredit=stateCredit;
            allocation->initialCaptureCpuNs=capturedCpu;
            allocation->nativeBeginStart=nativeStart;
            auto payload=std::shared_ptr<State>(allocation.release(),[stateCredit](State* value){delete value;});
            auto result=std::unique_ptr<CaptureBatchProbeReplay>(new CaptureBatchProbeReplay(payload,0));
            result->wrapperCredit=std::move(wrapperCredit);
            // Every fixed allocation/control is complete before input ownership
            // moves. A setup decline after this point remains an extractable
            // wrapper, including while unused members finish on other workers.
            payload->record=std::move(captured);
            result->workload=payload->record->key();result->generation=payload->record->requiredGeneration();
            result->deadline=payload->record->due();
            payload->receipt.setup=diagnostic;payload->floor=beginCpu;
            if(!payload->now()){payload->receipt.setupDeclined=true;payload->receipt.setup.reason=SetupDecline::ClockRead;result->progress=Progress::Declined;
                attempted=payload->receipt;return result;}
            diagnostic.stage=SetupStage::Authorization;payload->receipt.setup.stage=diagnostic.stage;
            const auto authorization=payload->record->authorize(admission.requiredGeneration,admission.tick,
                admission.slackNs,admission.requiredBacklog);
            if(!authorization) {payload->receipt.setupDeclined=true;payload->receipt.setup.reason=SetupDecline::Authorization;result->progress=Progress::Declined;
                attempted=payload->receipt;return result;}
            payload->authorization=*authorization;
            payload->receipt.requiredOnlyAllowanceCpuNs=admission.requiredOnlyAllowanceCpuNs;
            payload->receipt.requiredAllowanceQualified=admission.requiredAllowanceQualified;
            payload->receipt.requiredAllowanceCalibration=admission.requiredAllowanceCalibration;
            payload->receipt.acceptedReferenceCpuNs=authorization->acceptedReferenceCpuNs;
            payload->receipt.commonPreparationCpuNs=authorization->commonPreparationCpuNs;
            payload->receipt.reservedCpuNs=authorization->reservedCpuNs;
            payload->receipt.captureCpuLimitNs=payload->record->captureLimit;
            const auto ticket=payload->record->probeTicket();
            payload->receipt.ticketId=ticket.id;payload->receipt.policyGeneration=ticket.generation;
            payload->receipt.requiredGeneration=payload->record->requiredGeneration();
            const auto& record=*payload->record;
            // State owns no wrapper backreference. Native Plane aliases retain
            // this wrapper, State and the real capture/replay ledger credits.
            diagnostic.stage=SetupStage::NativeBegin;payload->receipt.setup.stage=diagnostic.stage;
            try {
                auto costOwner=PrechargedProbeCostOwner::create(payload,record.storageCredit,payload->storageCredit,future);
                const auto retained=payload->receipt.reservedBytes+record.receipt().reservedBytes+
                    PrechargedProbeCostOwner::metadataBytes();
                CostIdentity identity{costOwner,0,record.requiredGeneration(),true,retained,
                    LAND_STEPS.cardinal|(LAND_STEPS.diagonal<<16)};
                identity.prechargedOwner=costOwner;
                OpenCLProbeInput first{record.original(0).data(),record.maxCost(),
                    field::Grid(int(record.key().width),int(record.key().height)),payload->session,
                    payload.get(),&State::cost,std::move(identity)};
                first.capturedPrivateCosts=true;
                std::array<OpenCLProbeInput,8> inputs{first,first,first,first,first,first,first,first};
                for(unsigned i=1;i<record.count();++i)inputs[i].gradient=record.original(i).data();
                const auto fields=std::span<const OpenCLProbeInput>(inputs.data(),record.count());
                if(payload->hooks.begin)result->probe=payload->hooks.begin(fields,authorization->plan,payload,payload->hooks.owner.get());
                else if(auto native=beginOpenCLProbeBatch(fields,authorization->plan,payload,{},future))
                    result->probe=std::make_unique<NativeProbe>(std::move(native));
            } catch(...) {payload->receipt.setupDeclined=true;payload->receipt.setup.reason=SetupDecline::Exception;}
            payload->account(beginCpu,payload->now(),true);
            result->snapshotProbe();
            if(!result->probe) {
                payload->receipt.setupDeclined=true;
                if(payload->receipt.setup.reason==SetupDecline::None)payload->receipt.setup.reason=SetupDecline::NativeBegin;
                if(payload.use_count()>2) {payload->receipt.quarantined=true;result->progress=Progress::Quarantined;}
                else result->progress=Progress::Declined;
            }
            payload->setupComplete=true;
            if(!payload->receipt.setupDeclined)payload->receipt.setup.stage=SetupStage::Complete;
            attempted=payload->receipt;return result;
        } catch(...) {reject(diagnostic.stage==SetupStage::Clock?SetupDecline::ClockRead:SetupDecline::Exception);
            attempted.clockValid=false;++attempted.invalidClocks;return {};}
    }
    static std::unique_ptr<CaptureBatchProbeReplay> begin(std::shared_ptr<GradientCpuCaptureGroup::Record>&& captured,
        Admission admission,Receipt& attempted) noexcept {
        return begin(std::move(captured),admission,attempted,Clock{},Hooks{});
    }
    Progress advance(Admission admission) noexcept {
        if(!state)return progress;
        auto& s=*state;
        // Only the original coordinator may touch lane/events. Migration may
        // request atomic cancellation, but leaves reaping to that coordinator.
        if(s.producer!=std::this_thread::get_id()) {
            s.receipt.clockValid=false;++s.receipt.invalidClocks;s.canceled=true;
            return progress;
        }
        const auto start=s.now();
        const bool comparisonAdvance=comparing;
        if(!s.record->captureValid() || admission.requiredGeneration!=s.record->requiredGeneration() || admission.requiredBacklog ||
           !admission.slackNs || admission.tick>=s.record->due()) {
            s.receipt.requiredCanceled=true;s.canceled=true;
        }
        // Atomic cancellation can originate elsewhere; diagnostic mutation is
        // confined to this producer and excludes clock/budget/required causes.
        if(s.canceled && s.receipt.clockValid && !s.receipt.budgetCanceled &&
           !s.receipt.overshoots && !s.receipt.requiredCanceled)s.receipt.explicitCanceled=true;
        if(s.canceled && probe)probe->cancel();
        if(progress==Progress::Quarantined) {
            if(s.hooks.reap)s.hooks.reap(s.hooks.owner.get());else reapOpenCLProbeQuarantine();
            if(state.use_count()==1)progress=Progress::Declined;
        } else if(progress==Progress::Pending && probe && !comparing) {
            const auto next=probe->advance();
            snapshotProbe();
            s.receipt.executionFailed=s.receipt.executionFailed || probe->executionFailed();
            if(next==OpenCLProbeProgress::Quarantined) {
                s.receipt.quarantined=true;progress=Progress::Quarantined;dropProbe();
            } else if(next==OpenCLProbeProgress::Declined) {progress=Progress::Declined;dropProbe();}
            else if(next==OpenCLProbeProgress::Complete)comparing=true;
        } else if(progress==Progress::Pending && comparing) {
            if(s.canceled) {progress=Progress::Declined;dropProbe();}
            else {
                auto remaining=GradientCpuCapture::CopyBlockBytes/sizeof(std::uint16_t);
                while(remaining && comparedField<s.record->count() && progress==Progress::Pending) {
                    const auto actual=probe->result(comparedField),expected=s.record->expected(comparedField);
                    if(actual.size()!=expected.size()) {
                        s.receipt.executionFailed=true;progress=Progress::Declined;break;
                    }
                    const auto end=std::min(actual.size(),comparedOffset+remaining);
                    for(auto i=comparedOffset;i<end;++i) {
                        ++s.receipt.comparedCells;
                        if(actual[i]!=expected[i]) {
                            s.receipt.firstMismatch=s.receipt.comparedCells-1;progress=Progress::Declined;break;
                        }
                    }
                    remaining-=end-comparedOffset;comparedOffset=end;
                    if(end==actual.size()){++comparedField;comparedOffset=0;}
                }
                if(progress==Progress::Declined)dropProbe();
                else if(comparedField==s.record->count()) {
                    s.receipt.exact=true;progress=Progress::ReadyToSettle;dropProbe();
                }
            }
        }
        s.account(start,s.now(),!comparisonAdvance,comparisonAdvance);
        s.receipt.elapsedNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-s.started).count();
        if(s.canceled && !probe && progress!=Progress::Quarantined)progress=Progress::Declined;
        return progress;
    }
    // Caller invokes token.cleanup(), closes complete process/tail accounting,
    // destroys this wrapper, then settles with a qualified outer authorization.
    // Until then diagnostic defaults suppress inference and pay full credit.
    std::optional<GradientCpuCaptureGroup::Settlement> takeSettlement() noexcept {
        if(!state || state->producer!=std::this_thread::get_id() || probe || state.use_count()!=1 ||
           !state->record->workerAliasesGone() ||
           (progress!=Progress::ReadyToSettle && progress!=Progress::Declined))return {};
        // A late member may invalidate timing after comparison but before the
        // last alias retires. Freeze that rejection before dropping Record;
        // removing State must never turn an invalid capture into native proof.
        if(!state->record->captureValid()) {
            state->receipt.requiredCanceled=true;progress=Progress::Declined;
            const auto captured=state->record->receipt();
            if(!captured.clockValid){state->receipt.clockValid=false;++state->receipt.invalidClocks;}
        }
        const auto additionalCpu=state->additionalBackgroundCpu();
        auto token=GradientCpuCaptureGroup::takeSettlement(state->record);
        if(!token)return {};
        finalReceipt=state->receipt;
        finalAuthorization=state->authorization;
        const auto start=glob2::threadCpuNs();state.reset();const auto end=glob2::threadCpuNs();
        token->addObservedCpu(additionalCpu,finalReceipt.clockValid);
        token->addObservedCpu(start && end && end>=start?end-start:0,start && end && end>=start);
        return token;
    }
};
}
