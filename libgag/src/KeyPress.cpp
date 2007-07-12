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

#include "KeyPress.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "GUIBase.h"
#include "FormatableString.h"

using namespace GAGCore;
using namespace GAGGUI;

KeyPress::KeyPress(SDL_keysym nkey, bool pressed)
	: pressed(pressed)
{
	std::string key_s = std::string("[") + SDL_GetKeyName(nkey.sym) + std::string("]");
	Uint16 c=nkey.unicode;
	
	if(Toolkit::getStringTable()->doesStringExist(key_s.c_str()))
	{
		key = SDL_GetKeyName(nkey.sym);
	}
	else if (c)
	{
		char utf8text[4];
		UCS16toUTF8(c, utf8text);
		size_t lutf8=strlen(utf8text);
		key = std::string(utf8text, lutf8);
	}
	else
	{
		key = SDL_GetKeyName(nkey.sym);
	}
}


KeyPress::KeyPress()
{
	key="escape";
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



bool KeyPress::operator==(const KeyPress& rhs) const
{
	return key == rhs.key && pressed == rhs.pressed;
}


	
std::string KeyPress::format() const
{
	std::string s;
	if(!pressed)
		s+="<unpress>";
	s+=FormatableString("<%0>").arg(key);
	return s;
}



void KeyPress::interpret(const std::string& s)
{
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
	key = ks.substr(1, ks.size()-2);
}



std::string KeyPress::getKey() const
{
	return key;
}



std::string KeyPress::getTranslated() const
{
	std::string key_s = "[" + key + "]";
	StringTable* table = Toolkit::getStringTable();
	if(table->doesStringExist(key_s.c_str()))
	{
		return table->getString(key_s.c_str());
	}
	return key;
}



bool KeyPress::getPressed() const
{
	return pressed;
}
