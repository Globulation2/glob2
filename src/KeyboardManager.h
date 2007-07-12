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

#ifndef __KEYBOARD_MANAGER_H
#define __KEYBOARD_MANAGER_H

#include "SDL.h"
#include <map>

//Steps to add a keyboard shortcut:
//1) Identify where it goes (either GameGUIKeyboardActions or MapEditorKeyboardActions)
//2) Add the action to the enum there, and give it an approppriette name in the init function
//3) Find the handleKey function in either GameGUI or MapEdit and add the code there

///Represents a "key" on the keyboard and how its used
class KeyPress
{
public:
	///Construct a KeyPress
	KeyPress(SDLKey key, bool pressed);

	///Construct an empty KeyPress
	KeyPress();

	///Compares two KeyPress
	bool operator<(const KeyPress& rhs) const;

	///Compares two KeyPress
	bool operator!=(const KeyPress& rhs) const;
	
	///Formats a key press
	std::string format() const;
	
	///Interprets a key press from a string
	void interpret(const std::string& s);
	
	///Returns the key
	SDLKey getKey() const;
	
	///Returns whether the key is to be pressed in or our
	bool getPressed() const;
private:
	static void initKeyMap();
	static bool keyMapInitialized;
	static std::map<std::string, SDLKey> keyMap;
	SDLKey key;
	bool pressed;
};


///This class is meant to do keyboard management, handling keyboard shortcuts, layouts and such for GameGUI
class KeyboardManager
{
public:
	enum ShortcutMode
	{
		GameGUIShortcuts,
		MapEditShortcuts,
	};

	///Constructs a keyboard manager, either to use the MapEdit shortcuts or the GameGUI shortcuts
	KeyboardManager(ShortcutMode mode);
	
	///Returns the integer action accossiatted with the provided key.
	Uint32 getAction(const KeyPress& key);

	///Sets the defaults for a keyboard layout
	void setToDefaults(ShortcutMode mode);
	
	///Saves the keyboard layout
	void saveKeyboardLayout() const;
	
	///Loads the keyboard layout, returns false in unsuccessfull
	bool loadKeyboardLayout(const std::string& file);
	
	///Returns the map for single-key shortcuts
	const std::map<KeyPress, Uint32>& getSingleKeyShortcuts() const;
	
	///Returns a name for a particular single-key shortcut
	std::string getSingleKeyShortcutName(std::map<KeyPress, Uint32>::const_iterator shortcut) const;
private:
	std::map<KeyPress, Uint32> singleKeys;
	std::map<KeyPress, std::map<KeyPress, Uint32> > comboKeys;
	KeyPress lastPressedComboKey;
	ShortcutMode mode;
};

#endif
