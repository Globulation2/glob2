// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
#include <cstddef>
#include <vector>
#include "ResourceClaims.h"

namespace ResourceClaims
{
// Outcome of a hire-time claim target search (Unit::materialClaimHireAllowed).
struct HireTarget
{
    // Unchecked: the search cannot judge this worker (no published field or
    // destinations, or a stationary worker); hiring proceeds as before.
    enum class State : std::uint8_t { Unchecked, Found, None };
    State state=State::Unchecked;
    Claim claim{};
    Uint32 capacity=0;
    bool infinite=false;
};
// Derived per-team table of hire target searches, run from one frozen state
// immediately before the team's building hiring phase. Every
// search is a pure function of that state and its (worker, material) key, so
// results never depend on thread count or scheduling. Rebuilt every tick for
// the team about to hire; never saved, checksummed or snapshotted.
class HireTargetIndex
{
public:
    struct Request { Uint64 key=0; Uint16 gid=0; std::uint8_t material=0; };
    static Uint64 key(Uint16 gid,int material) {return (Uint64(gid)<<8)|Uint64(std::uint8_t(material));}
    void reset(int team,Uint64 tick) {this->team=team;this->tick=tick;requests.clear();results.clear();}
    bool current(int team,Uint64 tick) const {return this->team==team && this->tick==tick;}
    const HireTarget* find(Uint16 gid,int material) const;
    int team=-1;
    Uint64 tick=UINT64_MAX;
    std::vector<Request> requests; // Sorted by key, unique.
    std::vector<HireTarget> results;
};
}
