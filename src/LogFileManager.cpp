/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�e
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

#include "LogFileManager.h"
#include "FileManager.h"
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
	for (std::vector<LogFMF>::iterator logFileIt=logFileList.begin(); logFileIt!=logFileList.end(); ++logFileIt)
		if (logFileIt->file!=stdout)
			fclose(logFileIt->file);
}

FILE *LogFileManager::getFile(char *name)
{
	//printf("getFile(%s).\n", name);
	for (std::vector<LogFMF>::iterator logFileIt=logFileList.begin(); logFileIt!=logFileList.end(); ++logFileIt)
		if (logFileIt->name==name)
			return logFileIt->file;
	
	assert(fileManager);
	FILE *file=fileManager->openFP(name, "w");
	
	if (file==NULL)
		file=stdout;
	
	LogFMF logFMF;
	logFMF.name=name;
	logFMF.file=file;
	
	logFileList.push_back(logFMF);
	
	return file;
}
