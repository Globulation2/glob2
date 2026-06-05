// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include "CortexTypes.h"

class Game;
class Team;
class Map;

// AICortex placement helper. This is the one piece of spatial reasoning the
// direct (AIImplementation) binding does not inherit from Echo — see
// docs/AI/cortex/NEXT.md "Verdict on open question #1". It answers a single
// question: "where could I put a building of this type?", ranked best-first.
//
// It lives on the observation side of the three-layer split: Cortex::observe()
// calls it to fill CortexObservation::buildCandidates, so the policy only ever
// chooses among surfaced slots (keeping the action space discrete and bounded).
// It reads Game*/Team*/Map* freely; the policy never sees those types.

namespace Cortex
{
	/// Fill `out` with up to CORTEX_BUILD_CANDIDATES ranked candidate locations
	/// for placing a building of `buildingType` (an IntBuildingType::Number) at
	/// internal level `level` (0-based; use 0 for a fresh building) for `team`.
	///
	/// Candidates are returned best-first (highest BuildCandidate::score in slot
	/// 0). Unused trailing slots are zeroed with valid == 0. (x, y) is the tile
	/// of the building footprint's top-left corner, suitable for an OrderCreate.
	///
	/// Returns the number of valid candidates written (0..CORTEX_BUILD_CANDIDATES).
	/// Returns 0 (and leaves all slots valid == 0) when no legal placement exists.
	int placeCandidates(Game* game, Team* team, int buildingType, int level,
	                    BuildCandidate out[CORTEX_BUILD_CANDIDATES]);

	/// Fill `out` with up to CORTEX_FLAG_TARGETS DISCOVERED enemy buildings, ranked
	/// nearest-first to our colony, to serve as war-flag offense targets. Each
	/// BuildCandidate's (x, y) is the enemy building's tile (its center/posX,posY,
	/// the coordinate an OrderCreate for a WAR_FLAG consumes) and `score` ranks
	/// proximity (nearer == higher, slot 0 is the closest reachable target).
	///
	/// FAIRNESS: only buildings the team has legitimately seen are included —
	/// gate strictly on Building::seenByMask & team->me (the engine's own per-
	/// building discovery record). NEVER read unfogged enemy state. Iterate enemy
	/// myBuildings[] by index (never an std::set); break ties deterministically by
	/// scan order / syncRand(), exactly as placeCandidates does.
	///
	/// Returns the number of valid targets written (0..CORTEX_FLAG_TARGETS); 0 when
	/// we have not yet discovered any enemy building.
	int placeFlagTargets(Game* game, Team* team, BuildCandidate out[CORTEX_FLAG_TARGETS]);

	/// Chebyshev distance from tile (x, y) to the nearest CORN (wheat) tile, found
	/// by an outward radial scan bounded at `cap` rings. Returns the distance in
	/// [0, cap], or -1 when no CORN lies within `cap` tiles. Warp-safe (uses Map's
	/// coordinate normalization). Deterministic (fixed scan order, no rand). Shared
	/// by placeCandidates (a candidate site's BuildCandidate::wheatDist) and
	/// Cortex::observe (a tracked swarm/inn's TrackedBuilding::nearestWheatDist), so
	/// the wheat-distance metric is defined in exactly one place. Pass
	/// CORTEX_WHEAT_SCAN_CAP for `cap`.
	int nearestCornDist(const Map& map, int x, int y, int cap);
}
