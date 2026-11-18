// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadlessGlobals.h"

#include "DeviceProfile.h"
#include "FileManager.h"
#include <GraphicContext.h>
#include "GameGUIKeyActions.h"
#include "IntBuildingType.h"
#include "MapEditKeyActions.h"
#include "OnlineServices.h"
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
		// The destructor does not run when the constructor throws (a failed load(),
		// a throwing beforeLoad): release the process-wide state here instead, or
		// every later case in the process would find a stale globalContainer.
		try
		{
			configure(options);
		}
		catch (...)
		{
			releaseProcessState();
			throw;
		}
	}

	void HeadlessGlobals::configure(const Options &options)
	{
		if (options.addSourceRoot)
			globals.fileManager->addDir(sourceRoot().string());
		globals.runNoX = !options.display;
		globals.terrainSheetsEager = !options.terrainSheetsOnDemand;
		globals.settings.rememberUnit = false;
		globals.settings.mute = 1;
		globals.settings.screenWidth = options.width;
		globals.settings.screenHeight = options.height;
		globals.settings.screenFlags = options.screenFlags;
		// Fixtures choose their renderer through the window flags; pin it so that the
		// desktop Auto policy (Legacy GL) does not replace a requested Software surface.
		globals.settings.rendererBackend =
			(options.screenFlags & GAGCore::GraphicContext::PORTABLEGPU) ? GAGCore::RendererBackend::PortableSDL :
			(options.screenFlags & GAGCore::GraphicContext::USEGPU) ? GAGCore::RendererBackend::LegacyGL :
			GAGCore::RendererBackend::Software;
		// Device-adaptive defaults follow the high tier (today's defaults) whatever
		// the runner's memory or cores; tests of adaptation replace the profile.
		DeviceProfile::replace(DeviceProfile::classify(DeviceProfile::current().inputs, DeviceTier::High, "test"));
		globals.settings.resolveAdaptiveDefaults();
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
		releaseProcessState();
	}

	void HeadlessGlobals::releaseProcessState()
	{
		// The online storage and map cache hold the file manager that globals
		// closes with Toolkit; the next case must not reach this one's.
		Online::releaseProcessServices();
		PerformanceTelemetry::collector().reset();
		globalContainer = nullptr;
	}
}
