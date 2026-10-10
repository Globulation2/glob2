// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientCpuCapture.h"
#include "common/ThreadCpuEnvelope.h"

namespace gradient_kernel
{
// Empirical fixed-tail accounting, not causal driver attribution. The qualified
// release boolean is frozen by production configuration after whole tuning-on /
// off evidence. Until then no authorization may permit inference.
class CaptureProbeAccounting
{
public:
    static constexpr std::uint64_t TailIntervalNs=250000000;
    struct Receipt {
        std::uint64_t requiredGeneration=0,ticketId=0,policyGeneration=0;
        std::uint64_t processBeginNs=0,processEndNs=0,processCpuNs=0,knownInnerCpuNs=0,unknownUpperCpuNs=0;
        std::uint64_t capturedCpuNs=0,optionalCallingCpuNs=0,totalUpperCpuNs=0,samplerCpuNs=0,closingCpuNs=0;
        std::uint64_t deviceTerminalNs=0,cleanupNs=0,firstTailNs=0,secondTailNs=0;
        std::uint64_t cleanupProcessCpuNs=0,firstTailProcessCpuNs=0,secondTailProcessCpuNs=0;
        std::uint64_t invalidClocks=0,overshoots=0,maxResolutionNs=0;
        std::size_t reservedBytes=0,omittedThreads=0;
        bool clockValid=true,validEnvelope=false,churn=false,cleanupComplete=false,wrapperDestroyed=false;
        bool releaseQualified=false,nativeBackend=false,inferenceEligible=false;
    };
    struct FinishResult {
        CaptureSettlementAuthorization authorization;
        CpuSavingPolicy::ProbeOutcome outcome;
        Receipt receipt;
    };
private:
    struct Lease {std::size_t bytes=0;~Lease(){releaseOpenCLProbeBytes(bytes);}} lease;
    std::shared_ptr<glob2::CpuClockRegistry> registry;
    std::optional<glob2::ProcessCpuEnvelope> envelope;
    std::thread::id producer=std::this_thread::get_id();
    CpuSavingPolicy::ProbeTicket ticket;
    Receipt totals;
    std::uint64_t cpuFloor=0,wallFloor=0,nextTail=0,processFloor=0;
    unsigned tails=0;
    bool finishing=false;
    CaptureProbeAccounting(std::shared_ptr<glob2::CpuClockRegistry> clocks,std::uint64_t capturedCpu,
        std::uint64_t generation,CpuSavingPolicy::ProbeTicket identity,bool qualified,std::size_t bytes)
        :registry(std::move(clocks)),ticket(identity) {
        lease.bytes=bytes;totals.reservedBytes=bytes;totals.capturedCpuNs=capturedCpu;
        totals.requiredGeneration=generation;totals.ticketId=ticket.id;totals.policyGeneration=ticket.generation;
        totals.releaseQualified=qualified;
        envelope.emplace(*registry);totals.processBeginNs=envelope->processBeginCpuNs();processFloor=totals.processBeginNs;
        if(!processFloor)invalid();
    }
    void invalid() noexcept {totals.clockValid=false;++totals.invalidClocks;}
    bool sameProducer() noexcept {
        if(producer==std::this_thread::get_id())return true;invalid();return false;
    }
    bool wall(std::uint64_t now) noexcept {
        if(!sameProducer() || !now || now<wallFloor){invalid();return false;}
        wallFloor=now;return true;
    }
    bool checkpoint(std::uint64_t& output) noexcept {
        if(!envelope){invalid();return false;}
        const auto value=envelope->sampleProcessCpu();
        if(!value.valid || !value.ns || value.ns<processFloor){invalid();return false;}
        processFloor=value.ns;output=value.ns;return true;
    }
    void add(std::uint64_t ns) noexcept {
        if(ns>UINT64_MAX-totals.optionalCallingCpuNs)invalid();
        else totals.optionalCallingCpuNs+=ns;
    }
public:
    CaptureProbeAccounting(const CaptureProbeAccounting&)=delete;
    // The registry shared owner must pin its reader context as well. retainedBytes
    // covers that COMPLETE graph/control allocation, even if already globally
    // charged. Native registry retention is conservatively sizeof(registry)+256.
    static std::unique_ptr<CaptureProbeAccounting> begin(std::shared_ptr<glob2::CpuClockRegistry> registry,
        std::uint64_t capturedOptionalCpuNs,std::uint64_t requiredGeneration,CpuSavingPolicy::ProbeTicket ticket,
        bool releaseQualified,Receipt& attempted,std::size_t retainedBytes=glob2::CpuClockRegistry::storageBytes()+256) noexcept {
        attempted={};
        constexpr auto fixed=sizeof(CaptureProbeAccounting)+256;
        if(!registry || !requiredGeneration || !ticket || retainedBytes<glob2::CpuClockRegistry::storageBytes() ||
           retainedBytes>OpenCLProbeBudget-fixed)return {};
        const auto bytes=fixed+retainedBytes;
        if(!reserveOpenCLProbeBytes(bytes))return {};
        struct Guard {std::size_t bytes;~Guard(){if(bytes)releaseOpenCLProbeBytes(bytes);}} guard{bytes};
        try {
            auto result=std::unique_ptr<CaptureProbeAccounting>(new CaptureProbeAccounting(std::move(registry),
                capturedOptionalCpuNs,requiredGeneration,ticket,releaseQualified,bytes));
            guard.bytes=0;attempted=result->totals;return result;
        } catch(...) {attempted.clockValid=false;++attempted.invalidClocks;return {};}
    }
    // All caller brackets are disjoint optional work only: allocation, queue,
    // adapter, sampler, wrapper destruction and cleanup. Required execution is
    // excluded. Mutation overhead is separately measured here; samplerCpuNs is
    // component telemetry and is never added twice.
    void accountStep(std::uint64_t start,std::uint64_t end) noexcept {
        if(!sameProducer())return;
        const auto mutationStart=glob2::threadCpuNs();
        if(!start || !end || end<start || start<cpuFloor)invalid();
        else {
            cpuFloor=end;add(end-start);
            if(end-start>GradientCpuCapture::MaxBlockCpuNs)++totals.overshoots;
        }
        const auto mutationEnd=glob2::threadCpuNs();
        if(!mutationStart || !mutationEnd || mutationEnd<mutationStart)invalid();else add(mutationEnd-mutationStart);
    }
    bool envelopeReady() const noexcept {return envelope && envelope->progress()==glob2::ProcessCpuEnvelope::Progress::Running;}
    bool tailFinished() const noexcept {return envelope && envelope->progress()==glob2::ProcessCpuEnvelope::Progress::Complete;}
    std::uint64_t nextTailWakeNs() const noexcept {return !failed() && totals.cleanupComplete && tails<2 && !finishing ? nextTail : 0;}
    bool failed() const noexcept {return !totals.clockValid || !envelope || envelope->progress()==glob2::ProcessCpuEnvelope::Progress::Invalid;}
    glob2::ProcessCpuEnvelope::Progress advanceEnvelope() noexcept {
        if(!sameProducer())return glob2::ProcessCpuEnvelope::Progress::Invalid;
        if(!envelope){invalid();return glob2::ProcessCpuEnvelope::Progress::Invalid;}
        if(failed())return glob2::ProcessCpuEnvelope::Progress::Invalid;
        return envelope->advance(); // <=64 clocks; caller accountStep covers it.
    }
    bool markDeviceTerminal(std::uint64_t now) noexcept {
        if(!wall(now) || !envelopeReady())return false;
        if(!totals.deviceTerminalNs)totals.deviceTerminalNs=now;return true;
    }
    template<class CaptureToken> bool markCleanup(const CaptureToken& token,bool wrapperDestroyed,std::uint64_t now) noexcept {
        if(!wall(now) || !totals.deviceTerminalNs || !wrapperDestroyed || !token.cleanupComplete() ||
           token.requiredGeneration()!=totals.requiredGeneration || token.probeTicket().id!=ticket.id ||
           token.probeTicket().generation!=ticket.generation){invalid();return false;}
        if(totals.cleanupComplete)return true;
        totals.cleanupNs=now;totals.cleanupComplete=true;totals.wrapperDestroyed=true;
        if(!checkpoint(totals.cleanupProcessCpuNs))return false;
        if(now>UINT64_MAX-TailIntervalNs){invalid();return false;}
        nextTail=now+TailIntervalNs;return true;
    }
    bool advanceTail(std::uint64_t now) noexcept {
        if(failed() || !wall(now) || !totals.cleanupComplete)return false;
        if(finishing) {advanceEnvelope();return tailFinished();}
        if(now<nextTail)return false;
        if(!tails){totals.firstTailNs=now;tails=1;
            if(!checkpoint(totals.firstTailProcessCpuNs))return false;
            if(now>UINT64_MAX-TailIntervalNs){invalid();return false;}
            nextTail=now+TailIntervalNs;return false;}
        totals.secondTailNs=now;tails=2;
        if(!checkpoint(totals.secondTailProcessCpuNs))return false;
        if(!envelope->finish()){invalid();return false;}
        finishing=true;advanceEnvelope();return tailFinished();
    }
    Receipt receipt() const noexcept {return totals;}
    template<class CaptureToken> FinishResult finishResult(const CaptureToken& token,std::uint64_t currentRequiredGeneration,
        bool nativeBackend,bool success,bool exact,std::uint64_t elapsedNs,std::uint64_t slackNs) noexcept {
        FinishResult result;totals.nativeBackend=nativeBackend;
        const glob2::ProcessCpuEnvelopeResult unavailable{};
        const auto& measured=envelope?envelope->metrics():unavailable;
        if(!envelope)invalid();
        totals.validEnvelope=measured.valid;totals.churn=measured.churn;totals.omittedThreads=measured.omittedThreads;
        totals.processCpuNs=measured.processCpuNs;totals.knownInnerCpuNs=measured.knownInnerCpuNs;
        totals.unknownUpperCpuNs=measured.unknownUpperCpuNs;totals.samplerCpuNs=measured.samplerCpuNs;
        totals.maxResolutionNs=measured.maxResolutionNs;
        if(measured.valid) {
            totals.processEndNs=totals.processBeginNs+measured.processCpuNs;
            if(totals.processEndNs<processFloor)invalid();
        }
        const auto captured=token.receipt();
        const bool overflow=totals.capturedCpuNs>UINT64_MAX-totals.optionalCallingCpuNs;
        const auto explicitCpu=overflow?UINT64_MAX:totals.capturedCpuNs+totals.optionalCallingCpuNs;
        const auto observed=std::max(explicitCpu,captured.optionalCpuNs);
        if(overflow || measured.unknownUpperCpuNs>UINT64_MAX-observed)invalid();
        totals.totalUpperCpuNs=observed>UINT64_MAX-measured.unknownUpperCpuNs?UINT64_MAX:observed+measured.unknownUpperCpuNs;
        const bool identity=token.probeTicket().id==ticket.id && token.probeTicket().generation==ticket.generation &&
            currentRequiredGeneration==totals.requiredGeneration && token.requiredGeneration()==totals.requiredGeneration;
        totals.inferenceEligible=sameProducer() && identity && tailFinished() && tails==2 &&
            totals.cleanupComplete && token.cleanupComplete() && totals.wrapperDestroyed && totals.clockValid &&
            captured.clockValid && !captured.requiresFullReservedCharge && !totals.overshoots &&
            measured.valid && !measured.churn && !measured.omittedThreads &&
            nativeBackend && totals.releaseQualified && success && exact && elapsedNs && slackNs;
        result.authorization=CaptureSettlementAuthorization(totals.inferenceEligible,ticket.id,ticket.generation,
            totals.requiredGeneration,totals.totalUpperCpuNs);
        result.outcome={totals.totalUpperCpuNs,elapsedNs,totals.totalUpperCpuNs,slackNs,exact,success,totals.inferenceEligible};
        result.receipt=totals;return result;
    }
    // The final envelope endpoint precedes destruction of its own metadata.
    // Charge that closing work explicitly before settling; callers must include
    // any additional queue/receipt/settlement-wrapper work in their outer cost.
    // This does not extend the empirical process tail or claim causal proof.
    static FinishResult close(std::unique_ptr<CaptureProbeAccounting>& accounting,FinishResult result,
        std::uint64_t closingStartedCpuNs=0) noexcept {
        if(accounting && !accounting->sameProducer()) {
            result.authorization=CaptureSettlementAuthorization(false);result.outcome.clockValid=false;
            result.receipt.clockValid=false;result.receipt.inferenceEligible=false;++result.receipt.invalidClocks;
            return result; // Original producer still owns destruction.
        }
        const auto start=closingStartedCpuNs?closingStartedCpuNs:glob2::threadCpuNs();accounting.reset();const auto end=glob2::threadCpuNs();
        const bool valid=start && end && end>=start;
        const auto cost=valid?end-start:0;
        result.receipt.closingCpuNs=cost;
        if(cost>GradientCpuCapture::MaxBlockCpuNs) {
            ++result.receipt.overshoots;result.receipt.inferenceEligible=false;
            result.authorization=CaptureSettlementAuthorization(false);result.outcome.clockValid=false;
        }
        if(!valid || cost>UINT64_MAX-result.outcome.actualProbeCpuNs || cost>UINT64_MAX-result.outcome.alternativeCpuNs) {
            result.authorization=CaptureSettlementAuthorization(false);result.outcome.clockValid=false;
            result.receipt.clockValid=false;result.receipt.inferenceEligible=false;++result.receipt.invalidClocks;
        } else {
            result.outcome.actualProbeCpuNs+=cost;result.outcome.alternativeCpuNs+=cost;
            result.receipt.totalUpperCpuNs=result.outcome.actualProbeCpuNs;
            result.authorization.minimumCpuNs=result.outcome.actualProbeCpuNs;
        }
        return result;
    }
};
}
