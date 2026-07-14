// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "LogFileManager.h"
#include "FileManager.h"
using namespace GAGCore;
#include <assert.h>
#include <stdio.h>
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
	// FIXME: This is a hack to temporarilly disable log files
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
