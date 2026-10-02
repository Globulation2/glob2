// SPDX-License-Identifier: GPL-3.0-or-later
// Fixtures for tests that link the real client: the GlobalContainer bootstrap that
// every headless harness used to repeat, and a one-colony game on a small torus.
#pragma once

#include "Glob2Test.h"

#include "Building.h"
#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Map.h"
#include "Team.h"
#include "Unit.h"

#include <functional>
#include <optional>
#include <string>

namespace glob2test
{
	// Binds a game's synchronized stream on this thread, as the engine does around
	// simulation entry points, for fixtures that call game functions directly. The
	// stream continues from the current process stream, so fixtures that seed with
	// setSyncRandSeed before building the game keep their sequences.
	struct BoundGameRandom
	{
		explicit BoundGameRandom(Game& game) : scope((game.syncRandom = syncRandEngine(), game.syncRandom)) {}
		SyncRandScope scope;
	};

	struct GlobalsOptions
	{
		bool display = false;        // create a real window and load graphics
		bool loadStrings = false;    // run GlobalContainer::load() (texts, key layouts)
		int width = 640, height = 480;
		Uint32 screenFlags = 0;
		Uint32 seed = 0;             // nonzero: setSyncRandSeed(seed); zero leaves the default state
		std::string profileName = "glob2-tests";
		bool addSourceRoot = true;   // data/ and maps/ resolve from any working directory
		// Runs after the settings above are applied and before load(): language,
		// extra data directories, anything the old harness set before loading.
		std::function<void(GlobalContainer&)> beforeLoad;
	};

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
		// The seed and experiments below only take effect with it.
		bool header = false;
		Uint32 seed = 1;              // GameHeader otherwise seeds from the wall clock
		ExperimentSet experiments;    // the experiments the game carries (ExperimentalFeatures.h)
	};

	// Owns the process-wide GlobalContainer for the scope of a test case. Headless by
	// default: no window, no sprites, no sound, building types and races initialised,
	// the repository root on the data search path and the simulation RNG seeded.
	struct HeadlessGlobals
	{
		using Options = GlobalsOptions;

		GlobalContainer globals;

		explicit HeadlessGlobals(Options options = {});
		~HeadlessGlobals();
		HeadlessGlobals(const HeadlessGlobals&) = delete;
		HeadlessGlobals& operator=(const HeadlessGlobals&) = delete;

		GlobalContainer* operator->() { return &globals; }
		GlobalContainer& operator*() { return globals; }
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
		// One simulation tick per call: Game::syncStep for the local team.
		void step(int ticks = 1);
		Uint32 checksum();
	};
}
