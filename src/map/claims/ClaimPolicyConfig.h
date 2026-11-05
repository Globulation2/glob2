// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace ResourceClaims
{
// Temporary, explicit live experiment. Defaults reproduce the format-195
// prototype; none of these parameters enables claims in an ordinary game.
struct ClaimPolicyConfig
{
    enum class Selection : std::uint8_t { FirstAvailable, ClosestGeometric };
    enum class Fallback : std::uint8_t { Wait, Roam, ReleaseJob, BoundedWaitRelease };
    enum class RouteTarget : std::uint8_t { ClaimedAccess, ProbeThenGradient };
    enum class PendingStep : std::uint8_t { Wait, DirectHeading };
    static constexpr unsigned SourceVersion=2;
    static constexpr unsigned LatestSourceVersion=5;
    unsigned sourceVersion=SourceVersion;
    bool usesRecovery() const { return sourceVersion>=2; }
    enum class PatternFrame : std::uint8_t { MapAxes, NearestD4 };
    PatternFrame patternFrame=PatternFrame::MapAxes;
    std::string pattern="spiral:k16:unit:s1:every:forward:d0:seed1";
    Selection selection=Selection::FirstAvailable;
    Fallback fallback=Fallback::Wait;
    RouteTarget routeTarget=RouteTarget::ClaimedAccess;
    PendingStep pendingStep=PendingStep::Wait;
    unsigned fallbackAfterDecisions=16, destinationRefreshJ=0, routeRefreshJ=0;
    unsigned maximumExpansions=4096;
    bool usePrimaryGradient=false;
    bool directResourceProbes=false; // Version 4 only; XY then physical tile candidates.
    bool opportunisticAdjacentPickup=false; // Versions 3+; old policies remain sticky.
    // Optional experiments, any source version. Defaults are omitted from the
    // canonical JSON so every existing policy keeps its exact digest.
    // A held claim moves to another access on refresh only when that access is
    // at least this many permille closer (toroidal Chebyshev); 0 is unrestricted.
    unsigned refreshSwitchMarginPermille=0;
    static constexpr unsigned MaximumRefreshSwitchMarginPermille=900;
    // Skip accesses whose terrain-only connected component excludes the unit.
    bool terrainComponentPrefilter=false;
    // A source admits claimants while its claim count stays below
    // spendable*permille/1000 (rounded down). 1000 is exact capacity; the
    // Unlimited sentinel (canonical "unlimited") drops the count limit.
    static constexpr unsigned ExactClaimOversubscriptionPermille=1000;
    static constexpr unsigned MaximumClaimOversubscriptionPermille=100000;
    static constexpr unsigned UnlimitedClaimOversubscription=UINT32_MAX;
    unsigned claimOversubscriptionPermille=ExactClaimOversubscriptionPermille;
    unsigned recoveryCandidateCount(std::size_t coordinates) const;
    unsigned maximumRecoveryCandidateCount() const;
    static ClaimPolicyConfig parse(std::string_view);
    void validate() const;
    std::string canonicalJson() const;
    std::string digest() const;
    bool operator==(const ClaimPolicyConfig&) const = default;
};
struct MaterialClaimPolicyState
{
    std::uint64_t decisionOrdinal=0,lastGoalAttempt=0;
    std::uint32_t fallbackCount=0,probe=UINT32_MAX;
    bool operator==(const MaterialClaimPolicyState&) const = default;
};
}
