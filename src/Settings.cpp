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

#include "Settings.h"
#include "Utilities.h"
#include <Stream.h>
#include <BinaryStream.h>
#include <stdlib.h>
#include <GAG.h>
using namespace GAGCore;
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

	screenFlags = GraphicContext::RESIZABLE | GraphicContext::CUSTOMCURSOR;
	screenWidth = 640;
	screenHeight = 480;
	optionFlags = 0;
	defaultLanguage = 0;
	musicVolume = 255;
	mute = 0;
	rememberUnit = 0;
	warflagUnit = 1;
	clearflagUnit = 1;
	exploreflagUnit = 1;
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
		READ_PARSED_INT(defaultLanguage);
		READ_PARSED_INT(musicVolume);		
		READ_PARSED_INT(mute);
		READ_PARSED_INT(rememberUnit);
		READ_PARSED_INT(warflagUnit);
		READ_PARSED_INT(clearflagUnit);
		READ_PARSED_INT(exploreflagUnit);
	}
	delete stream;
}

void Settings::save(const char *filename)
{
	OutputStream *stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "Settings::save(\"" << filename << "\") : error, can't open file." << std::endl;
	}
	else
	{
		Utilities::streamprintf(stream, "username=%s\n", username.c_str());
		Utilities::streamprintf(stream, "password=%s\n", password.c_str());
		Utilities::streamprintf(stream, "screenWidth=%d\n", screenWidth);
		Utilities::streamprintf(stream, "screenHeight=%d\n", screenHeight);
		Utilities::streamprintf(stream, "screenFlags=%d\n", screenFlags);
		Utilities::streamprintf(stream, "optionFlags=%d\n", optionFlags);
		Utilities::streamprintf(stream, "defaultLanguage=%d\n", defaultLanguage);
		Utilities::streamprintf(stream, "musicVolume=%d\n", musicVolume);
		Utilities::streamprintf(stream, "mute=%d\n", mute);
		Utilities::streamprintf(stream, "rememberUnit=%d\n", rememberUnit);
		Utilities::streamprintf(stream, "warflagUnit=%d\n", warflagUnit);
		Utilities::streamprintf(stream, "clearflagUnit=%d\n", clearflagUnit);
		Utilities::streamprintf(stream, "exploreflagUnit=%d\n", exploreflagUnit);
	}
	delete stream;
}
