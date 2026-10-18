// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

// Stack-only evidence from checks the live movement selector already performs.
// Flags combine strict and sidestep passes: each category counts once per query,
// even if multiple uphill neighbours fail or both passes inspect the same tile.
struct GradientMovementEvidence
{
    bool emptyGoal=false, persistentProgressBlocked=false, unitProgressBlocked=false;
    bool sidestepUsed=false, reachableNoMove=false;
};
enum class MovementRejection : std::uint8_t { None, Persistent, Unit };

struct ResourceGradientContradictionCounts
{
    // Order is part of the collection CSV contract; no values enter simulation
    // state, checksums, inference, or the scheduling policy.
    std::array<std::uint64_t,6> values{};
    void record(const GradientMovementEvidence& e) {
        values[0]+=e.emptyGoal;values[1]+=e.persistentProgressBlocked;
        values[2]+=e.unitProgressBlocked;values[3]+=e.sidestepUsed;
        values[4]+=e.reachableNoMove;++values[5];
    }
    ResourceGradientContradictionCounts since(const ResourceGradientContradictionCounts& baseline) const {
        ResourceGradientContradictionCounts result;
        for (unsigned i=0;i<values.size();++i) result.values[i]=values[i]-baseline.values[i];
        return result;
    }
};
