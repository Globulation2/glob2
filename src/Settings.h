/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __SETTINGS_H
#define __SETTINGS_H

#include "Header.h"
#include <string>
#include <map>

class Settings
{
public:
	std::string username;
	std::string password;
	int screenWidth;
	int screenHeight;
	Uint32 screenFlags;
	Uint32 optionFlags;
	Uint32 defaultLanguage;
	Uint32 musicVolume;
	int mute;
	bool rememberUnit;
	int warflagUnit;
	int clearflagUnit;
	int exploreflagUnit;
	std::map<std::string, std::string> keyboard_shortcuts;
public:
	void load(const char *filename="preferences.txt");
	void save(const char *filename="preferences.txt");
	Settings();
};

#endif

