// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once
#include "CortexSnapshotQueries.h"
class Player;

#include "CortexTypes.h"


// AICortex observation layer reads canonical immutable snapshot records and
// projects them into the policy feature vector. Planning fields read the
// controller's sparse intent overlay; query scratch stays controller-owned.
//
// Fairness: the engine does NOT enforce fog-of-war on AI reads (see
// AIImplementation.h). Any enemy field this layer exposes must be gated on
// visibility here, in one place, so the policy can never learn to exploit a
// leak we forgot to close.

namespace Cortex
{
	/// Project the player's current game state into a fixed feature vector.
	/// Returns an observation with version == OBSERVATION_VERSION and valid == 1.
	/// `openMargin` is the per-game food open-margin N (drawn once from the controller stream in
	/// AICortex); it is runtimeed into obs.wheatOpenMargin and drives the food scan.
	/// `offenseFlagGid` is AICortex's tracked OFFENSE war-flag gid (NOGBID == none):
	/// the building scan captures THAT flag's footprint specifically (Cortex runs two
	/// flags now — offense + defense — so a bare "last WAR_FLAG wins" capture would be
	/// ambiguous), so the enemy-straggler and own-warriors-near-flag passes measure the
	/// offense front, which is what the retire/retreat decisions reason about.
	CortexObservation observeWorld(MersenneTwister& random, const AIEngine::AIWorldView* game, const AIEngine::TeamView* team, QueryScratch& scratch, const PlanningIntent& intents, std::ostream* diagnostics, int openMargin, Uint16 offenseFlagGid);

	/// Internal observe() helper: the single index pass over game->buildingSlots(team->number)
	/// that fills the building-derived signals (feedCapacity, swarm/inn tracking,
	/// upgradable counts, construction sites) and captures the OFFENSE war flag's
	/// footprint into warFlag* (the flag whose gid == offenseFlagGid) for the later
	/// enemy-straggler / own-warriors passes. Split out of observe() purely so each
	/// .cpp stays under the file-size cap; the iteration order is lockstep-determinism-
	/// critical and is preserved verbatim. Called exactly once, at the same point
	/// observe() previously ran the loop inline.
	void observeBuildings(CortexObservation& obs, const AIEngine::TeamView* team, const AIEngine::AIWorldView* game, const PlanningIntent& intents,
		int maxBuildLevel, Uint16 offenseFlagGid, bool& warFlagFound,
		Sint32& warFlagX, Sint32& warFlagY, Sint32& warFlagRange);
}

namespace Cortex {
CortexObservation observe(MersenneTwister& random, ::Player* player, int margin, Uint16 gid);
void observeBuildings(CortexObservation&, ::Team*, ::Game*, int, Uint16, bool&, Sint32&, Sint32&, Sint32&);
}
