// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include "CortexTypes.h"

class Game;
class Team;

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
}
