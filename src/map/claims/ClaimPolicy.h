// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string_view>

// The resource-claim policy every new game uses ("contention-v2").
//
// Without claims, every worker fetching a material follows the same shared
// gradient to the same nearest stock, so a late-game crowd converges on one
// farm tile and most of it arrives to find the stock already taken. A claim
// reserves one unit of natural stock (a resource cell or a pooled farm tile)
// for one worker and routes that worker to it, so a crowd spreads over the
// stock that is actually there. Supplier (market) fetches keep ordinary movement.
//
// Claims bind claimers only. The ledger is shared by all teams, so one team's
// claims block another team's claimers; but an unclaimed worker of any team
// still harvests claimed stock, and the resulting stock drop evicts the claims
// it no longer covers, highest owner ID first.
//
// Changing any value here changes the simulation: bump SIM_REVISION, regenerate
// the golden match record, and change Name (development builds used
// "contention-v1" with different values; a name must identify one policy).
namespace ResourceClaims::Policy
{
inline constexpr std::string_view Name="contention-v2";

// Claims engage only under contention. A worker without a claim moves exactly
// as with claims off, and switches to claim selection for a decision on which
// at least ContentionMinWorkers same-team workers fetching the same material
// (itself included) target cells within ContentionRadius tiles (toroidal
// Chebyshev) of its own target. Uncrowded workers therefore keep the cheaper,
// already well-tuned gradient movement, and claims pay their selection and
// routing cost only where workers would otherwise collide.
inline constexpr unsigned ContentionMinWorkers=2;
inline constexpr unsigned ContentionRadius=3;

// A worker holding a claim re-runs selection every this many movement
// decisions and takes the first claimable entry in pattern order, which may be
// the claim it already holds (then its route is kept). A claim taken early in a
// long walk therefore does not pin the worker once better-placed stock frees
// up. Between refreshes it keeps its target and route.
inline constexpr unsigned DestinationRefreshDecisions=24;

// A candidate is rejected when its toroidal Chebyshev distance d from the
// worker exceeds this factor of the distance to the nearest destination
// recorded at the worker's cell, plus a fixed slack: reject when
// 1000*d > MaxClaimDistanceFactorPermille*d_nearest + 1000*ClaimDistanceSlackTiles.
// Spreading a crowd must not send workers across the map for marginal stock.
// A decision with no recorded nearest destination is not capped.
inline constexpr unsigned MaxClaimDistanceFactorPermille=2500;
inline constexpr unsigned ClaimDistanceSlackTiles=4;

// After this many consecutive decisions waiting behind traffic on its route,
// a worker replans from its current cell around the units it sees now. A
// replan keeps the reservation; releasing would also reselect and reroute.
inline constexpr unsigned TrafficWaitLimit=8;

// Bound on A* node expansions for one claim route search.
inline constexpr unsigned RouteMaximumExpansions=4096;
}

namespace ResourceClaims
{
// Per-unit selection cadence, saved and checksummed with claim state.
struct MaterialClaimPolicyState
{
    // Movement decisions taken by this worker in claim mode, and the ordinal of
    // the last decision that ran selection (for DestinationRefreshDecisions).
    std::uint64_t decisionOrdinal=0,lastGoalAttempt=0;
    bool operator==(const MaterialClaimPolicyState&) const = default;
};
}
