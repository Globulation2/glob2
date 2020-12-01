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

#ifndef KeyPress_h
#define KeyPress_h

#include "SDL2/SDL.h"
#include <string>


///Represents a "key" on the keyboard and how its used
class KeyPress
{
public:
	///Construct a KeyPress
	KeyPress(SDL_Keysym& key, bool pressed);
	
	///Clone a KeyPress except changing the pressed value
	KeyPress(KeyPress key, bool pressed);

	///Construct an empty KeyPress
	KeyPress();

	///Compares two KeyPress
	bool operator<(const KeyPress& rhs) const;

	///Compares two KeyPress
	bool operator!=(const KeyPress& rhs) const;

	///Compares two KeyPress
	bool operator==(const KeyPress& rhs) const;
	
	///Formats a key press
	std::string format() const;
	
	///Interprets a key press from a string
	void interpret(const std::string& s);
	
	///Returns the key
	std::string getKey() const;
	
	///Returns the translated version of the key
	std::string getTranslated() const;
	
	///Returns whether the key is to be pressed in or out
	bool getPressed() const;
	
	///Returns whether the alt must be held with the key
	bool needAlt() const;
	
	///Returns whether control must be held with the key
	bool needControl() const;
	
	///Returns whether the meta must be helt with the key
	bool needMeta() const;
	
	///Returns whether shift must be held with the key
	bool needShift() const;
private:
	std::string key;
	bool pressed;
	bool alt;
	bool control;
	bool meta;
	bool shift;
};


#endif
