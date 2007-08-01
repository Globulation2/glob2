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
#include <list>
#include <vector>
#include <string>
#include "KeyPress.h"

enum ShortcutMode
{
	GameGUIShortcuts,
	MapEditShortcuts,
};

//Steps to add a keyboard shortcut:
//1) Identify where it goes (either GameGUIKeyboardActions or MapEditorKeyboardActions)
//2) Add the action to the enum there, and give it an approppriette name in the init function
//3) Find the handleKey function in either GameGUI or MapEdit and add the code for the action
//4) Add the name you provided to the translation files, as [name], and at the bare minimum, give it an English translation

///This class represents a keyboard shortcut
class KeyboardShortcut
{
public:
	///Constructs a KeyboardShortcut
	KeyboardShortcut();

	///Adds a key press
	void addKeyPress(const KeyPress& key);

	///Formats the shortcut
	std::string format(ShortcutMode mode) const;

	///Interprets a keyboard shortcut from a string
	void interpret(const std::string& s, ShortcutMode mode);

	///Formats a translated version, not for serializtaion
	std::string formatTranslated(ShortcutMode mode) const;
	
	///Counts how many key presses there is
	size_t getKeyPressCount() const;
	
	///Returns the n'th key press
	KeyPress getKeyPress(size_t n) const;
	
	///Sets the action accossiatted with this shortcut
	void setAction(Uint32 action);
	
	///Returns the action accossiatted with this shortcut
	Uint32 getAction() const;
	
	///Returns whether this shortcut is valid. Shortcuts are invalid if any of the
	///KeyPresses where not changed from "no key"
	bool isShortcutValid() const;
private:
	Uint32 action;
	std::vector<KeyPress> keys;
};


///This class is meant to do keyboard management, handling keyboard shortcuts, layouts and such for GameGUI
class KeyboardManager
{
public:
	///Constructs a keyboard manager, either to use the MapEdit shortcuts or the GameGUI shortcuts
	KeyboardManager(ShortcutMode mode);
	
	///Returns the integer action accossiatted with the provided key.
	Uint32 getAction(const KeyPress& key);
	
	///Saves the keyboard layout
	void saveKeyboardLayout() const;
	
	///Loads the keyboard layout, returns false in unsuccessfull
	bool loadKeyboardLayout(const std::string& file);

	///Clears all current shortcuts and loads the defaults
	void loadDefaultShortcuts();

	///Returns the list of keyboard shortcuts
	const std::list<KeyboardShortcut>& getKeyboardShortcuts() const;

	///Returns the list of keyboard shortcuts
	std::list<KeyboardShortcut>& getKeyboardShortcuts();

private:
	std::list<KeyboardShortcut> shortcuts;
	std::vector<KeyPress> lastPresses;
	ShortcutMode mode;
};

#endif
