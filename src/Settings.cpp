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

#include "Settings.h"
#include "Utilities.h"
#include <Stream.h>
#include <BinaryStream.h>
#include <stdlib.h>
#include <GAG.h>
#include <map>
#include <fstream>
#include "boost/lexical_cast.hpp"

using namespace GAGCore;

Settings::Settings()
{
	// set default values in settings or load them
	char *newUsername;

#	ifdef WIN32
		newUsername=getenv("USERNAME");
#	else // angel > case of unix and MacIntosh Systems
		newUsername=getenv("USER");		
#	endif
	if (!newUsername)
		newUsername="player";	
	username=newUsername;

	screenFlags = GraphicContext::RESIZABLE | GraphicContext::CUSTOMCURSOR;
	screenWidth = 800;
	screenHeight = 600;
	optionFlags = 0;
	language = "en";
	musicVolume = 190;
	voiceVolume = 190;
	mute = 0;
	rememberUnit = 1;
	tempUnit = 1;
	tempUnitFuture = 1;

	scrollWheelEnabled=true;
	
	for(int n=0; n<IntBuildingType::NB_BUILDING; ++n)
	{
		for(int t=0; t<6; ++t)
		{
			defaultUnitsAssigned[n][t] = 0;
		}
	}
	defaultUnitsAssigned[IntBuildingType::WAR_FLAG][1] = 10;
	defaultUnitsAssigned[IntBuildingType::CLEARING_FLAG][1] = 5;
	defaultUnitsAssigned[IntBuildingType::EXPLORATION_FLAG][1] = 2;
	defaultUnitsAssigned[IntBuildingType::SWARM_BUILDING][0] = 7;
	defaultUnitsAssigned[IntBuildingType::SWARM_BUILDING][1] = 4;
	defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][0] = 3;
	defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][1] = 2;
	defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][2] = 5;
	defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][3] = 3;
	defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][4] = 15;
	defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][5] = 8;
	defaultUnitsAssigned[IntBuildingType::HEAL_BUILDING][0] = 2;
	defaultUnitsAssigned[IntBuildingType::HEAL_BUILDING][2] = 4;
	defaultUnitsAssigned[IntBuildingType::HEAL_BUILDING][4] = 6;
	defaultUnitsAssigned[IntBuildingType::WALKSPEED_BUILDING][0] = 3;
	defaultUnitsAssigned[IntBuildingType::WALKSPEED_BUILDING][2] = 7;
	defaultUnitsAssigned[IntBuildingType::WALKSPEED_BUILDING][4] = 12;
	defaultUnitsAssigned[IntBuildingType::SWIMSPEED_BUILDING][0] = 2;
	defaultUnitsAssigned[IntBuildingType::SWIMSPEED_BUILDING][2] = 5;
	defaultUnitsAssigned[IntBuildingType::SWIMSPEED_BUILDING][4] = 12;
	defaultUnitsAssigned[IntBuildingType::ATTACK_BUILDING][0] = 3;
	defaultUnitsAssigned[IntBuildingType::ATTACK_BUILDING][2] = 6;
	defaultUnitsAssigned[IntBuildingType::ATTACK_BUILDING][4] = 9;
	defaultUnitsAssigned[IntBuildingType::SCIENCE_BUILDING][0] = 5;
	defaultUnitsAssigned[IntBuildingType::SCIENCE_BUILDING][2] = 10;
	defaultUnitsAssigned[IntBuildingType::SCIENCE_BUILDING][4] = 20;
	defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][0] = 3;
	defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][1] = 2;
	defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][2] = 5;
	defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][3] = 2;
	defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][4] = 8;
	defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][5] = 2;
	defaultUnitsAssigned[IntBuildingType::STONE_WALL][0] = 1;
	defaultUnitsAssigned[IntBuildingType::MARKET_BUILDING][0] = 3;

	cloudPatchSize=16;//the bigger the faster the uglier
	cloudMaxAlpha=120;//the higher the nicer the clouds the harder the units are visible
	cloudMaxSpeed=3;
	cloudWindStability=3550;//how much will the wind change
	cloudStability=1300;//how much will the clouds change shape
	cloudSize=300;//the bigger the better they look with big Patches. The smaller the better they look with smaller patches
	cloudHeight=150;//(cloud - ground) / (eyes - ground) * 100 (to get an int value)
}




#define READ_PARSED_STRING(var) \
{ \
	if (parsed.find(#var) != parsed.end()) \
		var = parsed[#var]; \
}

#define READ_PARSED_INT(var) \
{ \
	if (parsed.find(#var) != parsed.end()) \
		var = atoi(parsed[#var].c_str()); \
}

void Settings::load(const char *filename)
{
	std::map<std::string, std::string> parsed;

	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "Settings::load(\"" << filename << "\") : error, can't open file." << std::endl;
	}
	else
	{
		// load and parse file
		char *dest, *varname, *token;
		char buffer[256];
		while ((dest = Utilities::gets(buffer, 256, stream))!=NULL)
		{
			token = strtok(dest,"\t\n\r=;");
			if ((!token) || (strcmp(token,"//")==0))
				continue;
			varname = token;
			token = strtok(NULL,"\t\n\r=");
			if (token)
				parsed[varname] = token;
		}
		

		// read values
		READ_PARSED_STRING(username);
		READ_PARSED_STRING(password);
		READ_PARSED_INT(screenWidth);
		READ_PARSED_INT(screenHeight);
		READ_PARSED_INT(screenFlags);
		READ_PARSED_INT(optionFlags);
		READ_PARSED_STRING(language);
		READ_PARSED_INT(musicVolume);
		READ_PARSED_INT(voiceVolume);
		READ_PARSED_INT(mute);
		READ_PARSED_INT(rememberUnit);
		READ_PARSED_INT(scrollWheelEnabled);

		for(int n=0; n<IntBuildingType::NB_BUILDING; ++n)
		{
			for(int t=0; t<6; ++t)
			{
				std::string keyname="defaultUnitsAssigned["+boost::lexical_cast<std::string>(n)+"]["+boost::lexical_cast<std::string>(t)+"]";
				if(parsed.find(keyname)!=parsed.end())
					defaultUnitsAssigned[n][t] = boost::lexical_cast<int>(parsed[keyname]);
			}
		}

		READ_PARSED_INT(cloudPatchSize);
		READ_PARSED_INT(cloudMaxAlpha);
		READ_PARSED_INT(cloudMaxSpeed);
		READ_PARSED_INT(cloudWindStability);
		READ_PARSED_INT(cloudStability);
		READ_PARSED_INT(cloudSize);
		READ_PARSED_INT(cloudHeight);
	}
	delete stream;
}

void Settings::save(const char *filename)
{
	OutputStream *stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(filename));
//	std::fstream f(filename);
	if (stream->isEndOfStream())
//	if (!f.is_open())
	{
		//std::cerr << "Settings::save(\"" << filename << "\") : error, can't open file." << std::endl;
	}
	else
	{
		Utilities::streamprintf(stream, "username=%s\n", username.c_str());
		Utilities::streamprintf(stream, "password=%s\n", password.c_str());
		Utilities::streamprintf(stream, "screenWidth=%d\n", screenWidth);
		Utilities::streamprintf(stream, "screenHeight=%d\n", screenHeight);
		Utilities::streamprintf(stream, "screenFlags=%d\n", screenFlags);
		Utilities::streamprintf(stream, "optionFlags=%d\n", optionFlags);
		Utilities::streamprintf(stream, "language=%s\n", language.c_str());
		Utilities::streamprintf(stream, "musicVolume=%d\n", musicVolume);
		Utilities::streamprintf(stream, "voiceVolume=%d\n", voiceVolume);
		Utilities::streamprintf(stream, "mute=%d\n", mute);
		Utilities::streamprintf(stream, "rememberUnit=%d\n", rememberUnit);
		Utilities::streamprintf(stream, "scrollWheelEnabled=%d\n", scrollWheelEnabled);

		for(int n=0; n<IntBuildingType::NB_BUILDING; ++n)
		{
			for(int t=0; t<6; ++t)
			{
				std::string keyname="defaultUnitsAssigned["+boost::lexical_cast<std::string>(n)+"]["+boost::lexical_cast<std::string>(t)+"]";
				Utilities::streamprintf(stream, "%s=%i\n", keyname.c_str(), defaultUnitsAssigned[n][t]);
			}
		}

		Utilities::streamprintf(stream, "cloudPatchSize=%d\n",	cloudPatchSize);
		Utilities::streamprintf(stream, "cloudMaxAlpha=%d\n",	cloudMaxAlpha);
		Utilities::streamprintf(stream, "cloudMaxSpeed=%d\n",	cloudMaxSpeed);
		Utilities::streamprintf(stream, "cloudWindStability=%d\n",	cloudWindStability);
		Utilities::streamprintf(stream, "cloudStability=%d\n",	cloudStability);
		Utilities::streamprintf(stream, "cloudSize=%d\n",	cloudSize);
		Utilities::streamprintf(stream, "cloudHeight=%d\n",	cloudHeight);
	}
	delete stream;
}
