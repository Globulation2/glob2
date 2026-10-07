// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once
#include "CortexSnapshotQueries.h"
class Player;

#include "CortexTypes.h"


// AICortex placement helper. This is the one piece of spatial reasoning the
// direct (AIImplementation) binding does not inherit from Runtime — see
// docs/AI/cortex/NEXT.md "Verdict on open question #1". It answers a single
// question: "where could I put a building of this type?", ranked best-first.
//
// It lives on the observation side of the three-layer split: Cortex::observeWorld()
// calls it to fill CortexObservation::buildCandidates, so the policy only ever
// chooses among surfaced slots (keeping the action space discrete and bounded).
// It reads canonical snapshot records and pure map queries; the policy receives
// only the resulting bounded feature slots.

namespace Cortex
{
	/// Fill `out` with up to CORTEX_BUILD_CANDIDATES ranked candidate locations
	/// for placing a building of `buildingType` (a Cortex semantic role) at
	/// internal level `level` (0-based; use 0 for a fresh building) for `team`.
	///
	/// Candidates are returned best-first (highest BuildCandidate::score in slot
	/// 0). Unused trailing slots are zeroed with valid == 0. (x, y) is the tile
	/// of the building footprint's top-left corner, suitable for an OrderCreate.
	///
	/// Returns the number of valid candidates written (0..CORTEX_BUILD_CANDIDATES).
	/// Returns 0 (and leaves all slots valid == 0) when no legal placement exists.
	/// Pass a qualification computed for the current observation to avoid rescanning
	/// workers for each role. The default computes it for standalone callers.
	int placeCandidates(const AIEngine::AIWorldView* game, const AIEngine::TeamView* team, QueryScratch& scratch, const PlanningIntent& intents, int buildingType, int level,
	                    BuildCandidate out[CORTEX_BUILD_CANDIDATES], int placementType = -1,
	                    int maxWorkerQualification = -1);

	/// Forward-base variant of placeCandidates: the single best legal spot for
	/// `buildingType` whose distance to the attack target (targetX, targetY) lies
	/// in [minTargetDist, maxTargetDist] — near enough that the finished building
	/// brings the target inside the attack-range support envelope, far enough not
	/// to be built under enemy fire. All normal legality gates for the type still
	/// apply (a forward inn still needs harvestable food at the front); only the
	/// stay-clustered-with-the-colony cap is lifted. Among legal spots the one
	/// closest to the colony wins (safest that does the job). Returns 1 and fills
	/// `out`, or 0 (out.valid == 0) when no legal forward spot exists.
	int placeForwardCandidate(const AIEngine::AIWorldView* game, const AIEngine::TeamView* team, QueryScratch& scratch, const PlanningIntent& intents, int buildingType,
	                          int targetX, int targetY,
	                          int minTargetDist, int maxTargetDist,
	                          BuildCandidate& out, int maxWorkerQualification = -1);

	/// Fill `out` with up to CORTEX_FLAG_TARGETS DISCOVERED enemy buildings, ranked
	/// nearest-first to our colony, to serve as war-flag offense targets. Each
	/// BuildCandidate's (x, y) is the enemy building's tile (its center/posX,posY,
	/// the coordinate an OrderCreate for a WAR_FLAG consumes) and `score` ranks
	/// proximity (nearer == higher, slot 0 is the closest reachable target).
	/// `outTeam[i]` receives the team owning slot i (-1 when invalid), for telemetry.
	///
	/// FAIRNESS: only buildings the team has legitimately seen are included —
	/// gate strictly on Building::seenByMask & team->mask (the engine's own per-
	/// building discovery record). NEVER read unfogged enemy state. Iterate enemy
	/// myBuildings[] by index (never an std::set); break ties deterministically by
	/// scan order / syncRand(), exactly as placeCandidates does.
	///
	/// Returns the number of valid targets written (0..CORTEX_FLAG_TARGETS); 0 when
	/// we have not yet discovered any enemy building.
	int placeFlagTargetsWorld(const AIEngine::AIWorldView* game, const AIEngine::TeamView* team, const PlanningIntent& intents, BuildCandidate out[CORTEX_FLAG_TARGETS], Sint32 outTeam[CORTEX_FLAG_TARGETS]);

	/// Chebyshev distance from tile (x, y) to the nearest food tile, found
	/// by an outward radial scan bounded at `cap` rings. Returns the distance in
	/// [0, cap], or -1 when no Food lies within `cap` tiles. Warp-safe (uses Map's
	/// coordinate normalization). Deterministic (fixed scan order, no rand). Shared
	/// by placeCandidates (a candidate site's BuildCandidate::foodSourceDistance) and
	/// Cortex::observe (a tracked swarm/inn's TrackedBuilding::nearestFoodSourceDistance), so
	/// the food-distance metric is defined in exactly one place. Pass
	/// CORTEX_WHEAT_SCAN_CAP for `cap`.
	int nearestFoodSourceDistance(const AIEngine::AIWorldView& map, int x, int y, int cap);
}

namespace Cortex {
int placeFlagTargets(::Game*,::Team*,BuildCandidate out[CORTEX_FLAG_TARGETS],Sint32 teams[CORTEX_FLAG_TARGETS]);
}
