// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientCpuCapture.h"
#include <chrono>

namespace gradient_kernel
{
// Unattached background adapter. One producer owns every operation except
// cancel(). Optional device/compiler initialization is never performed here.
class CaptureProbeReplay
{
public:
    enum class Progress {Pending,ReadyToSettle,Declined,Quarantined};
    struct Admission {
        std::uint64_t requiredGeneration=0,tick=0,slackNs=0;
        bool requiredBacklog=false;
    };
    struct Receipt {
        std::uint64_t attemptedCallingCpuNs=0,backgroundCallingCpuNs=0;
        std::uint64_t alternativeCallingCpuNs=0,comparisonCpuNs=0,elapsedNs=0;
        std::uint64_t invalidClocks=0,overshoots=0,comparedCells=0;
        std::size_t firstMismatch=SIZE_MAX,reservedBytes=0;
        bool clockValid=true,exact=false,nativeBackend=true,inferenceSuppressed=true;
        bool quarantined=false,setupDeclined=false,executionFailed=false;
        // Immutable bound reference and latest native stage, retained after
        // native/state cleanup. Diagnostics never replace paid cost receipts.
        std::uint64_t acceptedReferenceCpuNs=0,commonPreparationCpuNs=0,reservedCpuNs=0;
        OpenCLProbe::Metrics nativeMetrics;
        bool nativeMetricsAvailable=false,budgetCanceled=false,requiredCanceled=false,explicitCanceled=false;
    };
    struct Clock {
        std::shared_ptr<const void> owner;
        std::uint64_t (*read)(const void*)=nullptr;
        std::size_t retainedBytes=0;
    };
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
        virtual std::span<const std::uint16_t> result() const noexcept=0;
    };
    struct Hooks {
        std::shared_ptr<const void> owner;
        std::size_t retainedBytes=0;
        bool (*ready)(const void*) noexcept=nullptr;
        std::unique_ptr<Probe> (*begin)(const OpenCLProbeInput&,Plan,std::shared_ptr<const void>,const void*)=nullptr;
        bool (*reap)(const void*) noexcept=nullptr;
    };
private:
    struct Lease {
        std::size_t bytes=0;
        ~Lease(){releaseOpenCLProbeBytes(bytes);}
    } wrapperLease; // Last destroyed, after wrapper allocation's payload.
    class NativeProbe final : public Probe {
        std::unique_ptr<OpenCLProbe> probe;
    public:
        explicit NativeProbe(std::unique_ptr<OpenCLProbe> value):probe(std::move(value)) {}
        OpenCLProbeProgress advance() noexcept override {return probe->advance();}
        void cancel() noexcept override {probe->cancel();}
        bool executionFailed() const noexcept override {return probe->metrics().executionFailed;}
        bool waitingForEvent() const noexcept override {return probe->waitingForEvent();}
        bool metrics(OpenCLProbe::Metrics& out) const noexcept override {out=probe->metrics();return true;}
        std::span<const std::uint16_t> result() const noexcept override {return probe->result();}
    };
    struct State {
        std::shared_ptr<PrechargedProbeStorage> storageCredit; // Destroyed last.
        Lease lease; // Pins reservation through all retained payload destruction.
        std::shared_ptr<GradientCpuCapture::Record> record;
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
        explicit State(std::shared_ptr<GradientCpuCapture::Record> captured,Clock timer,Hooks backend,std::size_t bytes)
            :record(std::move(captured)),clock(std::move(timer)),hooks(std::move(backend)) {
            lease.bytes=bytes;session.setExternalInitialization(true);
            receipt.reservedBytes=bytes;receipt.nativeBackend=!hooks.begin;
        }
        ~State() {
            // Premature disposal/quarantine may leave this as the last holder.
            // Captured admission cannot settle until the backend drops every
            // keepAlive alias after positive terminal proof.
            if(record) {
                const auto cleanupStart=glob2::threadCpuNs();
                auto observed=additionalBackgroundCpu();
                bool valid=receipt.clockValid;
                const auto duplicate=record->receipt().optionalCpuNs-initialCaptureCpuNs;
                auto settlement=GradientCpuCapture::takeSettlement(record);
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
            const auto current=record?record->receipt().optionalCpuNs:initialCaptureCpuNs;
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
            if(authorization.reservedCpuNs &&
               (record->receipt().optionalCpuNs>=authorization.reservedCpuNs ||
                receipt.backgroundCallingCpuNs>=authorization.reservedCpuNs-record->receipt().optionalCpuNs)) {
                receipt.budgetCanceled=true;canceled=true;
            }
        }
        static EntrySteps cost(void* value,std::size_t cell) {
            const auto& captured=*static_cast<State*>(value)->record;
            return weightedClass(captured.swim()) && captured.water()[cell]!=0?
                entrySteps(WATER_STEP[captured.swim()]):LAND_STEPS;
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
    explicit CaptureProbeReplay(std::shared_ptr<State> value,std::size_t bytes):state(std::move(value)) {
        wrapperLease.bytes=bytes;
        workload=state->record->key();generation=state->record->requiredGeneration();deadline=state->record->due();
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
    CaptureProbeReplay(const CaptureProbeReplay&)=delete;
    ~CaptureProbeReplay() {
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
    // No event queries or allocations. Zero requests another bounded broker
    // turn, with required EDF reconsidered first; it never loops within advance.
    std::uint64_t nextAdvanceDelayNs() const noexcept {
        if(progress==Progress::Quarantined)return 100000000;
        return progress==Progress::Pending && !comparing && probe && probe->waitingForEvent()?100000:0;
    }
    static std::unique_ptr<CaptureProbeReplay> begin(std::shared_ptr<GradientCpuCapture::Record>&& captured,
        Admission admission,Receipt& attempted,Clock clock,Hooks hooks) noexcept {
        attempted={};
        const auto nativeStart=glob2::threadCpuNs();
        struct Attempt {
            Receipt& out;std::uint64_t start;
            ~Attempt(){const auto end=glob2::threadCpuNs();
                if(start && end && end>=start)out.attemptedCallingCpuNs=end-start;
                else {out.clockValid=false;++out.invalidClocks;}}
        } attempt{attempted,nativeStart};
        if(!captured || captured.use_count()!=1 || !captured->receipt().referenceBound ||
           captured->requiredGeneration()!=admission.requiredGeneration || admission.requiredBacklog ||
           !admission.slackNs || admission.tick>=captured->due() ||
           (clock.read && (!clock.owner || !clock.retainedBytes)) ||
           (!clock.read && (clock.owner || clock.retainedBytes)) ||
           (hooks.begin && (!hooks.owner || !hooks.retainedBytes || !hooks.ready || !hooks.reap)) ||
           (!hooks.begin && (hooks.owner || hooks.retainedBytes || hooks.ready || hooks.reap))) {
            attempted.setupDeclined=true;return {};
        }
        constexpr std::size_t fixed=sizeof(State)+sizeof(NativeProbe)+512+PrechargedProbeStorage::metadataBytes();
        if(clock.retainedBytes>OpenCLProbeBudget-fixed ||
           hooks.retainedBytes>OpenCLProbeBudget-fixed-clock.retainedBytes) {
            attempted.setupDeclined=true;return {};
        }
        Lease reservedState,reservedWrapper;
        try {
            const auto beginCpu=clock.read?clock.read(clock.owner.get()):glob2::threadCpuNs();
            if(!beginCpu){attempted.clockValid=false;++attempted.invalidClocks;return {};}
            const auto capturedCpu=captured->receipt().optionalCpuNs;
            const auto bytes=fixed+clock.retainedBytes+hooks.retainedBytes;
            if(!reserveOpenCLProbeBytes(bytes))return {};
            reservedState.bytes=bytes;
            if(!reserveOpenCLProbeBytes(sizeof(CaptureProbeReplay)))return {};
            reservedWrapper.bytes=sizeof(CaptureProbeReplay);
            if(!prepared(hooks)){attempted.setupDeclined=true;return {};}
            // Adopt producer before moving ownership. Rejected admission keeps
            // the caller's capture alive for conservative receipt settlement.
            if(!captured->adoptProducer()){attempted.setupDeclined=true;return {};}
            auto allocation=std::unique_ptr<State>(new State(std::move(captured),std::move(clock),std::move(hooks),bytes));
            reservedState.bytes=0;
            allocation->initialCaptureCpuNs=capturedCpu;
            allocation->nativeBeginStart=nativeStart;
            allocation->storageCredit=PrechargedProbeStorage::adopt(allocation->lease.bytes);
            auto stateCredit=allocation->storageCredit;
            auto payload=std::shared_ptr<State>(allocation.release(),[stateCredit](State* value){delete value;});
            auto result=std::unique_ptr<CaptureProbeReplay>(new CaptureProbeReplay(payload,reservedWrapper.bytes));
            reservedWrapper.bytes=0;
            payload->floor=beginCpu;
            if(!payload->now()){payload->receipt.setupDeclined=true;return {};}
            const auto authorization=payload->record->authorize(admission.requiredGeneration,admission.tick,
                admission.slackNs,admission.requiredBacklog);
            if(!authorization) {payload->receipt.setupDeclined=true;return {};}
            payload->authorization=*authorization;
            payload->receipt.acceptedReferenceCpuNs=authorization->acceptedReferenceCpuNs;
            payload->receipt.commonPreparationCpuNs=authorization->commonPreparationCpuNs;
            payload->receipt.reservedCpuNs=authorization->reservedCpuNs;
            const auto& record=*payload->record;
            auto costOwner=PrechargedProbeCostOwner::create(payload,record.storageCredit,payload->storageCredit);
            const auto retained=payload->receipt.reservedBytes+record.receipt().reservedBytes+
                PrechargedProbeCostOwner::metadataBytes();
            CostIdentity identity{costOwner,0,record.requiredGeneration(),true,retained,
                weightedClass(record.swim())?0u:LAND_STEPS.cardinal|(LAND_STEPS.diagonal<<16)};
            identity.prechargedOwner=costOwner;
            OpenCLProbeInput input{record.original().data(),record.maxCost(),
                field::Grid(int(record.key().width),int(record.key().height)),payload->session,
                payload.get(),&State::cost,std::move(identity)};
            input.capturedPrivateCosts=true;
            if(payload->hooks.begin)result->probe=payload->hooks.begin(input,authorization->plan,payload,payload->hooks.owner.get());
            else if(auto native=beginOpenCLProbe(input,authorization->plan,payload))
                result->probe=std::make_unique<NativeProbe>(std::move(native));
            payload->account(beginCpu,payload->now(),true);
            result->snapshotProbe();
            if(!result->probe){payload->receipt.setupDeclined=true;return {};}
            payload->setupComplete=true;
            attempted=payload->receipt;return result;
        } catch(...) {attempted.setupDeclined=true;attempted.clockValid=false;++attempted.invalidClocks;return {};}
    }
    static std::unique_ptr<CaptureProbeReplay> begin(std::shared_ptr<GradientCpuCapture::Record>&& captured,
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
        if(admission.requiredGeneration!=s.record->requiredGeneration() || admission.requiredBacklog ||
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
                const auto actual=probe->result(),expected=s.record->expected();
                if(actual.size()!=expected.size()){s.receipt.executionFailed=true;progress=Progress::Declined;dropProbe();}
                else {
                    const auto end=std::min(actual.size(),std::size_t(s.receipt.comparedCells)+GradientCpuCapture::CopyBlockBytes/sizeof(std::uint16_t));
                    for(auto i=std::size_t(s.receipt.comparedCells);i<end;++i) {
                        s.receipt.comparedCells=i+1;
                        if(actual[i]!=expected[i]) {s.receipt.firstMismatch=i;progress=Progress::Declined;break;}
                    }
                    if(progress==Progress::Declined)dropProbe();
                    else if(end==actual.size()){s.receipt.exact=true;progress=Progress::ReadyToSettle;dropProbe();}
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
    std::optional<GradientCpuCapture::Settlement> takeSettlement() noexcept {
        if(!state || state->producer!=std::this_thread::get_id() || probe || state.use_count()!=1 ||
           (progress!=Progress::ReadyToSettle && progress!=Progress::Declined))return {};
        const auto additionalCpu=state->additionalBackgroundCpu();
        auto token=GradientCpuCapture::takeSettlement(state->record);
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
