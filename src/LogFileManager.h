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

#ifndef __GLOB_LOG_FILE_MANAGER_H
#define __GLOB_LOG_FILE_MANAGER_H

#include "Header.h"
#include <string>
#include <vector>

class FileManager;

class LogFileManager
{
public:
	LogFileManager(FileManager *fileManager);
	virtual ~LogFileManager();
	
	typedef struct
	{
		std::string name;
		FILE *file;
	} LogFMF; //FMF means FileManagerFile
	
	std::vector<LogFMF> logFileList;
	
	FILE *getFile(const char *fileName);
	
	FileManager *fileManager;
};

#endif
