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

#ifndef __GLOB_LOG_FILE_MANAGER_H
#define __GLOB_LOG_FILE_MANAGER_H

#include "Header.h"
#include <string>
#include <map>

namespace GAGCore
{
	class FileManager;
}
using namespace GAGCore;

///This is a hack to temporarilly disable log files
#define fprintf if(false)fprintf

/**
 * The LogFileManager is an utility class. It's designed to have only one
 * instance per programm. Simply call the class method "getFile()" like you
 * would call the C function "fopen()".
 * The returned "FILE*" is writeable, and stored in a place chosen by the
 * "FileManager*" policy.
 * The name of the file is the concathenation of the user name and the 
 * "const std::string filename" argument.
 * This is usefull when you have to test multiple users on the same account
 * while debugging multiplayers games.
 * 
 * If you request the same file name more than once, the LogFileManager will
 * give you a pointer "FILE*" to the same file, without opening nor closing any
 * file.
 * This way we can have all log data of all instances of the same class in the
 * same file.
 * 
 * @deprecated As logging has been disabled for bugs, and hasn't been much help
 * before anyway, this class is planned to be either removed or massively
 * reorganized before being used again.
 **/
class LogFileManager
{
public:
	LogFileManager(FileManager *fileManager);
	virtual ~LogFileManager();
	
	typedef std::map<std::string, FILE *> NameFileMap;
	NameFileMap logFileMap;
	
	FILE *getFile(const std::string filename);
	
	FileManager *fileManager;
};

#endif
