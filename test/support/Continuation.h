// SPDX-License-Identifier: GPL-3.0-or-later
// Save/load continuation checks for engine tests: save a game, load the bytes into a
// fresh GameGUI, step both in lockstep and compare their complete simulation
// component vectors, synchronized RNG streams and AI orders at every tick. Needs a
// live HeadlessGlobals; linked into glob2-engine-tests only (it builds GameGUIs).
#pragma once

#include "GameFixtures.h"

#include "GameGUI.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace glob2test
{
	// Game::checkSum's per-component words: game fields (map header, teams, players,
	// map, scripts, statistics, catalog), then every building, then every unit.
	// legacy152Representation selects the frozen trace layout historical goldens use
	// (ChecksumScheme::Legacy152) and drops its map-header word, which mixes in the
	// format version saving rewrites.
	std::vector<Uint32> simulationComponents(Game& game, bool legacy152Representation = false);

	// Game::save into memory, binary unless text.
	std::string saveGameBytes(Game& game, const std::string& name = "continuation", bool text = false);
	// GameGUI::save into memory: the game, then the GUI section.
	std::string saveGUIBytes(GameGUI& gui, const std::string& name = "continuation");
	// Game::load from saveGameBytes output, in place.
	bool loadGameBytes(Game& game, const std::string& bytes, bool text = false);
	// A fresh GameGUI holding the loaded bytes (GameGUI::load when throughGUI, which
	// also reads the GUI section). REQUIREs the load to succeed.
	std::unique_ptr<GameGUI> loadGameGUI(const std::string& bytes, bool throughGUI = false);

	// One engine tick, as HeadlessGame::step does: Game::step, then the owning GUI
	// consumes its client events.
	void stepEngine(Game& game);

	struct ContinuationOptions
	{
		// Ticks to step both games after loading.
		int ticks = 0;
		// Advances a game by one tick and returns its decisions (stepAI's type+wire
		// payloads), compared between the games. Runs with that game's synchronized
		// stream bound. The default is Game::step(0) with no orders.
		std::function<std::vector<std::string>(Game&)> step;
		// Compare the two games right after loading, before the first step.
		bool compareLoaded = true;
		// Compare two loads of the save with each other, instead of the original with a
		// load, for code that deliberately rebuilds unsaved state on load.
		bool twoLoads = false;
		// Save and load through GameGUI (GUI section included); Game::save otherwise.
		GameGUI* gui = nullptr;
		std::string name = "continuation";
		// Runs on each loaded game before any comparison, with the original game:
		// restore what a real loader applies after Game::load, or check the loaded
		// state against the original.
		std::function<void(Game& original, Game& loaded)> afterLoad;
		// Extra comparisons at each compared point, after the built-in ones: tick -1
		// right after loading (with compareLoaded), then 0..ticks-1 after each step.
		std::function<void(Game& reference, Game& loaded, int tick)> each;
	};

	struct ContinuationResult
	{
		std::unique_ptr<GameGUI> reference; // twoLoads only: the first load
		std::unique_ptr<GameGUI> loaded;
		bool matched = true;
		int divergedAt = -2; // tick of the first mismatch (-1: right after loading)
	};

	// Saves `original`, loads the bytes, then steps the reference (the original, or a
	// first load with twoLoads) and the load in lockstep for options.ticks ticks. At
	// each compared point checks components, syncRandom and the step's orders; the
	// first mismatch fails the test with the tick and the first differing component,
	// and the comparison stops there. Returns the loaded games for further checks.
	ContinuationResult expectContinuation(Game& original, const ContinuationOptions& options);
}
