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

FILE *LogFileManager::getFile(const char *fileName)
{
	//printf("getFile(%s).\n", name);
	int logPrefixSize=strlen("logs/");
	int fileNameSize=strlen(fileName);
	int userNameSize=strlen(globalContainer->userName);
	int fullSize=logPrefixSize+fileNameSize+userNameSize+1;
	char *fullName=new char[fullSize];
	assert(fullName);
	snprintf(fullName, fullSize, "logs/%s%s", globalContainer->userName, fileName);
		
	for (std::vector<LogFMF>::iterator logFileIt=logFileList.begin(); logFileIt!=logFileList.end(); ++logFileIt)
		if (logFileIt->name==fullName)
			return logFileIt->file;
	
	assert(fileManager);
	FILE *file=fileManager->openFP(fullName, "w");
	
	if (file==NULL)
		file=stdout;
	
	LogFMF logFMF;
	logFMF.name=fullName;
	logFMF.file=file;
	
	delete[] fullName;
	
	logFileList.push_back(logFMF);
	
	return file;
}
