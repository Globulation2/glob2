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

#include "KeyPress.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "GUIBase.h"
#include "FormatableString.h"

#include <algorithm>

using namespace GAGCore;
using namespace GAGGUI;

KeyPress::KeyPress(SDL_Keysym nkey, bool pressed)
	: pressed(pressed)
{
	if(nkey.mod & KMOD_CTRL)
		control = true;
	else
		control = false;
	if(nkey.mod & KMOD_SHIFT)
		shift = true;
	else
		shift = false;
	if(nkey.mod & KMOD_LGUI || nkey.mod & KMOD_RGUI)
		meta = true;
	else
		meta = false;
	if(nkey.mod & KMOD_ALT)
		alt = true;
	else
		alt = false;

	std::string name = SDL_GetKeyName(nkey.sym);
	std::transform(name.begin(), name.end(), name.begin(), ::tolower);
	std::string key_s = std::string("[") + name + std::string("]");
	Uint16 c=0;
	//This is to get over a bug where ctrl-d ctrl-a etc... would cause nkey.unicode to be mangled,
	//whereas nkey.sym is still fine
	if(nkey.sym < 128)
		c = nkey.sym;
	
	if(Toolkit::getStringTable()->doesStringExist(key_s))
	{
		key = name;
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
		key = name;
	}
}



KeyPress::KeyPress(KeyPress key, bool npressed)
{
	*this = key;
	pressed = npressed;
}



KeyPress::KeyPress()
{
	key="no key";
	pressed=true;
	control = false;
	shift = false;
	meta = false;
	alt = false;
}


bool KeyPress::operator<(const KeyPress& rhs) const
{
	if(key == rhs.key)
	{
		if(alt == rhs.alt)
		{
			if(control == rhs.control)
			{
				if(meta == rhs.meta)
				{
					if(shift == rhs.shift)
						return pressed < rhs.pressed;
					else
						return shift < rhs.shift;
				}
				else
					return meta < rhs.meta;
			}
			else
				return control < rhs.control;
		}
		else
			return alt < rhs.alt;
	}
	return key < rhs.key;
}



bool KeyPress::operator!=(const KeyPress& rhs) const
{
	if(key != rhs.key)
		return true;
	if(pressed != rhs.pressed)
		return true;
	if(alt != rhs.alt)
		return true;
	if(control != rhs.control)
		return true;
	if(meta != rhs.meta)
		return true;
	if(shift != rhs.shift)
		return true;
	return false;
}



bool KeyPress::operator==(const KeyPress& rhs) const
{
	return key == rhs.key && pressed == rhs.pressed && alt == rhs.alt && control == rhs.control && meta == rhs.meta && shift==rhs.shift;
}


	
std::string KeyPress::format() const
{
	std::string s;
	if(!pressed)
		s+="<unpress>";
	if(alt)
		s+="<alt>";
	if(control)
		s+="<control>";
	if(meta)
		s+="<meta>";
	if(shift)
		s+="<shift>";
	s+=FormattableString("<%0>").arg(key);
	return s;
}



void KeyPress::interpret(const std::string& s)
{
	std::string ks = s;
	size_t pos = ks.find("<unpress>");
	if(pos!=std::string::npos)
	{
		ks = ks.substr(pos+9);
		pressed=false;
	}
	else
	{
		pressed=true;
	}
	pos = ks.find("<alt>");
	if(pos!=std::string::npos)
	{
		ks = ks.substr(pos+5);
		alt=true;
	}
	else
	{
		alt=false;
	}
	pos = ks.find("<control>");
	if(pos!=std::string::npos)
	{
		ks = ks.substr(pos+9);
		control=true;
	}
	else
	{
		control=false;
	}
	pos = ks.find("<meta>");
	if(pos!=std::string::npos)
	{
		ks = ks.substr(pos+6);
		meta=true;
	}
	else
	{
		meta=false;
	}
	pos = ks.find("<shift>");
	if(pos!=std::string::npos)
	{
		ks = ks.substr(pos+7);
		shift=true;
	}
	else
	{
		shift=false;
	}
	key = ks.substr(1, ks.size()-2);
}



std::string KeyPress::getKey() const
{
	return key;
}



std::string KeyPress::getTranslated() const
{
	StringTable* table = Toolkit::getStringTable();
	std::string str;
	std::string key_s = "[" + key + "]";
	if(table->doesStringExist(key_s))
	{
		str=table->getString(key_s);
	}
	else
	{
		str = key;
	}
	//Visual order, control always first. Rather than alt-control-a for example, its control-alt-a
	if(alt)
		str=FormattableString(table->getString("[alt %0]")).arg(str);
	if(meta)
		str=FormattableString(table->getString("[meta %0]")).arg(str);
	if(shift)
		str=FormattableString(table->getString("[shift %0]")).arg(str);
	if(control)
		str=FormattableString(table->getString("[control %0]")).arg(str);
	return str;
}



bool KeyPress::getPressed() const
{
	return pressed;
}



bool KeyPress::needAlt() const
{
	return alt;
}



bool KeyPress::needControl() const
{
	return control;
}



bool KeyPress::needMeta() const
{
	return meta;
}



bool KeyPress::needShift() const
{
	return shift;
}


