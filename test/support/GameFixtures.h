// SPDX-License-Identifier: GPL-3.0-or-later
// Helpers for tests that drive a Game directly, with or without a GameGUI: the bound
// synchronized stream, the deterministic AI scheduler, and published hiring and
// turret inputs. Scene extraction and draw passes are in DrawFixtures.h.
#pragma once

#include "Glob2Test.h"

#include "Game.h"

#include <string>
#include <vector>

namespace glob2test
{
    // Controlled admission fixtures use fresh planner suggestions, independently
    // of scheduling. Continuation/pipeline tests exercise real tick deadlines.
    void publishCurrentHiringSuggestions(Game&);
    // Remove only premature injected buffers before testing normal persistence.
    void discardPrematureHiringSuggestions(Game&);
    // Isolated weapon fixtures advance observation/publication without stepping units.
    void prepareTurretTargets(Building&);
	// Deterministic continuation scheduler: collect each unpaused AI decision,
	// then apply in player order and advance one tick. Returns type+wire payload
	// per AI so paired continuations can compare decisions as well as state.
	std::vector<std::string> stepAI(Game &game);

	// Binds a game's synchronized stream on this thread, as the engine does around
	// simulation entry points, for fixtures that call game functions directly. The
	// stream continues from the current process stream, so fixtures that seed with
	// setSyncRandSeed before building the game keep their sequences.
	struct BoundGameRandom
	{
		explicit BoundGameRandom(Game& game) : scope((game.syncRandom = syncRandEngine(), game.syncRandom)) {}
		SyncRandScope scope;
	};
}
