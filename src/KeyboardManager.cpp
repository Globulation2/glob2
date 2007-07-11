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

#include <boost/lexical_cast.hpp>
#include "FileManager.h"
#include "FormatableString.h"
#include "GameGUIKeyActions.h"
#include <iostream>
#include "KeyboardManager.h"
#include "MapEditKeyActions.h"
#include "Stream.h"
#include "Toolkit.h"

using namespace GAGCore;

std::map<std::string, SDLKey> KeyPress::keyMap;
bool KeyPress::keyMapInitialized;


KeyPress::KeyPress(SDLKey key, bool pressed)
	: key(key), pressed(pressed)
{

}


KeyPress::KeyPress()
{
	key=SDLK_UNKNOWN;
	pressed=true;
}


bool KeyPress::operator<(const KeyPress& rhs) const
{
	if(key == rhs.key)
		return pressed < rhs.pressed;
	return key < rhs.key;
}



bool KeyPress::operator!=(const KeyPress& rhs) const
{
	if(key != rhs.key)
		return true;
	if(pressed != rhs.pressed)
		return true;
	return false;
}


	
std::string KeyPress::format() const
{
	if(!keyMapInitialized)
		initKeyMap();
	std::string s;
	if(!pressed)
		s+="<unpress>";
	s+=FormatableString("<%0>").arg(SDL_GetKeyName(key));
	return s;
}



void KeyPress::interpret(const std::string& s)
{
	if(!keyMapInitialized)
		initKeyMap();
	std::string ks = s;
	size_t pos =ks.find("<unpress>");
	if(pos!=std::string::npos)
	{
		ks = ks.substr(pos+9);
		pressed=false;
	}
	else
	{
		pressed=true;
	}
	key = keyMap[ks];
}



SDLKey KeyPress::getKey() const
{
	return key;
}



bool KeyPress::getPressed() const
{
	return pressed;
}



void KeyPress::initKeyMap()
{
	///This is because SDL provides no native function to do the reverse of SDL_GetKeyName
	for(Uint32 i = Uint32(SDLK_FIRST); i!=Uint32(SDLK_LAST); ++i)
	{
		keyMap["<"+std::string(SDL_GetKeyName(SDLKey(i)))+">"] = SDLKey(i);
	}
}



KeyboardManager::KeyboardManager(ShortcutMode mode)
	: mode(mode)
{
	lastPressedComboKey = KeyPress();
	if(mode == GameGUIShortcuts)
		if(!loadKeyboardLayout(GameGUIKeyActions::getConfigurationFile()))
			loadKeyboardLayout(GameGUIKeyActions::getDefaultConfigurationFile());
	else if(mode == MapEditShortcuts)
		if(!loadKeyboardLayout(MapEditKeyActions::getConfigurationFile()))
			loadKeyboardLayout(MapEditKeyActions::getDefaultConfigurationFile());
}


Uint32 KeyboardManager::getAction(const KeyPress& key)
{
	if(lastPressedComboKey != KeyPress())
	{
		///When doing combo keys, say <b>-<b>, ignore the <unpressed><b> key that will occur between the two <b> presses
		if(key.getKey() == lastPressedComboKey.getKey() && key.getPressed() == false && lastPressedComboKey.getPressed() == true)
			return GameGUIKeyActions::DoNothing;
		KeyPress comboKey = lastPressedComboKey;
		lastPressedComboKey = KeyPress();
		if(comboKeys[comboKey].find(key) != comboKeys[comboKey].end())
			return comboKeys[comboKey][key];
		else
			return GameGUIKeyActions::DoNothing;
	}
	else if(singleKeys.find(key) != singleKeys.end())
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



void KeyboardManager::setToDefaults(ShortcutMode mode)
{
	if(mode == GameGUIShortcuts)
	{
		singleKeys[KeyPress(SDLK_ESCAPE, true)] = GameGUIKeyActions::ShowMainMenu;
		singleKeys[KeyPress(SDLK_PLUS, true)] = GameGUIKeyActions::IncreaseUnitsWorking;
		singleKeys[KeyPress(SDLK_KP_PLUS, true)] = GameGUIKeyActions::IncreaseUnitsWorking;
		singleKeys[KeyPress(SDLK_EQUALS, true)] = GameGUIKeyActions::IncreaseUnitsWorking;
		singleKeys[KeyPress(SDLK_MINUS, true)] = GameGUIKeyActions::DecreaseUnitsWorking;
		singleKeys[KeyPress(SDLK_KP_MINUS, true)] = GameGUIKeyActions::DecreaseUnitsWorking;
		singleKeys[KeyPress(SDLK_RETURN, true)] = GameGUIKeyActions::OpenChatBox;
		singleKeys[KeyPress(SDLK_TAB, true)] = GameGUIKeyActions::IterateSelection;
		singleKeys[KeyPress(SDLK_SPACE, true)] = GameGUIKeyActions::GoToEvent;
		singleKeys[KeyPress(SDLK_HOME, true)] = GameGUIKeyActions::GoToHome;
		singleKeys[KeyPress(SDLK_PAUSE, true)] = GameGUIKeyActions::PauseGame;
		singleKeys[KeyPress(SDLK_SCROLLOCK, true)] = GameGUIKeyActions::HardPause;
		
		singleKeys[KeyPress(SDLK_t, true)] = GameGUIKeyActions::ToggleDrawUnitPaths;
		singleKeys[KeyPress(SDLK_d, true)] = GameGUIKeyActions::DestroyBuilding;
		singleKeys[KeyPress(SDLK_r, true)] = GameGUIKeyActions::RepairBuilding;
		singleKeys[KeyPress(SDLK_i, true)] = GameGUIKeyActions::ToggleDrawInformation;
		singleKeys[KeyPress(SDLK_h, true)] = GameGUIKeyActions::ToggleDrawAccessibilityAids;
		singleKeys[KeyPress(SDLK_m, true)] = GameGUIKeyActions::MarkMap;
		singleKeys[KeyPress(SDLK_v, true)] = GameGUIKeyActions::ToggleRecordingVoice;
		singleKeys[KeyPress(SDLK_s, true)] = GameGUIKeyActions::ViewHistory;
		singleKeys[KeyPress(SDLK_u, true)] = GameGUIKeyActions::UpgradeBuilding;
		
		comboKeys[KeyPress(SDLK_b, true)][KeyPress(SDLK_i, true)] = GameGUIKeyActions::SelectConstructInn;
		comboKeys[KeyPress(SDLK_b, true)][KeyPress(SDLK_a, true)] = GameGUIKeyActions::SelectConstructSwarm;
		comboKeys[KeyPress(SDLK_b, true)][KeyPress(SDLK_h, true)] = GameGUIKeyActions::SelectConstructHospital;
		comboKeys[KeyPress(SDLK_b, true)][KeyPress(SDLK_r, true)] = GameGUIKeyActions::SelectConstructRacetrack;
		comboKeys[KeyPress(SDLK_b, true)][KeyPress(SDLK_p, true)] = GameGUIKeyActions::SelectConstructSwimmingPool;
		comboKeys[KeyPress(SDLK_b, true)][KeyPress(SDLK_b, true)] = GameGUIKeyActions::SelectConstructBarracks;
		comboKeys[KeyPress(SDLK_b, true)][KeyPress(SDLK_s, true)] = GameGUIKeyActions::SelectConstructSchool;
		comboKeys[KeyPress(SDLK_b, true)][KeyPress(SDLK_d, true)] = GameGUIKeyActions::SelectConstructDefenceTower;
		comboKeys[KeyPress(SDLK_b, true)][KeyPress(SDLK_w, true)] = GameGUIKeyActions::SelectConstructStoneWall;
		comboKeys[KeyPress(SDLK_b, true)][KeyPress(SDLK_m, true)] = GameGUIKeyActions::SelectConstructMarket;
		
		comboKeys[KeyPress(SDLK_f, true)][KeyPress(SDLK_e, true)] = GameGUIKeyActions::SelectPlaceExplorationFlag;
		comboKeys[KeyPress(SDLK_f, true)][KeyPress(SDLK_w, true)] = GameGUIKeyActions::SelectPlaceWarFlag;
		comboKeys[KeyPress(SDLK_f, true)][KeyPress(SDLK_c, true)] = GameGUIKeyActions::SelectPlaceClearingFlag;
		
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_f, true)] = GameGUIKeyActions::SelectPlaceForbiddenArea;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_g, true)] = GameGUIKeyActions::SelectPlaceGuardArea;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_c, true)] = GameGUIKeyActions::SelectPlaceClearingArea;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_a, true)] = GameGUIKeyActions::SwitchToAddingAreas;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_d, true)] = GameGUIKeyActions::SwitchToRemovingAreas;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_1, true)] = GameGUIKeyActions::SwitchToAreaBrush1;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_2, true)] = GameGUIKeyActions::SwitchToAreaBrush2;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_3, true)] = GameGUIKeyActions::SwitchToAreaBrush3;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_4, true)] = GameGUIKeyActions::SwitchToAreaBrush4;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_5, true)] = GameGUIKeyActions::SwitchToAreaBrush5;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_6, true)] = GameGUIKeyActions::SwitchToAreaBrush6;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_7, true)] = GameGUIKeyActions::SwitchToAreaBrush7;
		comboKeys[KeyPress(SDLK_a, true)][KeyPress(SDLK_8, true)] = GameGUIKeyActions::SwitchToAreaBrush8;
	}
	else if(mode == MapEditShortcuts)
	{
		singleKeys[KeyPress(SDLK_ESCAPE, true)] = MapEditKeyActions::ToggleMenuScreen;
	}
}


void KeyboardManager::saveKeyboardLayout()
{
	std::string file;
	if(mode == GameGUIShortcuts)
		file = GameGUIKeyActions::getConfigurationFile();
	else if(mode == MapEditShortcuts)
		file = MapEditKeyActions::getConfigurationFile();

	OutputLineStream *stream = new OutputLineStream(Toolkit::getFileManager()->openOutputStreamBackend(file));
	for(std::map<KeyPress, Uint32>::iterator i = singleKeys.begin(); i!=singleKeys.end(); ++i)
	{
		if(mode == GameGUIShortcuts)
			stream->writeLine(FormatableString("%0=%1").arg(i->first.format()).arg(GameGUIKeyActions::getName(i->second)));
		else if(mode == MapEditShortcuts)
			stream->writeLine(FormatableString("%0=%1").arg(i->first.format()).arg(MapEditKeyActions::getName(i->second)));

	}
	for(std::map<KeyPress, std::map<KeyPress, Uint32> >::iterator i = comboKeys.begin(); i!=comboKeys.end(); ++i)
	{
		for(std::map<KeyPress, Uint32>::iterator j = i->second.begin(); j!=i->second.end(); ++j)
		{
			if(mode == GameGUIShortcuts)
				stream->writeLine(FormatableString("%0-%1=%2").arg(i->first.format()).arg(j->first.format()).arg(GameGUIKeyActions::getName(j->second)));
			else if(mode == MapEditShortcuts)
				stream->writeLine(FormatableString("%0-%1=%2").arg(i->first.format()).arg(j->first.format()).arg(MapEditKeyActions::getName(j->second)));
		}
	}
	delete stream;
}


bool KeyboardManager::loadKeyboardLayout(const std::string& file)
{
	InputLineStream *stream = new InputLineStream(Toolkit::getFileManager()->openInputStreamBackend(file));
	if(stream->isEndOfStream())
	{
		delete stream;
		return false;
	}
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
			KeyPress f_key;
			f_key.interpret(first_key);
			KeyPress s_key;
			s_key.interpret(second_key);
			if(mode == GameGUIShortcuts)
				comboKeys[f_key][s_key] = GameGUIKeyActions::getAction(action);
			else if(mode == MapEditShortcuts)
				comboKeys[f_key][s_key] = MapEditKeyActions::getAction(action);
		}
		else
		{
			std::string key(line, 0, equal);
			std::string action(line, equal+1, std::string::npos);
			KeyPress f_key;
			f_key.interpret(key);
			if(mode == GameGUIShortcuts)
				singleKeys[f_key] = GameGUIKeyActions::getAction(action);
			else if(mode == MapEditShortcuts)
				singleKeys[f_key] = MapEditKeyActions::getAction(action);
		}
	}
	delete stream;
	return true;
}

