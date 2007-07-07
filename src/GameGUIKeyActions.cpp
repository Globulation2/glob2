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

#include "GameGUIKeyActions.h"

namespace GameGUIKeyActions
{
	void init()
	{
		names[DoNothing] = "";
		keys[""] = DoNothing;
		

		names[UpgradeBuilding] = "upgrade building";
		keys["upgrade building"] = UpgradeBuilding;

		names[ShowMainMenu] = "show main menu";
		keys["show main menu"] = ShowMainMenu;		
	}

	const std::string getName(Uint32 action)
	{
		return names[action];
	}

	const Uint32 getAction(const std::string& name)
	{
		return keys[name];
	}
	
}

std::vector<std::string> GameGUIKeyActions::names;
std::map<std::string, Uint32> GameGUIKeyActions::keys;
