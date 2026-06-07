// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include "CortexTypes.h"

class Player;
class Team;
class Game;

// AICortex observation layer. This is the ONLY place that reads live engine
// state (Game*/Team*/Map*) to fill a CortexObservation. Both the direct
// (AIImplementation) and Echo (EchoAI) bindings call this same function, so the
// feature surface is identical regardless of parent class — the whole point of
// the parent-class spike is that only the *action* path differs, not this one.
//
// Fairness: the engine does NOT enforce fog-of-war on AI reads (see
// AIImplementation.h). Any enemy field this layer exposes must be gated on
// visibility here, in one place, so the policy can never learn to exploit a
// leak we forgot to close.

namespace Cortex
{
	/// Project the player's current game state into a fixed feature vector.
	/// Returns an observation with version == OBSERVATION_VERSION and valid == 1.
	/// `openMargin` is the per-game wheat open-margin N (drawn once via syncRand in
	/// AICortex); it is echoed into obs.wheatOpenMargin and drives the wheat scan.
	CortexObservation observe(Player* player, int openMargin);

	/// Internal observe() helper: the single index pass over team->myBuildings
	/// that fills the building-derived signals (feedCapacity, swarm/inn tracking,
	/// upgradable counts, construction sites) and captures the live war flag's
	/// footprint into warFlag* for the later enemy-straggler pass. Split out of
	/// observe() purely so each .cpp stays under the file-size cap; the iteration
	/// order is lockstep-determinism-critical and is preserved verbatim. Called
	/// exactly once, at the same point observe() previously ran the loop inline.
	void observeBuildings(CortexObservation& obs, Team* team, Game* game,
		int maxBuildLevel, bool& warFlagFound,
		Sint32& warFlagX, Sint32& warFlagY, Sint32& warFlagRange);
}
