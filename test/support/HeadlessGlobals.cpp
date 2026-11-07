// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadlessGlobals.h"

#include "FileManager.h"
#include "GameGUIKeyActions.h"
#include "IntBuildingType.h"
#include "MapEditKeyActions.h"
#include "PerformanceTelemetry.h"
#include "Race.h"
#include "Utilities.h"

namespace glob2test
{
	HeadlessGlobals::HeadlessGlobals(Options options)
		: globals(options.profileName.c_str())
	{
		REQUIRE_MESSAGE(globalContainer == nullptr, "a HeadlessGlobals is already live in this process");
		globalContainer = &globals;
		if (options.addSourceRoot)
			globals.fileManager->addDir(sourceRoot().string());
		globals.runNoX = !options.display;
		globals.settings.rememberUnit = false;
		globals.settings.mute = 1;
		globals.settings.screenWidth = options.width;
		globals.settings.screenHeight = options.height;
		globals.settings.screenFlags = options.screenFlags;
		// Tests read English text unless they choose otherwise, whatever the host's locale.
		if (globals.settings.language.empty())
			globals.settings.language = "en";
		if (options.beforeLoad)
			options.beforeLoad(globals);
		if (options.display || options.loadStrings)
			globals.load();
		else
		{
			globals.buildingsTypes.init();
			IntBuildingType::init();
			Race::loadDefault();
			GameGUIKeyActions::init();
			MapEditKeyActions::init();
		}
		if (options.seed != 0)
			setSyncRandSeed(options.seed);
	}

	HeadlessGlobals::~HeadlessGlobals()
	{
		PerformanceTelemetry::collector().reset();
		globalContainer = nullptr;
	}
}
