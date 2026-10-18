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

// Stable counter IDs also name the diagnostic CSV columns. Generated models
// select a compact subset; do not reorder these when changing model features.
enum class GradientMovementCounter : unsigned {
    EmptyGoal=0, PersistentProgressBlocked=1, UnitProgressBlocked=2,
    SidestepUsed=3, ReachableNoMove=4, MovementQueries=5
};
constexpr unsigned gradientMovementCounterBit(GradientMovementCounter counter) {
    return 1u << static_cast<unsigned>(counter);
}
inline constexpr unsigned GradientMovementOutcomeMask=
    gradientMovementCounterBit(GradientMovementCounter::EmptyGoal)
    | gradientMovementCounterBit(GradientMovementCounter::ReachableNoMove)
    | gradientMovementCounterBit(GradientMovementCounter::MovementQueries);

// A compile-time ID lets the policy update only its selected production
// counters. Diagnostic totals below deliberately retain all six categories.
template<unsigned Id>
std::uint64_t contradictionValue(const GradientMovementEvidence& evidence) {
    static_assert(Id<6);
    if constexpr (Id==0) return evidence.emptyGoal;
    else if constexpr (Id==1) return evidence.persistentProgressBlocked;
    else if constexpr (Id==2) return evidence.unitProgressBlocked;
    else if constexpr (Id==3) return evidence.sidestepUsed;
    else if constexpr (Id==4) return evidence.reachableNoMove;
    else return 1; // Every eligible natural-ground query, including no movement.
}

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
