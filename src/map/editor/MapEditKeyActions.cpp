// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "MapEditKeyActions.h"

#include "KeyActionTable.h"

namespace MapEditKeyActions
{
	static KeyActionTable table(ActionSize);

	void init()
	{
		table.add(DoNothing, "do nothing");
		table.add(SwitchToBuildingView, "switch to building view");
		table.add(SwitchToFlagView, "switch to flag view");
		table.add(SwitchToTerrainView, "switch to terrain view");
		table.add(SwitchToTeamsView, "switch to teams view");
		table.add(OpenSaveScreen, "open save menu");
		table.add(OpenLoadScreen, "open load menu");
		table.add(SelectSwarm, "select swarm building");
		table.add(SelectInn, "select inn building");
		table.add(SelectHospital, "select hospital building");
		table.add(SelectRacetrack, "select racetrack building");
		table.add(SelectSwimmingpool, "select swimmingpool building");
		table.add(SelectSchool, "select school building");
		table.add(SelectBarracks, "select barracks building");
		table.add(SelectTower, "select tower building");
		table.add(SelectStonewall, "select wall building");
		table.add(SelectMarket, "select market building");
		table.add(SelectExplorationFlag, "select explorationflag");
		table.add(SelectWarFlag, "select warflag");
		table.add(SelectClearingFlag, "select clearingflag");
		table.add(ToggleMenuScreen, "toggle menu screen");
		table.add(SelectDeleteTool, "select delete tool");
	}

	const std::string getName(Uint32 action)
	{
		return table.getName(action);
	}

	std::optional<Uint32> getAction(const std::string& name)
	{
		return table.getAction(name);
	}
	
	std::string getDefaultConfigurationFile()
	{
		return "data/keyboard-mapedit.default.txt";
	}
	
	std::string getConfigurationFile()
	{
		return "keyboard-mapedit.txt";
	}
}
