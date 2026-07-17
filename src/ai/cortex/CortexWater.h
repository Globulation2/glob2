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

	/// Result of the amphibious-campaign assessment for one (rally -> target) push.
	/// POD; the caller copies it into the observation (obs.campaign*/landingZone*).
	struct AmphibiousAssessment
	{
		Sint32 amphibious;   ///< 1 if the shortest path to the target's land region crosses water.
		Sint32 landDist;     ///< BFS hop distance rally->target with canSwim=false; -1 == unreachable.
		Sint32 swimDist;     ///< BFS hop distance rally->target with canSwim=true;  -1 == unreachable.
		Sint32 landingValid; ///< 1 if a landing zone was found (only meaningful when amphibious==1).
		Sint32 landingX;     ///< Landing-zone tile x (a shore tile in the target's land component). Valid iff landingValid.
		Sint32 landingY;     ///< Landing-zone tile y.
	};

	/// Classify the (rally -> target) offense campaign and, when it crosses water,
	/// pick a landing zone. Two full-map BFS from the colony rally (the same passability
	/// predicate family as countReach — Map::isHardSpaceForGroundUnit with the canSwim
	/// toggle, warp-safe, plain FIFO, no std::set / floats / RNG): landDist with
	/// canSwim=false and swimDist with canSwim=true, each the hop distance to the
	/// target's 8-neighbourhood (the target tile is an enemy-building footprint, so —
	/// like countReach's anchor seed — the target is "reached" when the BFS touches any
	/// tile 8-adjacent to it).
	///
	/// AMPHIBIOUS iff swimDist is reachable AND (landDist unreachable OR swimDist <
	/// landDist): a land-only path is also a valid swim path, so a swimmer never does
	/// worse than a walker; therefore swimDist < landDist holds exactly when the true
	/// shortest path crosses water (the user-approved formulation).
	///
	/// For an amphibious campaign it also runs a third BFS from the TARGET (canSwim=false)
	/// to isolate the target's land COMPONENT, then picks the landing zone: a walkable
	/// component tile 8-adjacent to water (a shore where swimmers climb out), at least
	/// `landingStandoffTiles` (warp-safe Chebyshev) from every discovered enemy building
	/// in standoffX/standoffY[0..standoffCount) (the observation's obs.flagTargets[]),
	/// minimizing swim-BFS distance from the rally, tie-broken by lowest flattened index.
	/// If no tile clears the standoff, the best shore tile ignoring standoff is used; if
	/// the component has no reachable shore tile at all, landingValid stays 0 (the campaign
	/// cannot be amphibious-assaulted and the caller falls back to today's behavior).
	///
	/// The third BFS + landing scan run ONLY on the amphibious branch, so a land campaign
	/// costs exactly two BFS. Deterministic and safe inside lockstep. Returns all-zero /
	/// unreachable when the player/team/game/map is unavailable or the team has no anchor.
	AmphibiousAssessment assessAmphibious(Player* player, int targetX, int targetY,
	                                      const Sint32* standoffX, const Sint32* standoffY,
	                                      int standoffCount, int landingStandoffTiles);
}
