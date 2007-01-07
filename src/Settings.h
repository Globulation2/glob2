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
	int swarmUnit0c;
	int swarmUnit0;
	int innUnit0c;
	int innUnit0;
	int innUnit1c;
	int innUnit1;
	int innUnit2c;
	int innUnit2;
	int hospitalUnit0c;
	int hospitalUnit1c;
	int hospitalUnit2c;
	int racetrackUnit0c;
	int racetrackUnit1c;
	int racetrackUnit2c;
	int swimmingpoolUnit0c;
	int swimmingpoolUnit1c;
	int swimmingpoolUnit2c;
	int barracksUnit0c;
	int barracksUnit1c;
	int barracksUnit2c;
	int schoolUnit0c;
	int schoolUnit1c;
	int schoolUnit2c;
	int defencetowerUnit0c;
	int defencetowerUnit0;
	int defencetowerUnit1c;
	int defencetowerUnit1;
	int defencetowerUnit2c;
	int defencetowerUnit2;
	int stonewallUnit0c;
	int marketUnit0c;
	int warflagUnitTemp;
	int clearflagUnitTemp;
	int exploreflagUnitTemp;
	int swarmUnitTemp0c;
	int swarmUnitTemp0;
	int innUnitTemp0c;
	int innUnitTemp0;
	int innUnitTemp1c;
	int innUnitTemp1;
	int innUnitTemp2c;
	int innUnitTemp2;
	int hospitalUnitTemp0c;
	int hospitalUnitTemp1c;
	int hospitalUnitTemp2c;
	int racetrackUnitTemp0c;
	int racetrackUnitTemp1c;
	int racetrackUnitTemp2c;
	int swimmingpoolUnitTemp0c;
	int swimmingpoolUnitTemp1c;
	int swimmingpoolUnitTemp2c;
	int barracksUnitTemp0c;
	int barracksUnitTemp1c;
	int barracksUnitTemp2c;
	int schoolUnitTemp0c;
	int schoolUnitTemp1c;
	int schoolUnitTemp2c;
	int defencetowerUnitTemp0c;
	int defencetowerUnitTemp0;
	int defencetowerUnitTemp1c;
	int defencetowerUnitTemp1;
	int defencetowerUnitTemp2c;
	int defencetowerUnitTemp2;
	int stonewallUnitTemp0c;
	int marketUnitTemp0c;
	
	std::map<std::string, std::string> keyboard_shortcuts;
	std::map<std::string, std::string> editor_keyboard_shortcuts;
	
	int campaignPlace;
	int campaignPlayed;
	
public:
	void restoreDefaultShortcuts();
	void load(const char *filename="preferences.txt");
	void save(const char *filename="preferences.txt");
	Settings();
};

#endif

