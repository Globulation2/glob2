// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include "common/ThreadCpuClock.h"

namespace gradient_kernel
{
enum class Plan : unsigned;
enum class Family;

// Features are supplied by existing preparation. Unknown densities are explicit;
// constructing a key never scans a gradient, terrain or the simulation state.
struct WorkloadKey {
    unsigned width=0, height=0, cpuBuckets=0, threads=1, batch=1;
    Family family{};
    std::uint8_t seedDensity=255, blockerDensity=255;
    unsigned limit=65534;
    std::uint64_t movement=0;
    bool movementModifiers=false;
    bool operator==(const WorkloadKey&) const = default;
};

// Only lookup is used by required-work selection. All mutation, confidence
// calculations and probe accounting belong to background execution. Published
// keys never move, so readers require neither a lock nor dynamic allocation.
class CpuSavingPolicy
{
public:
    static constexpr unsigned MaxProfiles=64, MaxAlternatives=2, WindowTicks=256;
    static constexpr unsigned ProbePeriod=128, MinimumPairs=8, CooldownRequests=64;
    struct Choice { Plan plan=Plan(0); std::uint64_t version=0,maximumElapsedNs=0; };
    struct ProbeOffer { Plan plan=Plan(0);std::uint64_t reservedCpuNs=0,elapsedNs=0; };
    enum class ProbeKind { PaidCpuReplay, AcceptedCpuCapture, AcceptedCpuBatchCapture };
    enum class CapturePhase { AwaitingAcceptedReference, ReferenceBound, Running, Reaping };
    struct ProbeTicket {
        std::uint64_t id=0, generation=0;
        explicit operator bool() const { return id!=0; }
    };
    struct ProbeAuthorization {
        Plan plan=Plan(0);
        std::uint64_t acceptedReferenceCpuNs=0, commonPreparationCpuNs=0, reservedCpuNs=0;
    };
    struct ProbeOutcome {
        std::uint64_t alternativeCpuNs=0, elapsedNs=0, actualProbeCpuNs=0, slackNs=0;
        bool exact=false, success=false, clockValid=false;
    };
    struct Metrics {
        std::uint64_t acceptedCpuNs=0, probeCpuNs=0, admitted=0, canceled=0, promoted=0, demoted=0;
        unsigned profiles=0;std::uint64_t confidenceQualified=0;bool promotionEnabled=true;
    };
private:
    struct Reservation {
        std::uint64_t id=0, generation=0, tick=0, reservedCpuNs=0;
        unsigned profile=0, alternative=0;
        std::uint64_t commonPreparationCpuNs=0, acceptedReferenceCpuNs=0;
        ProbeKind kind=ProbeKind::PaidCpuReplay;
        CapturePhase phase=CapturePhase::Running;
        std::uint64_t requiredGeneration=0, conservativeElapsedNs=0;
        bool requiresFullReservedCharge=false;
        ProbeTicket ticket() const { return {id,generation}; }
    };
    struct Alternative {
        Plan plan=Plan(0);
        bool qualified=false;
        unsigned pairs=0;
        double sumRelative=0, sumAbsolute=0, relativeWidthsSquared=0, absoluteWidthsSquared=0, meanReference=0;
        std::uint64_t epoch=0;
        std::uint64_t maximumElapsed=0;
    };
    struct Profile {
        WorkloadKey key;
        std::atomic<std::uint64_t> decision{0},boundDecision{0},publishedElapsedNs{0};
        std::array<Alternative,MaxAlternatives> alternatives;
        std::uint64_t cpuReference=0,offeredCpu=0;
        unsigned offeredCount=0;
        unsigned expensive=0, cooldown=0;
    };
    struct Credit { std::uint64_t tick=UINT64_MAX, accepted=0, probes=0; };
    std::array<Profile,MaxProfiles> profiles;
    std::atomic<unsigned> published{0};
    std::array<Credit,WindowTicks> credits;
    mutable std::mutex background;
    std::atomic<std::uint64_t> generation{1};
    inline static std::atomic<std::uint64_t> nextTicket{1};
    std::uint64_t latestTick=0;
    std::optional<Reservation> active;
    inline static std::atomic<bool> globalProbeBusy{false};
    inline static std::atomic<std::uint64_t> nextConfidenceEpoch{1};
    inline static std::atomic<std::uint64_t> globalProbeOpportunities{0};
    Metrics totals;

    unsigned findOrCreate(const WorkloadKey& key) {
        const auto count=published.load(std::memory_order_relaxed);
        for(unsigned i=0;i<count;++i) if(profiles[i].key==key) return i;
        if(count==MaxProfiles) return MaxProfiles; // bounded: unknown classes stay CPU
        profiles[count].key=key;
        published.store(count+1,std::memory_order_release);
        return count;
    }
    static void publish(Profile& profile, Plan plan,std::uint64_t elapsedNs=0) {
        profile.offeredCpu=0;profile.offeredCount=0;
        const auto previous=profile.decision.load(std::memory_order_relaxed);
        const auto word=((previous>>8)+1)*256+unsigned(plan);
        // Sequential consistency orders invalidation before the new bound and
        // both reader tags after its payload read. Acquire/release alone across
        // these separate atomics would permit old tags with a new payload.
        profile.boundDecision.store(0,std::memory_order_seq_cst);
        profile.publishedElapsedNs.store(elapsedNs,std::memory_order_seq_cst);
        profile.decision.store(word,std::memory_order_seq_cst);
        profile.boundDecision.store(word,std::memory_order_seq_cst);
    }
    static std::uint64_t saturatedAdd(std::uint64_t a,std::uint64_t b) noexcept {
        return b>UINT64_MAX-a?UINT64_MAX:a+b;
    }
    Credit& credit(std::uint64_t tick) {
        auto& slot=credits[tick%WindowTicks];
        if(slot.tick!=tick) slot={tick,0,0};
        return slot;
    }
    void settleCredit(const Reservation& ticket,std::uint64_t actualCpuNs) {
        // An experiment can be paused across a whole rolling window. Never
        // recreate its expired slot and subtract a reservation from zero.
        auto& reserved=credits[ticket.tick%WindowTicks];
        if(reserved.tick==ticket.tick) reserved.probes-=ticket.reservedCpuNs;
        auto& current=credit(latestTick);current.probes=saturatedAdd(current.probes,actualCpuNs);
        totals.probeCpuNs=saturatedAdd(totals.probeCpuNs,actualCpuNs);
    }
    void demote(Profile& profile) {
        if((profile.decision.load(std::memory_order_relaxed)&255)!=0) {
            publish(profile,Plan(0)); ++totals.demoted;
        }
        profile.cooldown=CooldownRequests;
        profile.expensive=0;
        for(auto& alternative:profile.alternatives) {
            alternative.pairs=0; alternative.sumRelative=alternative.sumAbsolute=alternative.meanReference=0;
            alternative.relativeWidthsSquared=alternative.absoluteWidthsSquared=0;
            alternative.epoch=nextConfidenceEpoch.fetch_add(1,std::memory_order_relaxed);
            alternative.maximumElapsed=0;
        }
    }
    bool matches(const ProbeTicket& ticket) const {
        return active && active->id==ticket.id && active->generation==ticket.generation;
    }
    std::optional<ProbeTicket> beginReservation(const WorkloadKey& key, Plan plan, std::uint64_t tick,
        std::uint64_t reserveCpuNs, std::uint64_t slackNs, std::uint64_t conservativeElapsedNs,
        bool backlog, std::uint64_t commonPreparationCpuNs, ProbeKind kind,
        std::uint64_t requiredGeneration) {
        // Rejected sampling opportunities take no policy lock or profile scan.
        // The caller accounts admission/capture CPU separately from required work.
        if((globalProbeOpportunities.fetch_add(1,std::memory_order_relaxed)+1)%ProbePeriod ||
           backlog || !reserveCpuNs || !conservativeElapsedNs || !slackNs ||
           conservativeElapsedNs>slackNs/2) return {};
        std::lock_guard lock(background);
        latestTick=std::max(latestTick,tick);
        if(active || totals.probeCpuNs>totals.acceptedCpuNs/100 ||
           reserveCpuNs>totals.acceptedCpuNs/100-totals.probeCpuNs) return {};
        const auto index=findOrCreate(key);
        if(index==MaxProfiles || profiles[index].cooldown) return {};
        unsigned alternative=MaxAlternatives;
        for(unsigned i=0;i<MaxAlternatives;++i)
            if(profiles[index].alternatives[i].qualified && profiles[index].alternatives[i].plan==plan) alternative=i;
        if(alternative==MaxAlternatives) return {};
        std::uint64_t accepted=0, probes=0;
        for(const auto& c:credits) if(c.tick<=tick && tick-c.tick<WindowTicks) { accepted=saturatedAdd(accepted,c.accepted); probes=saturatedAdd(probes,c.probes); }
        if(probes>accepted/100 || reserveCpuNs>accepted/100-probes) return {};
        bool available=false;
        if(!globalProbeBusy.compare_exchange_strong(available,true,std::memory_order_acq_rel)) return {};
        auto& reserved=credit(tick);reserved.probes=saturatedAdd(reserved.probes,reserveCpuNs);
        active=Reservation{nextTicket.fetch_add(1,std::memory_order_relaxed),generation.load(std::memory_order_acquire),tick,reserveCpuNs,
            index,alternative,commonPreparationCpuNs,0,kind,
            kind!=ProbeKind::PaidCpuReplay ? CapturePhase::AwaitingAcceptedReference : CapturePhase::Running,
            requiredGeneration,conservativeElapsedNs,false};
        ++totals.admitted;
        return active->ticket();
    }
public:
    explicit CpuSavingPolicy(bool promotionEnabled=true) {totals.promotionEnabled=promotionEnabled;}
    ~CpuSavingPolicy() {
        // Captured work retains its policy. Disposal follows cancellation and
        // cannot leave another map permanently excluded from experimentation.
        if(active) globalProbeBusy.store(false,std::memory_order_release);
    }
    Choice lookup(const WorkloadKey& key) const noexcept {
        const auto count=published.load(std::memory_order_acquire);
        for(unsigned i=0;i<count;++i) if(profiles[i].key==key) {
            const auto word=profiles[i].decision.load(std::memory_order_seq_cst);
            const auto boundBefore=profiles[i].boundDecision.load(std::memory_order_seq_cst);
            const auto elapsed=profiles[i].publishedElapsedNs.load(std::memory_order_seq_cst);
            const auto boundAfter=profiles[i].boundDecision.load(std::memory_order_seq_cst);
            if(boundBefore!=word || boundAfter!=word)return {};
            return {Plan(word&255),word>>8,elapsed};
        }
        return {};
    }
    // Worker/coordinator only. Caps are predictable before observing a new
    // captured result; unknown references cannot fund an experiment.
    ProbeOffer probeOffer(const WorkloadKey& key) const {
        std::lock_guard lock(background);
        const auto count=published.load(std::memory_order_acquire);
        for(unsigned i=0;i<count;++i)if(profiles[i].key==key) {
            const auto& p=profiles[i];if(p.cooldown || !p.cpuReference)return {};
            for(const auto& a:p.alternatives)if(a.qualified) {
                const auto cap=std::clamp(p.cpuReference/4,std::uint64_t(1000000),std::uint64_t(8000000));
                // Before a measured alternative exists, this is only the
                // admission quantum for exploratory optional dispatches. It is
                // never a completion bound for required work or promotion.
                return {a.plan,cap,a.maximumElapsed?a.maximumElapsed:std::uint64_t(1000000)};
            }
        }
        return {};
    }
    // One group reservation precedes every member clone. Singleton history is
    // used only for a predictable optional CPU cap, never as batch evidence.
    ProbeOffer groupOffer(const WorkloadKey& singleton,unsigned capacity,std::uint64_t tick) const {
        if(singleton.batch!=1 || (capacity!=1 && capacity!=2 && capacity!=4 && capacity!=8))return {};
        std::lock_guard lock(background);
        std::uint64_t reference=0;const auto count=published.load(std::memory_order_acquire);
        for(unsigned i=0;i<count;++i)if(profiles[i].key==singleton)reference=profiles[i].cpuReference;
        if(!reference)return {};
        auto key=singleton;key.batch=capacity;
        for(unsigned i=0;i<count;++i)if(profiles[i].key==key && !profiles[i].cooldown) {
            for(const auto& a:profiles[i].alternatives)if(a.qualified) {
                const auto estimated=reference>UINT64_MAX/capacity?UINT64_MAX:reference*capacity;
                const auto reserve=std::clamp(estimated/4,std::uint64_t(1000000),std::uint64_t(8000000));
                std::uint64_t accepted=0,paid=0;
                for(const auto& c:credits)if(c.tick<=tick && tick-c.tick<WindowTicks) {
                    accepted=saturatedAdd(accepted,c.accepted);paid=saturatedAdd(paid,c.probes);
                }
                if(totals.probeCpuNs>totals.acceptedCpuNs/100 || reserve>totals.acceptedCpuNs/100-totals.probeCpuNs ||
                   paid>accepted/100 || reserve>accepted/100-paid)return {};
                return {a.plan,reserve,a.maximumElapsed?a.maximumElapsed:std::uint64_t(1000000)};
            }
        }
        return {};
    }
    // Admission attempts and background profile processing are tuning too.
    // Charging debt instead of hiding rejected work suppresses further probes.
    void recordTuningOverhead(std::uint64_t tick,std::uint64_t ns,bool valid=true) {
        const auto start=glob2::threadCpuNs();std::lock_guard lock(background);
        latestTick=std::max(latestTick,tick);
        const auto end=glob2::threadCpuNs();
        if(!valid || !start || end<start)ns=std::max(ns,totals.acceptedCpuNs/100+1);
        else ns=ns>UINT64_MAX-(end-start)?UINT64_MAX:ns+end-start;
        auto& c=credit(latestTick);c.probes=ns>UINT64_MAX-c.probes?UINT64_MAX:c.probes+ns;
        totals.probeCpuNs=ns>UINT64_MAX-totals.probeCpuNs?UINT64_MAX:totals.probeCpuNs+ns;
    }
    // Offline exact-array qualification is supplied by the experiment harness.
    // At most two qualified GPU alternatives may be retained for one class.
    bool qualify(const WorkloadKey& key, Plan plan, bool exactnessPassed) {
        if(!exactnessPassed || unsigned(plan)==0 || unsigned(plan)>=7) return false;
        std::lock_guard lock(background);
        const auto index=findOrCreate(key);
        if(index==MaxProfiles) return false;
        for(auto& alternative:profiles[index].alternatives) {
            if(alternative.qualified && alternative.plan==plan) return true;
            if(!alternative.qualified) { alternative.plan=plan; alternative.qualified=true;
                alternative.epoch=nextConfidenceEpoch.fetch_add(1,std::memory_order_relaxed); return true; }
        }
        return false;
    }
    void observeAccepted(const WorkloadKey& key, Plan plan, std::uint64_t hostCpuNs,
                         std::uint64_t tick, bool failed=false, bool publicationStall=false) {
        std::lock_guard lock(background);
        latestTick=std::max(latestTick,tick);
        auto& current=credit(tick);current.accepted=saturatedAdd(current.accepted,hostCpuNs);
        totals.acceptedCpuNs=saturatedAdd(totals.acceptedCpuNs,hostCpuNs);
        // Batch cooldown ages on real matching required fields, even while
        // singleton CPU remains selected. This changes no batch reference or
        // elapsed evidence and grants no synthetic batch CPU credits.
        if(key.batch==1 && hostCpuNs && !failed && !publicationStall) {
            const auto count=published.load(std::memory_order_acquire);
            for(unsigned i=0;i<count;++i)if(profiles[i].key.batch>1 && profiles[i].cooldown) {
                auto singleton=profiles[i].key;singleton.batch=1;
                if(singleton==key)--profiles[i].cooldown;
            }
        }
        const auto index=findOrCreate(key);
        if(index==MaxProfiles) return;
        auto& profile=profiles[index];
        if(profile.cooldown) --profile.cooldown;
        if(unsigned(plan)==0 && hostCpuNs) {
            profile.cpuReference=profile.cpuReference ? profile.cpuReference-profile.cpuReference/8+hostCpuNs/8 : hostCpuNs;
        }
        if(failed || publicationStall) { demote(profile); return; }
        if(unsigned(plan)!=0 && profile.cpuReference && hostCpuNs>profile.cpuReference-profile.cpuReference/10) {
            if(++profile.expensive>=4) demote(profile);
        } else if(unsigned(plan)!=0) profile.expensive=0;
    }
    // Batch-only offers that cannot form an already-ready set still pay real
    // CPU preparation/handoff/recovery. Accumulate exactly N such receipts for
    // demotion only; they never train a batch reference or completion bound.
    void observeBatchOfferFallback(const WorkloadKey& key,Plan plan,std::uint64_t version,
        std::uint64_t hostCpuNs,std::uint64_t tick,bool clockValid) {
        std::lock_guard lock(background);
        latestTick=std::max(latestTick,tick);
        auto& current=credit(tick);current.accepted=saturatedAdd(current.accepted,clockValid?hostCpuNs:0);
        totals.acceptedCpuNs=saturatedAdd(totals.acceptedCpuNs,clockValid?hostCpuNs:0);
        const auto count=published.load(std::memory_order_acquire);
        for(unsigned i=0;i<count;++i)if(profiles[i].key==key) {
            auto& profile=profiles[i];const auto decision=profile.decision.load(std::memory_order_acquire);
            if(Plan(decision&255)!=plan || (decision>>8)!=version)return;
            if(!clockValid || !hostCpuNs){demote(profile);return;}
            if(key.batch!=2 && key.batch!=4 && key.batch!=8)return;
            profile.offeredCpu=saturatedAdd(profile.offeredCpu,hostCpuNs);
            if(++profile.offeredCount<key.batch)return;
            const auto cpu=profile.offeredCpu;profile.offeredCpu=0;profile.offeredCount=0;
            if(profile.cpuReference && cpu>profile.cpuReference-profile.cpuReference/10) {
                if(++profile.expensive>=4)demote(profile);
            }else profile.expensive=0;
            return;
        }
    }
    // Admission reserves actual host CPU credits before immutable capture. A
    // canceled experiment still pays CPU already consumed. The caller runs CPU
    // work in <=500us resumable chunks and GPU work one dispatch at a time.
    std::optional<ProbeTicket> beginProbe(const WorkloadKey& key, Plan plan, std::uint64_t tick,
                                         std::uint64_t reserveCpuNs, std::uint64_t slackNs,
                                         std::uint64_t conservativeElapsedNs, bool backlog,
                                         std::uint64_t commonPreparationCpuNs=0) {
        return beginReservation(key,plan,tick,reserveCpuNs,slackNs,conservativeElapsedNs,backlog,
            commonPreparationCpuNs,ProbeKind::PaidCpuReplay,0);
    }
    // A required CPU job has not propagated yet. Admission precedes both seed
    // and golden clones. Its session generation is distinct from policy generation.
    std::optional<ProbeTicket> beginAcceptedCapture(const WorkloadKey& key, Plan plan, std::uint64_t tick,
        std::uint64_t reserveCpuNs, std::uint64_t slackNs, std::uint64_t conservativeElapsedNs,
        bool backlog, std::uint64_t commonPreparationCpuNs, std::uint64_t requiredGeneration,
        bool alternativeReady) {
        if(!alternativeReady || !requiredGeneration)return {};
        return beginReservation(key,plan,tick,reserveCpuNs,slackNs,conservativeElapsedNs,backlog,
            commonPreparationCpuNs,ProbeKind::AcceptedCpuCapture,requiredGeneration);
    }
    std::optional<ProbeTicket> beginAcceptedBatchCapture(const WorkloadKey& maximumKey,Plan plan,
        std::uint64_t tick,std::uint64_t reserveCpuNs,std::uint64_t slackNs,
        std::uint64_t conservativeElapsedNs,bool backlog,std::uint64_t requiredGeneration,bool alternativeReady) {
        if(!alternativeReady || !requiredGeneration ||
           (maximumKey.batch!=1 && maximumKey.batch!=2 && maximumKey.batch!=4 && maximumKey.batch!=8))return {};
        return beginReservation(maximumKey,plan,tick,reserveCpuNs,slackNs,conservativeElapsedNs,backlog,
            0,ProbeKind::AcceptedCpuBatchCapture,requiredGeneration);
    }
    // Freeze only already-complete members. Reference/preparation are exact sums
    // of those selected accepted jobs; unused member expense remains paid.
    bool bindReadyBatch(const ProbeTicket& ticket,const WorkloadKey& readyKey,std::uint64_t requiredGeneration,
        std::uint64_t propagationCpuNs,std::uint64_t commonPreparationCpuNs,bool successful,bool clockValid) {
        std::lock_guard lock(background);
        if(!matches(ticket) || active->kind!=ProbeKind::AcceptedCpuBatchCapture ||
           active->phase!=CapturePhase::AwaitingAcceptedReference)return false;
        const auto& maximum=profiles[active->profile].key;
        auto expected=maximum;expected.batch=readyKey.batch;
        if(!(expected==readyKey) || readyKey.batch>maximum.batch ||
           (readyKey.batch!=1 && readyKey.batch!=2 && readyKey.batch!=4 && readyKey.batch!=8))return false;
        if(!successful || !clockValid || !propagationCpuNs || active->requiredGeneration!=requiredGeneration ||
           active->generation!=generation.load(std::memory_order_acquire)) {
            active->requiresFullReservedCharge=true;active->phase=CapturePhase::Reaping;return false;
        }
        const auto plan=profiles[active->profile].alternatives[active->alternative].plan;
        const auto index=findOrCreate(readyKey);if(index==MaxProfiles)return false;
        unsigned alternative=MaxAlternatives;
        for(unsigned i=0;i<MaxAlternatives;++i)if(profiles[index].alternatives[i].qualified &&
            profiles[index].alternatives[i].plan==plan)alternative=i;
        if(alternative==MaxAlternatives || profiles[index].cooldown)return false;
        active->profile=index;active->alternative=alternative;
        active->acceptedReferenceCpuNs=propagationCpuNs;active->commonPreparationCpuNs=commonPreparationCpuNs;
        active->kind=ProbeKind::AcceptedCpuCapture;active->phase=CapturePhase::ReferenceBound;
        return true;
    }
    void observeBatchFailure(const WorkloadKey& singleton,unsigned batch,std::uint64_t tick) {
        auto key=singleton;key.batch=batch;observeAccepted(key,Plan(0),0,tick,true);
        if(batch!=1){key.batch=1;observeAccepted(key,Plan(0),0,tick,true);}
    }
    bool bindAcceptedReference(const ProbeTicket& ticket,std::uint64_t requiredGeneration,
        std::uint64_t propagationCpuNs,bool successful,bool clockValid) {
        std::lock_guard lock(background);
        if(!matches(ticket) || active->kind!=ProbeKind::AcceptedCpuCapture ||
           active->phase!=CapturePhase::AwaitingAcceptedReference)return false;
        if(!clockValid || !propagationCpuNs)active->requiresFullReservedCharge=true;
        if(!successful || !clockValid || !propagationCpuNs ||
           active->requiredGeneration!=requiredGeneration ||
           active->generation!=generation.load(std::memory_order_acquire)) {
            active->phase=CapturePhase::Reaping;
            return false;
        }
        active->acceptedReferenceCpuNs=propagationCpuNs;
        active->phase=CapturePhase::ReferenceBound;
        return true;
    }
    std::optional<ProbeAuthorization> authorizeOptionalExecution(const ProbeTicket& ticket,
        std::uint64_t requiredGeneration,std::uint64_t currentTick,std::uint64_t slackNs,bool backlog) {
        std::lock_guard lock(background);
        if(!matches(ticket) || active->kind!=ProbeKind::AcceptedCpuCapture ||
           active->phase!=CapturePhase::ReferenceBound || active->requiresFullReservedCharge ||
           active->requiredGeneration!=requiredGeneration ||
           active->generation!=generation.load(std::memory_order_acquire) || currentTick<active->tick ||
           backlog || !slackNs || !active->conservativeElapsedNs ||
           active->conservativeElapsedNs>slackNs/2)return {};
        latestTick=std::max(latestTick,currentTick);
        active->phase=CapturePhase::Running;
        return ProbeAuthorization{profiles[active->profile].alternatives[active->alternative].plan,
            active->acceptedReferenceCpuNs,active->commonPreparationCpuNs,active->reservedCpuNs};
    }
private:
    bool finish(const ProbeTicket& ticket, std::uint64_t referenceCpuNs, std::uint64_t alternativeCpuNs,
                std::uint64_t elapsedNs, std::uint64_t actualProbeCpuNs, std::uint64_t slackNs,
                bool exact,bool success,bool clockValid,ProbeKind kind) {
        const auto accountingStart=glob2::threadCpuNs();
        std::lock_guard lock(background);
        if(!matches(ticket))return false;
        const bool phaseValid=active->kind==kind && active->phase==CapturePhase::Running;
        if(kind==ProbeKind::AcceptedCpuCapture)referenceCpuNs=active->acceptedReferenceCpuNs;
        if(!clockValid)active->requiresFullReservedCharge=true;
        active->phase=CapturePhase::Reaping;
        const auto reservation=*active;
        auto& profile=profiles[reservation.profile];
        auto settle=[&](bool promoted) {
            const auto now=glob2::threadCpuNs();
            const bool accountingValid=accountingStart && now && now>=accountingStart;
            const auto accountingCpu=accountingValid ? now-accountingStart : 0;
            auto actual=actualProbeCpuNs>UINT64_MAX-accountingCpu ? UINT64_MAX : actualProbeCpuNs+accountingCpu;
            if(!accountingValid || reservation.requiresFullReservedCharge)
                actual=std::max(actual,reservation.reservedCpuNs);
            if(!accountingValid || reservation.requiresFullReservedCharge || actual>reservation.reservedCpuNs) {
                demote(profile); promoted=false;
            }
            settleCredit(reservation,actual);
            active.reset(); globalProbeBusy.store(false,std::memory_order_release);
            return promoted;
        };
        if(reservation.generation!=generation.load(std::memory_order_acquire)) {
            ++totals.canceled;
            actualProbeCpuNs=std::max(actualProbeCpuNs,reservation.reservedCpuNs);
            return settle(false);
        }
        if(!phaseValid){++totals.canceled;return settle(false);}
        if(reservation.requiresFullReservedCharge){demote(profile);return settle(false);}
        // A reference already measured on this immutable accepted job is free
        // to optional accounting, but must match the value frozen at binding.
        // Otherwise both reference and alternative CPU must be paid by credits.
        const bool acceptedReference=reservation.kind==ProbeKind::AcceptedCpuCapture;
        const bool validReference=acceptedReference ? referenceCpuNs==reservation.acceptedReferenceCpuNs :
            referenceCpuNs<=actualProbeCpuNs && alternativeCpuNs<=actualProbeCpuNs-referenceCpuNs;
        if(!success || !exact || !referenceCpuNs || !alternativeCpuNs || !elapsedNs || !slackNs || !validReference ||
           alternativeCpuNs>actualProbeCpuNs || actualProbeCpuNs>reservation.reservedCpuNs) {
            demote(profile); return settle(false);
        }
        auto& alternative=profile.alternatives[reservation.alternative];
        const double reference=double(referenceCpuNs)+double(reservation.commonPreparationCpuNs);
        const double candidate=double(alternativeCpuNs)+double(reservation.commonPreparationCpuNs);
        const auto n=++alternative.pairs;
        alternative.sumRelative+=candidate-.9*reference;
        alternative.sumAbsolute+=candidate-reference+10000.0*profile.key.batch;
        const double cap=double(reservation.reservedCpuNs);
        const double relativeWidth=(acceptedReference ? 1.0 : 1.9)*cap;
        const double absoluteWidth=(acceptedReference ? 1.0 : 2.0)*cap;
        alternative.relativeWidthsSquared+=relativeWidth*relativeWidth;
        alternative.absoluteWidthsSquared+=absoluteWidth*absoluteWidth;
        alternative.meanReference+=(reference-alternative.meanReference)/n;
        alternative.maximumElapsed=std::max(alternative.maximumElapsed,elapsedNs);
        profile.cpuReference=std::uint64_t(alternative.meanReference);
        if(n<MinimumPairs || (n&(n-1)) || profile.cooldown || alternative.maximumElapsed>slackNs/2)
            return settle(false);
        // Predeclared doubling looks with alpha spending across every epoch and
        // both criteria. Conditional Hoeffding bounds permit changing predictable
        // caps and nonstationary noise; repeated checking never resets alpha.
        // They bound the sampled conditional mean, not future workload drift.
        unsigned look=1; for(auto count=n;count>MinimumPairs;count/=2) ++look;
        constexpr double pi=3.14159265358979323846;
        const double epoch=double(alternative.epoch);
        const double delta=.05*(6/(pi*pi*epoch*epoch))*(6/(pi*pi*look*look))/2;
        const double logAlpha=std::log(1/delta);
        const double relativeUpper=alternative.sumRelative+std::sqrt(.5*alternative.relativeWidthsSquared*logAlpha);
        const double absoluteUpper=alternative.sumAbsolute+std::sqrt(.5*alternative.absoluteWidthsSquared*logAlpha);
        if(relativeUpper>=0 || absoluteUpper>=0) return settle(false);
        ++totals.confidenceQualified;
        if(!totals.promotionEnabled)return settle(false);
        // Include the statistical work in the reservation before publishing.
        if(!settle(true)) return false;
        publish(profile,alternative.plan,alternative.maximumElapsed); ++totals.promoted; return true;
    }
public:
    bool finishProbe(const ProbeTicket& ticket,std::uint64_t referenceCpuNs,std::uint64_t alternativeCpuNs,
        std::uint64_t elapsedNs,std::uint64_t actualProbeCpuNs,std::uint64_t slackNs,bool exact,bool success) {
        return finish(ticket,referenceCpuNs,alternativeCpuNs,elapsedNs,actualProbeCpuNs,slackNs,
            exact,success,true,ProbeKind::PaidCpuReplay);
    }
    bool finishAcceptedCapture(const ProbeTicket& ticket,const ProbeOutcome& outcome) {
        return finish(ticket,0,outcome.alternativeCpuNs,outcome.elapsedNs,outcome.actualProbeCpuNs,
            outcome.slackNs,outcome.exact,outcome.success,outcome.clockValid,ProbeKind::AcceptedCpuCapture);
    }
    void cancelProbe(const ProbeTicket& ticket, std::uint64_t actualCpuNs,bool clockValid=true) {
        const auto started=glob2::threadCpuNs();
        std::lock_guard lock(background);
        if(!matches(ticket))return;
        const auto ended=glob2::threadCpuNs();
        const bool accountingValid=started && ended && ended>=started;
        const auto overhead=accountingValid ? ended-started : 0;
        const auto actual=actualCpuNs>UINT64_MAX-overhead ? UINT64_MAX : actualCpuNs+overhead;
        const bool full=!clockValid || !accountingValid || active->requiresFullReservedCharge ||
            active->generation!=generation.load(std::memory_order_acquire);
        active->phase=CapturePhase::Reaping;
        settleCredit(*active,full ? std::max(actual,active->reservedCpuNs) : actual);
        ++totals.canceled; active.reset(); globalProbeBusy.store(false,std::memory_order_release);
    }
    // Owner-side cancellation is bounded metadata only. The background holder
    // keeps the global probe slot until transfers/reference work are reaped;
    // invalidation must not admit another probe while the old one still runs.
    // Immutable captured inputs become stale; performance history remains useful.
    void invalidate() noexcept {
        generation.fetch_add(1,std::memory_order_release);
    }
    Metrics metrics() const {
        std::lock_guard lock(background);
        auto result=totals; result.profiles=published.load(std::memory_order_acquire); return result;
    }
};
}
