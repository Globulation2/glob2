// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ResourceClaims
{
// Bounded retry of claim routes after failures: a heuristic that limits how
// much route work one worker can spend on unreachable stock, not a
// connectivity proof. Recently failed accesses are skipped (unless already in
// pickup range), selection resumes after the pattern entry that failed, and
// route submissions are rate limited per owner. All clocks
// use unsigned elapsed ticks so a bounded interval also works across tick wrap.
struct ClaimRecovery
{
    static constexpr unsigned CacheEntries=4, TtlTicks=128;
    static constexpr unsigned AttemptWindowTicks=64, MaxAttempts=4, BackoffTicks=16;
    static constexpr std::uint32_t NoTile=UINT32_MAX;
    struct Entry {
        std::uint32_t access=NoTile, tick=0;
        std::uint16_t ttl=TtlTicks;
        bool operator==(const Entry&) const = default;
    };
    std::array<Entry,CacheEntries> entries{};
    std::uint32_t nextProbe=0,pinnedAccess=NoTile,windowStart=0,backoffStart=0,failureStreak=0;
    std::uint8_t material=255,movement=255,replacement=0,attempts=0;
    bool recovering=false,windowKnown=false,backoffKnown=false;
    bool operator==(const ClaimRecovery&) const = default;

    void setContext(std::uint8_t nextMaterial,std::uint8_t nextMovement) {
        if(material==nextMaterial && movement==nextMovement)return;
        material=nextMaterial;movement=nextMovement;entries={};replacement=0;
        nextProbe=0;pinnedAccess=NoTile;recovering=false;
        // A changed job/class cannot evade the owner-wide admission window.
    }
    bool allowsAttempt(std::uint32_t now) const {
        return (!backoffKnown || std::uint32_t(now-backoffStart)>=BackoffTicks)
            && (!windowKnown || std::uint32_t(now-windowStart)>=AttemptWindowTicks || attempts<MaxAttempts);
    }
    bool admit(std::uint32_t now) {
        if(!allowsAttempt(now))return false;
        if(!windowKnown || std::uint32_t(now-windowStart)>=AttemptWindowTicks) {
            windowKnown=true;windowStart=now;attempts=0;
        }
        backoffKnown=false;++attempts;return true;
    }
    bool rejects(std::uint32_t access,std::uint32_t now) const {
        if(access==pinnedAccess)return true;
        for(const auto& entry:entries)
            if(entry.access==access && std::uint32_t(now-entry.tick)<entry.ttl)return true;
        return false;
    }
    // The first failing request may predate allocation of this sparse state.
    // Caller accounts that request with admit(requestTick), before this method.
    // ttl==0 records an unproductive outcome without rejecting an access.
    void recordFailure(std::uint32_t now,std::uint32_t access,unsigned ttl=TtlTicks) {
        if(ttl>TtlTicks)throw std::invalid_argument("claim recovery TTL exceeds bound");
        if(failureStreak<UINT32_MAX)++failureStreak;
        if(ttl && access!=NoTile) {
            recovering=true;pinnedAccess=access;
            Entry* chosen=nullptr;
            for(auto& entry:entries)if(entry.access==access){chosen=&entry;break;}
            if(!chosen){chosen=&entries[replacement];replacement=(replacement+1)%CacheEntries;}
            *chosen={access,now,std::uint16_t(ttl)};
        }
        if(ttl && access!=NoTile && windowKnown && attempts>=MaxAttempts) {backoffKnown=true;backoffStart=now;}
    }
    void selected(std::uint32_t probe,std::uint32_t patternSize) {
        if(probe>=patternSize)throw std::invalid_argument("claim recovery probe outside pattern");
        nextProbe=probe+1;pinnedAccess=NoTile;
    }
    void exhausted() {nextProbe=0;pinnedAccess=NoTile;}
    void harvested() {
        failureStreak=0;recovering=false;nextProbe=0;pinnedAccess=NoTile;
        // Keep useful soft exclusions and the admission window. Neither claim
        // churn nor incidental movement can reopen the CPU budget.
    }
};

struct ClaimRecoverySelection
{
    const ClaimRecovery* state=nullptr;
    std::uint32_t tick=0,firstProbe=0;
    bool recovering=false,mayRoute=true;
    bool rejects(std::uint32_t access) const {return state && state->rejects(access,tick);}
};
enum class ClaimFailureKind : std::uint8_t { NoResource, PathFailed };
}
