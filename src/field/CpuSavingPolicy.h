// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

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
    struct Choice { Plan plan=Plan(0); std::uint64_t version=0; };
    struct ProbeTicket {
        std::uint64_t id=0, generation=0, tick=0, reservedCpuNs=0;
        unsigned profile=0, alternative=0;
        explicit operator bool() const { return id!=0; }
    };
    struct Metrics {
        std::uint64_t acceptedCpuNs=0, probeCpuNs=0, admitted=0, canceled=0, promoted=0, demoted=0;
        unsigned profiles=0;
    };
private:
    struct Alternative {
        Plan plan=Plan(0);
        bool qualified=false;
        unsigned pairs=0;
        double meanDelta=0, deltaM2=0, meanReference=0;
        std::uint64_t maximumElapsed=0;
    };
    struct Profile {
        WorkloadKey key;
        std::atomic<std::uint64_t> decision{0};
        std::array<Alternative,MaxAlternatives> alternatives;
        std::uint64_t cpuReference=0;
        unsigned expensive=0, cooldown=0;
    };
    struct Credit { std::uint64_t tick=UINT64_MAX, accepted=0, probes=0; };
    std::array<Profile,MaxProfiles> profiles;
    std::atomic<unsigned> published{0};
    std::array<Credit,WindowTicks> credits;
    mutable std::mutex background;
    std::uint64_t generation=1, nextTicket=1, latestTick=0;
    std::optional<ProbeTicket> active;
    inline static std::atomic<bool> globalProbeBusy{false};
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
    static void publish(Profile& profile, Plan plan) {
        const auto previous=profile.decision.load(std::memory_order_relaxed);
        profile.decision.store(((previous>>8)+1)*256+unsigned(plan),std::memory_order_release);
    }
    Credit& credit(std::uint64_t tick) {
        auto& slot=credits[tick%WindowTicks];
        if(slot.tick!=tick) slot={tick,0,0};
        return slot;
    }
    void settleCredit(const ProbeTicket& ticket,std::uint64_t actualCpuNs) {
        // An experiment can be paused across a whole rolling window. Never
        // recreate its expired slot and subtract a reservation from zero.
        auto& reserved=credits[ticket.tick%WindowTicks];
        if(reserved.tick==ticket.tick) reserved.probes-=ticket.reservedCpuNs;
        credit(latestTick).probes+=actualCpuNs;
        totals.probeCpuNs+=actualCpuNs;
    }
    void demote(Profile& profile) {
        if((profile.decision.load(std::memory_order_relaxed)&255)!=0) {
            publish(profile,Plan(0)); ++totals.demoted;
        }
        profile.cooldown=CooldownRequests;
        profile.expensive=0;
        for(auto& alternative:profile.alternatives) {
            alternative.pairs=0; alternative.meanDelta=alternative.deltaM2=alternative.meanReference=0;
            alternative.maximumElapsed=0;
        }
    }
public:
    ~CpuSavingPolicy() {
        // Captured work retains its policy. Disposal follows cancellation and
        // cannot leave another map permanently excluded from experimentation.
        if(active) globalProbeBusy.store(false,std::memory_order_release);
    }
    Choice lookup(const WorkloadKey& key) const noexcept {
        const auto count=published.load(std::memory_order_acquire);
        for(unsigned i=0;i<count;++i) if(profiles[i].key==key) {
            const auto word=profiles[i].decision.load(std::memory_order_acquire);
            return {Plan(word&255),word>>8};
        }
        return {};
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
            if(!alternative.qualified) { alternative.plan=plan; alternative.qualified=true; return true; }
        }
        return false;
    }
    void observeAccepted(const WorkloadKey& key, Plan plan, std::uint64_t hostCpuNs,
                         std::uint64_t tick, bool failed=false, bool publicationStall=false) {
        std::lock_guard lock(background);
        latestTick=std::max(latestTick,tick);
        credit(tick).accepted+=hostCpuNs; totals.acceptedCpuNs+=hostCpuNs;
        const auto index=findOrCreate(key);
        if(index==MaxProfiles) return;
        auto& profile=profiles[index];
        if(profile.cooldown) --profile.cooldown;
        if(unsigned(plan)==0 && hostCpuNs) {
            profile.cpuReference=profile.cpuReference ? profile.cpuReference-profile.cpuReference/8+hostCpuNs/8 : hostCpuNs;
        }
        if(failed || publicationStall) { demote(profile); return; }
        if(unsigned(plan)!=0 && profile.cpuReference && hostCpuNs>profile.cpuReference*9/10) {
            if(++profile.expensive>=4) demote(profile);
        } else if(unsigned(plan)!=0) profile.expensive=0;
    }
    // Admission reserves actual host CPU credits before immutable capture. A
    // canceled experiment still pays CPU already consumed. The caller runs CPU
    // work in <=500us resumable chunks and GPU work one dispatch at a time.
    std::optional<ProbeTicket> beginProbe(const WorkloadKey& key, Plan plan, std::uint64_t tick,
                                         std::uint64_t reserveCpuNs, std::uint64_t slackNs,
                                         std::uint64_t conservativeElapsedNs, bool backlog) {
        std::lock_guard lock(background);
        latestTick=std::max(latestTick,tick);
        if((globalProbeOpportunities.fetch_add(1,std::memory_order_relaxed)+1)%ProbePeriod || active || backlog || !reserveCpuNs ||
           conservativeElapsedNs>slackNs/2) return {};
        const auto index=findOrCreate(key);
        if(index==MaxProfiles || profiles[index].cooldown) return {};
        unsigned alternative=MaxAlternatives;
        for(unsigned i=0;i<MaxAlternatives;++i)
            if(profiles[index].alternatives[i].qualified && profiles[index].alternatives[i].plan==plan) alternative=i;
        if(alternative==MaxAlternatives) return {};
        std::uint64_t accepted=0, probes=0;
        for(const auto& c:credits) if(c.tick<=tick && tick-c.tick<WindowTicks) { accepted+=c.accepted; probes+=c.probes; }
        if(probes>accepted/100 || reserveCpuNs>accepted/100-probes) return {};
        bool available=false;
        if(!globalProbeBusy.compare_exchange_strong(available,true,std::memory_order_acq_rel)) return {};
        credit(tick).probes+=reserveCpuNs;
        active=ProbeTicket{nextTicket++,generation,tick,reserveCpuNs,index,alternative};
        ++totals.admitted;
        return active;
    }
    bool finishProbe(const ProbeTicket& ticket, std::uint64_t referenceCpuNs, std::uint64_t alternativeCpuNs,
                     std::uint64_t elapsedNs, std::uint64_t actualProbeCpuNs, std::uint64_t slackNs,
                     bool exact, bool success) {
        std::lock_guard lock(background);
        if(!active || active->id!=ticket.id || ticket.generation!=generation) return false;
        // Tickets are opaque identifiers to callers. Accounting and indices use
        // the trusted internal reservation, never caller-modified fields.
        const auto reservation=*active;
        settleCredit(reservation,actualProbeCpuNs);
        active.reset();
        globalProbeBusy.store(false,std::memory_order_release);
        auto& profile=profiles[reservation.profile];
        if(!success || !exact || !referenceCpuNs || !alternativeCpuNs || actualProbeCpuNs>reservation.reservedCpuNs) {
            demote(profile); return false;
        }
        auto& alternative=profile.alternatives[reservation.alternative];
        // Paired differences retain covariance. Totals are measured for a
        // homogeneous batch; there is no invented amortization from singletons.
        const double delta=double(alternativeCpuNs)-double(referenceCpuNs);
        const auto n=++alternative.pairs;
        const double difference=delta-alternative.meanDelta;
        alternative.meanDelta+=difference/n;
        alternative.deltaM2+=difference*(delta-alternative.meanDelta);
        alternative.meanReference+=(double(referenceCpuNs)-alternative.meanReference)/n;
        alternative.maximumElapsed=std::max(alternative.maximumElapsed,elapsedNs);
        profile.cpuReference=std::uint64_t(alternative.meanReference);
        if(n<MinimumPairs || profile.cooldown || alternative.maximumElapsed>slackNs/2) return false;
        // 3 exceeds the two-sided Student t critical value for n>=8. This
        // deliberately conservative bound avoids premature online promotion.
        const double upper=alternative.meanDelta+3*std::sqrt(alternative.deltaM2/(n-1)/n);
        const double required=std::max(alternative.meanReference*.10,10000.0*profile.key.batch);
        if(upper>=-required) return false;
        publish(profile,alternative.plan); ++totals.promoted; return true;
    }
    void cancelProbe(const ProbeTicket& ticket, std::uint64_t actualCpuNs) {
        std::lock_guard lock(background);
        if(!active || active->id!=ticket.id) return;
        settleCredit(*active,actualCpuNs);
        ++totals.canceled; active.reset(); globalProbeBusy.store(false,std::memory_order_release);
    }
    // Invoke only after optional work is canceled. Published performance history
    // survives a terrain revision; immutable captured inputs use a separate gen.
    void invalidate() {
        std::lock_guard lock(background);
        if(active) { ++totals.canceled; active.reset(); globalProbeBusy.store(false,std::memory_order_release); } // reserved credit remains charged
        ++generation;
    }
    Metrics metrics() const {
        std::lock_guard lock(background);
        auto result=totals; result.profiles=published.load(std::memory_order_acquire); return result;
    }
};
}
