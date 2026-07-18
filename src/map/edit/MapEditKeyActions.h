// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include "SDL.h"
#include <string>
#include <map>
#include <optional>
#include <vector>

///This namespace stores everything related to the key actions that can occur at key-press
///in MapEdit
namespace MapEditKeyActions
{
	///Initiates the names and reverse names for each key action
	void init();

	///This is the enum of key actions
	enum
	{
		DoNothing = 0,
		SwitchToBuildingView,
		SwitchToFlagView,
		SwitchToTerrainView,
		SwitchToTeamsView,
		OpenSaveScreen,
		OpenLoadScreen,
		SelectSwarm,
		SelectInn,
		SelectHospital,
		SelectRacetrack,
		SelectSwimmingpool,
		SelectSchool,
		SelectBarracks,
		SelectTower,
		SelectStonewall,
		SelectMarket,
		SelectExplorationFlag,
		SelectWarFlag,
		SelectClearingFlag,
		ToggleMenuScreen,
		SelectDeleteTool,
		ActionSize,
	};

	///Gets the name of a key-action from the integer
	const std::string getName(Uint32 action);
	
	///Reverses a name of a key action back to its integer.
	///Returns std::nullopt if the name is not a known key action.
	std::optional<Uint32> getAction(const std::string& name);
	
	///Returns the name of the file for the default configuration
	std::string getDefaultConfigurationFile();
	
	///Returns the name of the file for the configuration
	std::string getConfigurationFile();
	
	extern std::vector<std::string> names;
	extern std::map<std::string, Uint32> keys;
};
