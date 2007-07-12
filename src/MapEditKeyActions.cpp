/*key
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "MapEditKeyActions.h"

namespace MapEditKeyActions
{
	void init()
	{
		names.resize(ActionSize);
	
		names[DoNothing] = "";
		keys[""] = DoNothing;
	
		names[SwitchToBuildingView] = "switch to building view";
		keys["switch to building view"] = SwitchToBuildingView;
	
		names[SwitchToFlagView] = "switch to flag view";
		keys["switch to flag view"] = SwitchToFlagView;
	
		names[SwitchToTerrainView] = "switch to terrain view";
		keys["switch to terrain view"] = SwitchToTerrainView;
	
		names[SwitchToTeamsView] = "switch to teams view";
		keys["switch to teams view"] = SwitchToTeamsView;
	
		names[OpenSaveScreen] = "open save menu";
		keys["open save menu"] = OpenSaveScreen;
	
		names[OpenLoadScreen] = "open load menu";
		keys["open load menu"] = OpenLoadScreen;
	
		names[SelectSwarm] = "select swarm building";
		keys["select swarm building"] = SelectSwarm;
	
		names[SelectInn] = "select inn building";
		keys["select inn building"] = SelectInn;
	
		names[SelectHospital] = "select hospital building";
		keys["select hospital building"] = SelectHospital;
	
		names[SelectRacetrack] = "select racetrack building";
		keys["select racetrack building"] = SelectRacetrack;
	
		names[SelectSwimmingpool] = "select swimmingpool building";
		keys["select swimmingpool building"] = SelectSwimmingpool;
	
		names[SelectSchool] = "select school building";
		keys["select school building"] = SelectSchool;
	
		names[SelectBarracks] = "select barracks building";
		keys["select barracks building"] = SelectBarracks;
	
		names[SelectTower] = "select tower building";
		keys["select tower building"] = SelectTower;
	
		names[SelectStonewall] = "select wall building";
		keys["select wall building"] = SelectStonewall;
	
		names[SelectMarket] = "select market building";
		keys["select market building"] = SelectMarket;
	
		names[SelectExplorationFlag] = "select explorationflag";
		keys["select explorationflag"] = SelectExplorationFlag;
	
		names[SelectWarFlag] = "select warflag";
		keys["select warflag"] = SelectWarFlag;
	
		names[SelectClearingFlag] = "select clearingflag";
		keys["select clearingflag"] = SelectClearingFlag;
	
		names[ToggleMenuScreen] = "toggle menu screen";
		keys["toggle menu screen"] = ToggleMenuScreen;
		
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
		return "data/keyboard-mapedit.default.txt";
	}
	
	std::string getConfigurationFile()
	{
		return "keyboard-mapedit.txt";
	}
}

std::vector<std::string> MapEditKeyActions::names;
std::map<std::string, Uint32> MapEditKeyActions::keys;
