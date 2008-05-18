/*
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

#include <boost/lexical_cast.hpp>
#include "FileManager.h"
#include "FormatableString.h"
#include "GameGUIKeyActions.h"
#include <iostream>
#include "KeyboardManager.h"
#include "MapEditKeyActions.h"
#include "Stream.h"
#include "Toolkit.h"
#include "StringTable.h"

using namespace GAGCore;

KeyboardShortcut::KeyboardShortcut()
{
	action = 0;
}



void KeyboardShortcut::addKeyPress(const KeyPress& key)
{
	keys.push_back(key);
}



std::string KeyboardShortcut::format(ShortcutMode mode) const
{
	std::string s;
	for(std::vector<KeyPress>::const_iterator i = keys.begin(); i!=keys.end(); ++i)
	{
		s += i->format();
		if(i!=(keys.end()-1))
			s+="-";
	}
	
	s+= "=";

	if(mode == GameGUIShortcuts)
		s+=GameGUIKeyActions::getName(action);
	else if(mode == MapEditShortcuts)
		s+=MapEditKeyActions::getName(action);
	return s;
}



void KeyboardShortcut::interpret(const std::string& as, ShortcutMode mode)
{
	std::string s = as;
	std::string left = std::string(s, 0, s.find(">=")+1);
	std::string right = std::string(s, s.find(">=")+2, std::string::npos);
	while(left.find(">-") != std::string::npos)
	{
		size_t end = left.find(">-");
		KeyPress kp;
		kp.interpret(left.substr(0, end+1));
		keys.push_back(kp);
		left=left.substr(end+2, std::string::npos);
	}
	
	//Add the key that isn't seperated by a -
	KeyPress kp;
	kp.interpret(left);
	keys.push_back(kp);
	
	if(mode == GameGUIShortcuts)
		action = GameGUIKeyActions::getAction(right);
	if(mode == MapEditShortcuts)
		action = MapEditKeyActions::getAction(right);
}



std::string KeyboardShortcut::formatTranslated(ShortcutMode mode) const
{
	std::string s;
	for(std::vector<KeyPress>::const_iterator i = keys.begin(); i!=keys.end(); ++i)
	{
		s += "<" + i->getTranslated() + ">";
		if(i!=(keys.end()-1))
			s+="-";
	}
	s+= "=";
	
	std::string key_s = "[";
	if(mode == GameGUIShortcuts)
		key_s += GameGUIKeyActions::getName(action);
	else if(mode == MapEditShortcuts)
		key_s += MapEditKeyActions::getName(action);
	key_s += "]";
	
	s+=Toolkit::getStringTable()->getString(key_s.c_str());
	return s;
}



size_t KeyboardShortcut::getKeyPressCount() const
{
	return keys.size();
}


	
KeyPress KeyboardShortcut::getKeyPress(size_t n) const
{
	return keys[n];
}


	
void KeyboardShortcut::setAction(Uint32 naction)
{
	action = naction;
}


	
Uint32 KeyboardShortcut::getAction() const
{
	return action;
}



bool KeyboardShortcut::isShortcutValid() const
{
	for(std::vector<KeyPress>::const_iterator i = keys.begin(); i!=keys.end(); ++i)
	{
		if(i->getKey() == "no key")
			return false;
	}
	return true;
}


KeyboardManager::KeyboardManager(ShortcutMode mode)
	: mode(mode)
{
	if(mode == GameGUIShortcuts)
	{
		if(!loadKeyboardLayout(GameGUIKeyActions::getConfigurationFile()))
		{
			loadKeyboardLayout(GameGUIKeyActions::getDefaultConfigurationFile());
		}
	}
	else if(mode == MapEditShortcuts)
	{
		if(!loadKeyboardLayout(MapEditKeyActions::getConfigurationFile()))
		{
			loadKeyboardLayout(MapEditKeyActions::getDefaultConfigurationFile());
		}
	}
}



Uint32 KeyboardManager::getAction(const KeyPress& key)
{
	//This is to solve a bug due to the system recieving the key-up event
	//which is not included in multiple-key sequences
	if(key.getPressed() || lastPresses.empty())
		lastPresses.push_back(key);
	int matches = 0;
	KeyboardShortcut lastMatch;
	for(std::list<KeyboardShortcut>::iterator i = shortcuts.begin(); i!=shortcuts.end(); ++i)
	{
		if(lastPresses.size() > i->getKeyPressCount())
			continue;
		
		bool matched=true;
		for(size_t j=0; j<lastPresses.size(); ++j)
		{
			if(i->getKeyPress(j) != lastPresses[j])
			{
				matched=false;
				break;
			}
		}
		if(matched == true)
		{
			lastMatch = *i;
			matches += 1;
		}
	}
	if(matches == 0)
	{
		lastPresses.clear();
		return GameGUIKeyActions::DoNothing;
	}
	else if(matches == 1)
	{
		lastPresses.clear();
		return lastMatch.getAction();
	}
	return GameGUIKeyActions::DoNothing;
}



void KeyboardManager::saveKeyboardLayout() const
{
	std::string file;
	if(mode == GameGUIShortcuts)
		file = GameGUIKeyActions::getConfigurationFile();
	else if(mode == MapEditShortcuts)
		file = MapEditKeyActions::getConfigurationFile();


	OutputLineStream *stream = new OutputLineStream(Toolkit::getFileManager()->openOutputStreamBackend(file));
	for(std::list<KeyboardShortcut>::const_iterator i = shortcuts.begin(); i!=shortcuts.end(); ++i)
	{
		if(i->isShortcutValid())
			stream->writeLine(i->format(mode));
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
		if(line == "")
			continue;
		KeyboardShortcut ks;
		ks.interpret(line, mode);
		shortcuts.push_back(ks);
	}

	delete stream;
	return true;
}



void KeyboardManager::loadDefaultShortcuts()
{
	shortcuts.clear();
	lastPresses.clear();
	if(mode == GameGUIShortcuts)
		loadKeyboardLayout(GameGUIKeyActions::getDefaultConfigurationFile());
	else if(mode == MapEditShortcuts)
		loadKeyboardLayout(MapEditKeyActions::getDefaultConfigurationFile());
}



const std::list<KeyboardShortcut>& KeyboardManager::getKeyboardShortcuts() const
{
	return shortcuts;
}



std::list<KeyboardShortcut>& KeyboardManager::getKeyboardShortcuts()
{
	return shortcuts;
}
