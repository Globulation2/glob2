/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __SETTINGS_H
#define __SETTINGS_H

#include "Header.h"
#include <string>
#include <map>
#include "IntBuildingType.h"
#include "BasePlayer.h" // for the MAX_NAME_LENGTH val.

class Settings
{
public:
	Settings();
	void load(const char *filename="preferences.txt");
	void save(const char *filename="preferences.txt");

/* Started to successively move public things to private and 
 * creating setter and getter functions */
private:
	std::string username;
	std::string password;

public:
	int screenWidth;
	int screenHeight;
	Uint32 screenFlags;
	Uint32 optionFlags;
	std::string language;
	Uint32 musicVolume;
	Uint32 voiceVolume;
	int mute;
	int version;
	bool rememberUnit;
	bool scrollWheelEnabled;

	// Getters and setters
	std::string getUsername();
	void setUsername(std::string);
	std::string getPasswd();
	void setPasswd(std::string);
	
	///Levels are from 0 to 5, where even numbers are building
	///under construction and odd ones are completed buildings.
	int defaultUnitsAssigned[IntBuildingType::NB_BUILDING][6];
	///Default radius of flags, 0 for exploration, 1 for war flag, 2 for clearing flag
	int defaultFlagRadius[3];

	int cloudPatchSize;//the bigger the faster the uglier
	int cloudMaxAlpha;//the higher the nicer the clouds the harder the units are visible
	int cloudMaxSpeed;
	int cloudWindStability;//how much will the wind change
	int cloudStability;//how much will the clouds change shape
	int cloudSize;//the bigger the better they look with big Patches. The smaller the better they look with smaller patches
	int cloudHeight;//(cloud - ground) / (eyes - ground)

	int tempUnit;
	int tempUnitFuture;

	void resetDefaultUnitsAssigned();
	void resetDefaultFlagRadius();
};

//Version 1 - Resets default units assigned and keyboard shortcuts
#define SETTINGS_VERSION 1

#endif
