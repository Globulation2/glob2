// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include "SDL.h"
#include <string>
#include <map>
#include <optional>
#include <vector>

///This namespace stores everything related to the key actions that can occur at key-press
///in GameGUI
namespace GameGUIKeyActions
{
	///Initiates the names and reverse names for each key action
	void init();

	///This is the enum of key actions
	enum
	{
		DoNothing = 0,
		ShowMainMenu,
		UpgradeBuilding,
		IncreaseUnitsWorking,
		DecreaseUnitsWorking,
		OpenChatBox,
		IterateSelection,
		GoToEvent,
		GoToHome,
		PauseGame,
		HardPause,
		ToggleDrawUnitPaths,
		DestroyBuilding,
		RepairBuilding,
		ToggleDrawInformation,
		ToggleDrawAccessibilityAids,
		MarkMap,
		ToggleRecordingVoice,
		ViewHistory,
		SelectConstructSwarm,
		SelectConstructInn,
		SelectConstructHospital,
		SelectConstructRacetrack,
		SelectConstructSwimmingPool,
		SelectConstructBarracks,
		SelectConstructSchool,
		SelectConstructDefenceTower,
		SelectConstructStoneWall,
		SelectConstructMarket,
		SelectPlaceExplorationFlag,
		SelectPlaceWarFlag,
		SelectPlaceClearingFlag,
		SelectPlaceForbiddenArea,
		SelectPlaceGuardArea,
		SelectPlaceClearingArea,
		SwitchToAddingAreas,
		SwitchToRemovingAreas,
		SwitchToAreaBrush1,
		SwitchToAreaBrush2,
		SwitchToAreaBrush3,
		SwitchToAreaBrush4,
		SwitchToAreaBrush5,
		SwitchToAreaBrush6,
		SwitchToAreaBrush7,
		SwitchToAreaBrush8,
		ToggleTorusView,
		ActionSize,
	};

	///Gets the name of a key-action from the integer
	const std::string getName(Uint32 action);
	
	///Reverses a name of a key action back to its integer.
	///Returns std::nullopt if the name is not a known key action.
	std::optional<Uint32> getAction(const std::string& name);
	
	///Returns the name of the file for the default configuration
	std::string getDefaultConfigurationFile();
	
	///Returns the name of the file for the personal configuration
	std::string getConfigurationFile();
	
	extern std::vector<std::string> names;
	extern std::map<std::string, Uint32> keys;
};

