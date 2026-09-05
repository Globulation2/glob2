// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "GameGUIKeyActions.h"

namespace GameGUIKeyActions
{
	void init()
	{
		names.resize(ActionSize);
	
		names[DoNothing] = "do nothing";
		keys["do nothing"] = DoNothing;

		names[UpgradeBuilding] = "upgrade building";
		keys["upgrade building"] = UpgradeBuilding;

		names[ShowMainMenu] = "show main menu";
		keys["show main menu"] = ShowMainMenu;

		names[IncreaseUnitsWorking] = "increase units working";
		keys["increase units working"] = IncreaseUnitsWorking;

		names[DecreaseUnitsWorking] = "decrease units working";
		keys["decrease units working"] = DecreaseUnitsWorking;

		names[OpenChatBox] = "open chat box";
		keys["open chat box"] = OpenChatBox;

		names[IterateSelection] = "iterate selection";
		keys["iterate selection"] = IterateSelection;

		names[GoToEvent] = "go to event";
		keys["go to event"] = GoToEvent;

		names[GoToHome] = "go to home";
		keys["go to home"] = GoToHome;

		names[PauseGame] = "pause game";
		keys["pause game"] = PauseGame;
		
		names[HardPause] = "hard pause";
		keys["hard pause"] = HardPause;
		
		names[ToggleDrawUnitPaths] = "toggle draw unit paths";
		keys["toggle draw unit paths"] = ToggleDrawUnitPaths;
		
		names[DestroyBuilding] = "destroy building";
		keys["destroy building"] = DestroyBuilding;
		
		names[RepairBuilding] = "repair building";
		keys["repair building"] = RepairBuilding;
		
		names[ToggleDrawInformation] = "toggle draw information";
		keys["toggle draw information"] = ToggleDrawInformation;
		
		names[ToggleDrawAccessibilityAids] = "toggle draw accessibility aids";
		keys["toggle draw accessibility aids"] = ToggleDrawAccessibilityAids;
		
		names[MarkMap] = "mark map";
		keys["mark map"] = MarkMap;
		
		names[ToggleRecordingVoice] = "toggle recording voice";
		keys["toggle recording voice"] = ToggleRecordingVoice;
		
		names[ViewHistory] = "view history";
		keys["view history"] = ViewHistory;
		
		names[SelectConstructSwarm] = "select construct swarm";
		keys["select construct swarm"] = SelectConstructSwarm;
		
		names[SelectConstructInn] = "select construct inn";
		keys["select construct inn"] = SelectConstructInn;
		
		names[SelectConstructHospital] = "select construct hospital";
		keys["select construct hospital"] = SelectConstructHospital;
		
		names[SelectConstructRacetrack] = "select construct racetrack";
		keys["select construct racetrack"] = SelectConstructRacetrack;
		
		names[SelectConstructSwimmingPool] = "select construct swimmingpool";
		keys["select construct swimmingpool"] = SelectConstructSwimmingPool;
		
		names[SelectConstructBarracks] = "select construct barracks";
		keys["select construct barracks"] = SelectConstructBarracks;
		
		names[SelectConstructSchool] = "select construct school";
		keys["select construct school"] = SelectConstructSchool;
		
		names[SelectConstructDefenceTower] = "select construct defencetower";
		keys["select construct defencetower"] = SelectConstructDefenceTower;
		
		names[SelectConstructStoneWall] = "select construct stonewall";
		keys["select construct stonewall"] = SelectConstructStoneWall;
		
		names[SelectConstructMarket] = "select construct market";
		keys["select construct market"] = SelectConstructMarket;
		
		names[SelectPlaceExplorationFlag] = "select place explorationflag";
		keys["select place explorationflag"] = SelectPlaceExplorationFlag;
		
		names[SelectPlaceWarFlag] = "select place warflag";
		keys["select place warflag"] = SelectPlaceWarFlag;
		
		names[SelectPlaceClearingFlag] = "select place clearingflag";
		keys["select place clearingflag"] = SelectPlaceClearingFlag;
		
		names[SelectPlaceForbiddenArea] = "select place forbidden area";
		keys["select place forbidden area"] = SelectPlaceForbiddenArea;
		
		names[SelectPlaceGuardArea] = "select place guard area";
		keys["select place guard area"] = SelectPlaceGuardArea;
		
		names[SelectPlaceClearingArea] = "select place clearing area";
		keys["select place clearing area"] = SelectPlaceClearingArea;
		
		names[SwitchToAddingAreas] = "switch to adding areas";
		keys["switch to adding areas"] = SwitchToAddingAreas;
		
		names[SwitchToRemovingAreas] = "switch to removing areas";
		keys["switch to removing areas"] = SwitchToRemovingAreas;
		
		names[SwitchToAreaBrush1] = "switch to area brush 1";
		keys["switch to area brush 1"] = SwitchToAreaBrush1;
		
		names[SwitchToAreaBrush2] = "switch to area brush 2";
		keys["switch to area brush 2"] = SwitchToAreaBrush2;
		
		names[SwitchToAreaBrush3] = "switch to area brush 3";
		keys["switch to area brush 3"] = SwitchToAreaBrush3;
		
		names[SwitchToAreaBrush4] = "switch to area brush 4";
		keys["switch to area brush 4"] = SwitchToAreaBrush4;
		
		names[SwitchToAreaBrush5] = "switch to area brush 5";
		keys["switch to area brush 5"] = SwitchToAreaBrush5;
		
		names[SwitchToAreaBrush6] = "switch to area brush 6";
		keys["switch to area brush 6"] = SwitchToAreaBrush6;
		
		names[SwitchToAreaBrush7] = "switch to area brush 7";
		keys["switch to area brush 7"] = SwitchToAreaBrush7;
		
		names[SwitchToAreaBrush8] = "switch to area brush 8";
		keys["switch to area brush 8"] = SwitchToAreaBrush8;

		names[IncreaseGameSpeed] = "increase game speed";
		keys["increase game speed"] = IncreaseGameSpeed;

		names[DecreaseGameSpeed] = "decrease game speed";
		keys["decrease game speed"] = DecreaseGameSpeed;
	}

	const std::string getName(Uint32 action)
	{
		return names[action];
	}

	const Uint32 getAction(const std::string& name)
	{
		return keys[name];
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

std::vector<std::string> GameGUIKeyActions::names;
std::map<std::string, Uint32> GameGUIKeyActions::keys;
