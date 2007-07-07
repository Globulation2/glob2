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


///This class is meant to do keyboard management, handling keyboard shortcuts, layouts and such for GameGUI
class KeyboardManager
{
public:
	///Constructs a keyboard manager
	KeyboardManager();
	
	///Returns the integer action accossiatted with the provided key. Often it may be
	///nothing, or it may wait for another key to be pressed, etc.
	Uint32 getAction(SDLKey key);

	///Sets the defaults for a keyboard layout
	void setToDefaults();
	
	///Saves the keyboard layout
	void saveKeyboardLayout();
	
	///Loads the keyboard layout
	void loadKeyboardLayout();
private:
	std::map<SDLKey, Uint32> singleKeys;
	std::map<SDLKey, std::map<SDLKey, Uint32> > comboKeys;
	SDLKey lastPressedComboKey;
};

#endif
