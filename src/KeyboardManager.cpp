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

#include "KeyboardManager.h"
#include "Stream.h"
#include "FormatableString.h"
#include "GameGUIKeyActions.h"
#include <boost/lexical_cast.hpp>
#include "Toolkit.h"
#include "FileManager.h"


using namespace GAGCore;

KeyboardManager::KeyboardManager()
{
	lastPressedComboKey = SDLKey(0);
	setToDefaults();
}


Uint32 KeyboardManager::getAction(SDLKey key)
{
	if(lastPressedComboKey != SDLKey(0))
	{
		SDLKey comboKey = lastPressedComboKey;
		lastPressedComboKey = SDLKey(0);
		if(comboKeys[comboKey].find(key) != comboKeys[comboKey].end())
			return comboKeys[comboKey][key];
		else
			return GameGUIKeyActions::DoNothing;
	}
	if(singleKeys.find(key) != singleKeys.end())
	{
		return singleKeys[key];
	}
	else if(comboKeys.find(key) != comboKeys.end())
	{
		lastPressedComboKey = key;
		return GameGUIKeyActions::DoNothing;
	}
	return GameGUIKeyActions::DoNothing;
}



void KeyboardManager::setToDefaults()
{
	singleKeys[SDLK_u] = GameGUIKeyActions::UpgradeBuilding;
	singleKeys[SDLK_ESCAPE] = GameGUIKeyActions::ShowMainMenu;
}



void KeyboardManager::saveKeyboardLayout()
{
	OutputLineStream *stream = new OutputLineStream(Toolkit::getFileManager()->openOutputStreamBackend("keyboard_layout.txt"));
	for(std::map<SDLKey, Uint32>::iterator i = singleKeys.begin(); i!=singleKeys.end(); ++i)
	{
		stream->writeLine(FormatableString("%0=%1").arg(SDL_GetKeyName(i->first)).arg(GameGUIKeyActions::getName(i->second)));
	}
	for(std::map<SDLKey, std::map<SDLKey, Uint32> >::iterator i = comboKeys.begin(); i!=comboKeys.end(); ++i)
	{
		for(std::map<SDLKey, Uint32>::iterator j = i->second.begin(); j!=i->second.end(); ++j)
		{
			stream->writeLine(FormatableString("%0-%1=%2").arg(SDL_GetKeyName(i->first)).arg(SDL_GetKeyName(j->first)).arg(GameGUIKeyActions::getName(j->second)));
		}
	}
	delete stream;
}



void KeyboardManager::loadKeyboardLayout()
{
	///This is because SDL provides no native function to go reverse from a keys name to its integer
	std::map<std::string, SDLKey> keyMap;
	for(Uint32 i = Uint32(SDLK_FIRST); i!=Uint32(SDLK_LAST); ++i)
	{
		keyMap[SDL_GetKeyName(SDLKey(i))] = SDLKey(i);
	}

	InputLineStream *stream = new InputLineStream(Toolkit::getFileManager()->openInputStreamBackend("keyboard_layout.txt"));
	while(!stream->isEndOfStream())
	{
		std::string line = stream->readLine();
		size_t dash = line.find('-');
		size_t equal = line.find('=');
		if(dash != std::string::npos)
		{
			std::string first_key(line, 0, dash);
			std::string second_key(line, dash+1, equal - dash - 1);
			std::string action(line, equal+1, std::string::npos);
			comboKeys[keyMap[first_key]][keyMap[second_key]] = GameGUIKeyActions::getAction(action);
		}
		else
		{
			std::string key(line, 0, equal);
			std::string action(line, equal+1, std::string::npos);
			singleKeys[keyMap[key]] = GameGUIKeyActions::getAction(action);
		}
	}
	delete stream;
}

