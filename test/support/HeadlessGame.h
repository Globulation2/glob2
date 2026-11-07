// SPDX-License-Identifier: GPL-3.0-or-later
// A one-colony game on a small torus, owned by the GameGUI that steps it. Needs a
// live HeadlessGlobals (HeadlessGlobals.h).
#pragma once

#include "GameFixtures.h"

#include "GameGUI.h"
#include "Team.h"

#include <optional>

namespace glob2test
{
	struct GameOptions
	{
		int wDec = 5, hDec = 5;
		TerrainType terrain = GRASS;
		int teams = 1;
		bool discovered = false;      // reveal the whole map to every team
		bool clearImmobile = false;   // clear immobile-unit bookkeeping on every tile
		bool loadDefaultRace = false; // team->race.loadDefault() for each team
		// Install a GameHeader with one local player per team, the way the game
		// loader does: orders then apply to the team, painted areas reach the
		// displayed view, and a save of the game reloads to the same checksums.
		// Experiments below only take effect with it.
		bool header = false;
		Uint32 seed = 1;              // deterministic entity seed, also without a player header
		ExperimentSet experiments;    // the experiments the game carries (ExperimentalFeatures.h)
	};

	// A game with one team on a 2^wDec x 2^hDec torus. Requires a live HeadlessGlobals.
	struct HeadlessGame
	{
		using Options = GameOptions;

		GameGUI gui;
		Game& game;
		// Binds the game's synchronized stream for the test's lifetime, so fixture code
		// that calls game functions directly draws from the game, as the engine does.
		std::optional<BoundGameRandom> random;
		Team* team = nullptr;
		int parked = 0;

		explicit HeadlessGame(Options options = {});
		HeadlessGame(const HeadlessGame&) = delete;
		HeadlessGame& operator=(const HeadlessGame&) = delete;

		// Game::addBuilding plus the map footprint registration the engine does separately.
		Building* addBuilding(const char* typeName, int x, int y, int level = 0, int teamNumber = 0);
		// A unit at (x, y), or parked on a free tile in the lower-right quadrant.
		Unit* addUnit(int typeNum, int x = -1, int y = -1, int teamNumber = 0, int level = 0);
		// One simulation tick per call: Game::step for the local team, then
		// GameGUI::consumeClientEvents as the engine does.
		void step(int ticks = 1);
		Uint32 checksum();
	};
}
