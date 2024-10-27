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

#include "LogFileManager.h"
#include "FileManager.h"
using namespace GAGCore;
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "GlobalContainer.h"

LogFileManager::LogFileManager(FileManager *fileManager)
{
	this->fileManager=fileManager;
}

LogFileManager::~LogFileManager()
{
	for (NameFileMap::iterator logFileIt=logFileMap.begin(); logFileIt!=logFileMap.end(); ++logFileIt)
		if (logFileIt->second != stdout)
			fclose(logFileIt->second);
}

FILE *LogFileManager::getFile(const std::string fileName)
{
	// FIXME: This is a hack to temporarily disable log files
	//
	// According to Bradley, logging causes crashes without this hack.
	// A major cleanup is required prior to switching logging back on.
	return stdout;

#if 0
	std::string logName = "logs/";
	logName += globalContainer->settings.getUsername();
	logName += fileName;
	if (logFileMap.find(logName) == logFileMap.end())
	{
		FILE *file=fileManager->openFP(logName.c_str(), "w");
		
		if (file==NULL)
			file = stdout;
		
		logFileMap[logName] = file;
		return file;
	}
	else
	{
		return logFileMap[logName];
	}
#endif  // 0
}
