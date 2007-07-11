/*
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

#ifndef __MAPEDIT_KEY_ACTIONS_H
#define __MAPEDIT_KEY_ACTIONS_H

#include "SDL.h"
#include <string>
#include <map>
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
		ActionSize,
	};

	///Gets the name of a key-action from the integer
	const std::string getName(Uint32 action);
	
	///Reverses a name of a key action back to its integer
	const Uint32 getAction(const std::string& name);
	
	///Returns the name of the file for the default configuration
	std::string getDefaultConfigurationFile(const std::string& file);
	
	extern std::vector<std::string> names;
	extern std::map<std::string, Uint32> keys;
};

#endif
