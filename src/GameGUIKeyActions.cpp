/*key
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

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
		
		//TODO 2009-12-06 giszmo Markets has been disabled as a bug was blocking
		//the release of beta5 and with that also the replay feature for 3 months
		//now.
//		names[SelectConstructMarket] = "select construct market";
//		keys["select construct market"] = SelectConstructMarket;
		
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
