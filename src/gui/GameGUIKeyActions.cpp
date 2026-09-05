// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "GameGUIKeyActions.h"

#include "KeyActionTable.h"

namespace GameGUIKeyActions
{
	static KeyActionTable table(ActionSize);

	void init()
	{
		table.add(DoNothing, "do nothing");
		table.add(UpgradeBuilding, "upgrade building");
		table.add(ShowMainMenu, "show main menu");
		table.add(IncreaseUnitsWorking, "increase units working");
		table.add(DecreaseUnitsWorking, "decrease units working");
		table.add(OpenChatBox, "open chat box");
		table.add(IterateSelection, "iterate selection");
		table.add(GoToEvent, "go to event");
		table.add(GoToHome, "go to home");
		table.add(PauseGame, "pause game");
		table.add(HardPause, "hard pause");
		table.add(ToggleDrawUnitPaths, "toggle draw unit paths");
		table.add(DestroyBuilding, "destroy building");
		table.add(RepairBuilding, "repair building");
		table.add(ToggleDrawInformation, "toggle draw information");
		table.add(ToggleDrawAccessibilityAids, "toggle draw accessibility aids");
		table.add(MarkMap, "mark map");
		table.add(ToggleRecordingVoice, "toggle recording voice");
		table.add(ViewHistory, "view history");
		table.add(SelectConstructSwarm, "select construct swarm");
		table.add(SelectConstructInn, "select construct inn");
		table.add(SelectConstructHospital, "select construct hospital");
		table.add(SelectConstructRacetrack, "select construct racetrack");
		table.add(SelectConstructSwimmingPool, "select construct swimmingpool");
		table.add(SelectConstructBarracks, "select construct barracks");
		table.add(SelectConstructSchool, "select construct school");
		table.add(SelectConstructDefenceTower, "select construct defencetower");
		table.add(SelectConstructStoneWall, "select construct stonewall");
		table.add(SelectConstructMarket, "select construct market");
		table.add(SelectPlaceExplorationFlag, "select place explorationflag");
		table.add(SelectPlaceWarFlag, "select place warflag");
		table.add(SelectPlaceClearingFlag, "select place clearingflag");
		table.add(SelectPlaceForbiddenArea, "select place forbidden area");
		table.add(SelectPlaceGuardArea, "select place guard area");
		table.add(SelectPlaceClearingArea, "select place clearing area");
		table.add(SwitchToAddingAreas, "switch to adding areas");
		table.add(SwitchToRemovingAreas, "switch to removing areas");
		table.add(SwitchToAreaBrush1, "switch to area brush 1");
		table.add(SwitchToAreaBrush2, "switch to area brush 2");
		table.add(SwitchToAreaBrush3, "switch to area brush 3");
		table.add(SwitchToAreaBrush4, "switch to area brush 4");
		table.add(SwitchToAreaBrush5, "switch to area brush 5");
		table.add(SwitchToAreaBrush6, "switch to area brush 6");
		table.add(SwitchToAreaBrush7, "switch to area brush 7");
		table.add(SwitchToAreaBrush8, "switch to area brush 8");
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
		return "data/keyboard-gui.default.txt";
	}
	
	std::string getConfigurationFile()
	{
		return "keyboard-gui.txt";
	}
}
