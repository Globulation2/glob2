/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __FILEMANAGER_H
#define __FILEMANAGER_H

#include "Header.h"
#include <vector>

#ifndef DIR_SEPARATOR
#define DIR_SEPARATOR '/'
#endif

//! File Manager (filesystem abstraction)
class FileManager
{
private:
	//! List of directory where to search for requested file
	std::vector<const char *> dirList;
	//! List of file relative to virtual base address after call to initDirectoryListing
	std::vector<const char *> fileList;
	//! Index in the dirFileList vector
	int fileListIndex;
	//! Last index in dir list, accelerate loading
	int dirListIndexCache;

	//int totTest, cMiss, cHit;

private:
	//! clear the list of directory
	void clearDirList(void);
	//! clear the list of file for directory listing
	void clearFileList(void);
	//! internal function that does the real listing job
	bool addListingForDir(const char *realDir, const char *extension);
	//! open a file, if it is in writing, do a backup
	SDL_RWops *openWithbackup(const char *filename, const char *mode);
	//! open a file, if it is in writing, do a backup, fopen version
	FILE *openWithbackupFP(const char *filename, const char *mode);

public:
	//! FileManager constructor
	FileManager();
	//! FileManager destructor
	virtual ~FileManager();

	//! Add a directory to the search list
	void addDir(const char *dir);

	//! Open a file in the SDL_RWops format
	SDL_RWops *open(const char *filename, const char *mode="rb", bool verboseIfNotFound=DBG_VPATH_OPEN);
	//! Open a file in the FILE* format
	FILE *openFP(const char *filename, const char *mode, bool verboseIfNotFound=DBG_VPATH_OPEN);

	// FIXME : the following functions are not thread-safe :
	//! must be call before directory listening, return true if success
	bool initDirectoryListing(const char *virtualDir, const char *extension);
	//! get the next name, return NULL if none
	const char *getNextDirectoryEntry(void);
};

#endif
