// SPDX-License-Identifier: GPL-3.0-or-later
// The GlobalContainer bootstrap that every headless engine harness used to repeat.
// HeadlessGame.h adds a one-colony game, GameFixtures.h helpers over any Game and
// DrawFixtures.h draw passes.
#pragma once

#include "Glob2Test.h"

#include "GlobalContainer.h"

#include <functional>
#include <string>

namespace glob2test
{
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
}
