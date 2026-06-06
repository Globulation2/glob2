// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include <SDL_stdinc.h>

class Player;

// AICortex swim/water assessment. Lives on the observation side of the three-
// layer split (like CortexPlacement / CortexWheat): Cortex::observe() calls it to
// fill the swim-decision fields of a CortexObservation, so the pure policy only
// ever reads bounded scalars and never touches Game*/Team*/Map*.
//
// It answers two questions the swimming-pool (SWIMSPEED_BUILDING) decision needs:
//   1. Has an explorer revealed reachable ALGA? Algae is a basic food resource
//      that grows only on water, so it is harvestable only by units that can
//      swim — a direct reason to train SWIM.
//   2. Does learning to swim materially expand the colony's reachable area? We
//      flood-fill the colony's vicinity twice — once treating water as an
//      obstacle (a non-swimmer) and once treating it as passable (a swimmer) —
//      and compare the two tile counts. A large gap means swimming opens up
//      water-separated land (fresh wheat patches across a channel, a shorter or
//      only route to a water-locked enemy). This mirrors the INTENT of AICastor's
//      computeNeedSwim (ai/castor/State.cpp:82-109).

namespace Cortex
{
	/// Result of the swim/water assessment. POD; copied straight into the
	/// observation's algaeDiscovered / swimLandReach / swimWaterReach / algaeReachable
	/// fields.
	struct SwimAssessment
	{
		Sint32 algaeDiscovered; ///< 1 if a takeable, FOW-discovered ALGA tile exists.
		Sint32 landReach;       ///< tiles reachable from the colony WITHOUT swim.
		Sint32 waterReach;      ///< tiles reachable from the colony WITH swim (>= landReach).
		Sint32 algaeReachable;  ///< 1 if a discovered takeable ALGA tile is 8-adjacent to
		                        ///< a tile reachable WITHOUT swim — harvestable from shore
		                        ///< now, so ALGA-consuming builds (the school) can be fed.
	};

	/// Compute the swim assessment for `player`'s team. Deterministic (fixed scan
	/// order, no rand, no pointer reads, warp-safe) so it is safe inside lockstep.
	/// Returns all-zero when the player/team/game/map is unavailable or the team has
	/// no real building to anchor the reach flood-fill on.
	///
	/// algaeDiscovered and algaeReachable are always computed (the school gate needs
	/// the latter throughout the game). The land/water reach COUNTS are only needed by
	/// the one-shot swimming-pool decision, which never fires once a pool exists, so
	/// they are computed only when `wantSwimReach` is true (the extra swim-pass fill is
	/// skipped otherwise, leaving both counts 0); algaeReachable's ground-pass fill runs
	/// regardless.
	SwimAssessment assessSwim(Player* player, bool wantSwimReach);
}
