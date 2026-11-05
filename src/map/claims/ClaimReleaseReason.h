// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

namespace ResourceClaims
{
// Why a unit released its held material claim. Observer telemetry only: the
// reason never affects simulation state, checksums or saved data. Replacing a
// held claim with another one is not a release; it is the "retargeted" event.
enum class ReleaseReason : unsigned {
    Harvest,            // Reservation consumed by the pickup itself.
    RouteFailed,        // Claim route search terminally failed.
    UnitStateChanged,   // Validity: dead, medical, job/activity/displacement or carry ability changed.
    ResourceDepleted,   // Validity: source type, stock or seed reserve no longer covers the claim.
    AccessInvalid,      // Validity: access is no longer an eligible destination or reachable stance.
    PickupMismatch,     // Pickup attempted at a cell other than the held access.
    JobEnded,           // Building detach, reassignment, new subscription or random activity.
    Medical,            // Medical recheck (hunger or healing) interrupted the job.
    UnitRemoved,        // Death, destruction or conversion to another team.
    Serialization,      // Unit reload; claims are rebuilt from saved state.
    MapInvalidation,    // Source/access cell change invalidated the owner.
    Configuration,      // Claim policy reconfiguration before the game starts.
    Other,
    Count
};
inline constexpr std::array<const char*,unsigned(ReleaseReason::Count)> ReleaseReasonNames{
    "harvest","route_failed","unit_state_changed","resource_depleted","access_invalid","pickup_mismatch",
    "job_ended","medical","unit_removed","serialization","map_invalidation","configuration","other"};
}
