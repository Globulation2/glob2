// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

// Outcomes already determined by the family movement selector. Each eligible
// query records at most one event of each kind; no extra neighbour scan is used.
struct GradientMovementEvidence
{
    bool emptyGoal=false, reachableNoMove=false;
};

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

struct ResourceGradientContradictionCounts
{
    // Keep historical counter IDs stable. Retired blocker/sidestep slots 1..3
    // are reserved; current collection emits only outcome slots 0, 4 and 5.
    // This diagnostic total is separate from persisted scheduling state.
    std::array<std::uint64_t,6> values{};
    void record(const GradientMovementEvidence& e) {
        values[0]+=e.emptyGoal;values[4]+=e.reachableNoMove;++values[5];
    }
    ResourceGradientContradictionCounts since(const ResourceGradientContradictionCounts& baseline) const {
        ResourceGradientContradictionCounts result;
        for (unsigned i=0;i<values.size();++i) result.values[i]=values[i]-baseline.values[i];
        return result;
    }
};
