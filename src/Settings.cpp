/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "Settings.h"
#include "Utilities.h"
#include <stdlib.h>
#include <GAG.h>
#include <map>

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

	screenFlags=DrawableSurface::RESIZABLE;
	screenWidth=640;
	screenHeight=480;
	graphicType=DrawableSurface::GC_SDL;
	optionFlags=0;
	defaultLanguage=0;
}

void Settings::load(const char *filename)
{
	std::map<std::string, std::string> parsed;

	SDL_RWops *stream=Toolkit::getFileManager()->open(filename, "r");
	if (stream)
	{
		// load and parse file
		char *dest, *varname, *token;
		char buffer[256];
		while ((dest=Utilities::gets(buffer, 256, stream))!=NULL)
		{
			token=strtok(dest,"\t\n\r=;");
			if ((!token) || (strcmp(token,"//")==0))
				continue;
			varname=token;
			token=strtok(NULL,"\t\n\r=");
			if (token)
				parsed.insert(std::pair<std::string, std::string>(varname, token));
		}
		SDL_RWclose(stream);

		// read values
		username=parsed["username"];
		screenWidth=atoi(parsed["screenWidth"].c_str());
		screenHeight=atoi(parsed["screenHeight"].c_str());
		screenFlags=atoi(parsed["screenFlags"].c_str());
		optionFlags=atoi(parsed["optionFlags"].c_str());
		graphicType=atoi(parsed["graphicType"].c_str());
		defaultLanguage=atoi(parsed["defaultLanguage"].c_str());
	}
}

void Settings::save(const char *filename)
{
	SDL_RWops *stream=Toolkit::getFileManager()->open(filename, "w");
	if (stream)
	{
		Utilities::streamprintf(stream, "username=%s\n", username.c_str());
		Utilities::streamprintf(stream, "screenWidth=%d\n", screenWidth);
		Utilities::streamprintf(stream, "screenHeight=%d\n", screenHeight);
		Utilities::streamprintf(stream, "screenFlags=%d\n", screenFlags);
		Utilities::streamprintf(stream, "optionFlags=%d\n", optionFlags);
		Utilities::streamprintf(stream, "graphicType=%d\n", graphicType);
		Utilities::streamprintf(stream, "defaultLanguage=%d\n", defaultLanguage);
	}
}
