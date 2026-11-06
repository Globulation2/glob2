// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
#include <array>
#include <optional>
#include <utility>
#include <vector>
#include "TeamCapacity.h"

class Map;
class Unit;
namespace ResourceClaims
{
// Derived target occupancy for the claim_trigger "contention" policy. One
// snapshot per team, taken immediately before that team steps its units, of
// every live same-team worker fetching a material: ACT_FILLING, MED_FREE,
// attached, and going to or harvesting at a resource. Its target cell is the
// held claim access when it holds a claim, else its ordinary target
// (targetX/targetY while that target is valid, or the touched pickup tile
// while harvesting). Keys are sorted (material, cell), so the snapshot and
// every count depend only on unit state, never on iteration or hash order.
// Rebuilt every tick from unit state: never saved, checksummed or snapshotted.
class ContentionIndex
{
public:
    static std::optional<Uint32> targetCell(const Map&,const Unit&);
    // Replace the team snapshot with its current live units (cost O(n log n)).
    void rebuild(const Map&,int team,Uint64 tick);
    bool current(int team,Uint64 tick) const { return teams[team].tick==tick; }
    // Snapshot workers other than `self` fetching `material` whose target lies
    // within toroidal Chebyshev `radius` of (x,y). Cost O((2r+1) log n).
    unsigned others(const Map&,const Unit& self,int material,int x,int y,unsigned radius) const;
    std::size_t size(int team) const { return teams[team].keys.size(); }
private:
    struct TeamSnapshot {
        Uint64 tick=UINT64_MAX;
        std::vector<Uint64> keys;                       // (material<<32)|cell, sorted.
        std::vector<std::pair<Uint16,Uint64>> selves;  // (gid,key), sorted by gid.
    };
    std::array<TeamSnapshot,TEAM_MAX_COUNT> teams;
};
}
