/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

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

#include "FileManager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// here we handle compile time options
#ifdef HAVE_CONFIG_H
#  include <config.h>
#else
#	ifdef WIN32
#		define PACKAGE_DATA_DIR ".."
#		define PACKAGE_SOURCE_DIR "../.."
#	else
#
#		define PACKAGE_DATA_DIR ".."
#		define PACKAGE_SOURCE_DIR "../.."
#	endif
#endif

// include for directory listing
#ifdef WIN32
#	include <io.h>
#else
#	include <sys/types.h>
#	include <dirent.h>
#endif

FileManager::FileManager()
{
    addDir(".");
    addDir(PACKAGE_DATA_DIR);
    addDir(PACKAGE_SOURCE_DIR);
	fileListIndex=-1;
	dirListIndexCache=-1;
	/*totTest=0;
	cMiss=0;
	cHit=0;*/
}

FileManager::~FileManager()
{
	clearDirList();
	clearFileList();
	//printf("FileManager : did %d open, cache efficiency %2.1f %\n", totTest, (float)cHit*100.0f/(float)(cHit+cMiss));
}

void FileManager::clearDirList(void)
{
	for (std::vector<const char *>::iterator dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
	{
		delete[] const_cast<char*>(*dirListIterator);
	}
	dirList.clear();
}

void FileManager::clearFileList(void)
{
	for (std::vector<const char *>::iterator fileListIterator=fileList.begin(); fileListIterator!=fileList.end(); ++fileListIterator)
	{
		delete[] const_cast<char*>(*fileListIterator);
	}
	fileList.clear();
	fileListIndex=-1;
}

void FileManager::addDir(const char *dir) 
{
	int len=strlen(dir);
	char *newDir=new char[len+1];
	strncpy(newDir, dir, len+1);
	dirList.push_back(newDir);
	dirListIndexCache=0;
}

SDL_RWops *FileManager::open(const char *filename, const char *mode, bool verboseIfNotFound)
{
	// try cache
	if (dirListIndexCache>=0)
	{
		int allocatedLength=strlen(filename) + strlen(dirList[dirListIndexCache]) + 2;
		char *fn = new char[allocatedLength];
		snprintf(fn, allocatedLength, "%s%c%s", dirList[dirListIndexCache], DIR_SEPARATOR ,filename);
		SDL_RWops *fp =  SDL_RWFromFile(fn, mode);
		delete[] fn;
		//totTest++;

		if (fp)
			return fp;
	}

	// other wise search
	int index=0;
	for (std::vector<const char *>::iterator dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
	{
		int allocatedLength=strlen(filename) + strlen(dirList[index]) + 2;
		char *fn = new char[allocatedLength];
		snprintf(fn, allocatedLength, "%s%c%s", *dirListIterator, DIR_SEPARATOR ,filename);

		SDL_RWops *fp =  SDL_RWFromFile(fn, mode);
		//totTest++;
		delete[] fn;
		if (fp)
		{
			dirListIndexCache=index;
			return fp;
		}
		index++;
	}

	if (verboseIfNotFound)
	{
		fprintf(stderr, "FILE %s not found.\n", filename);
		assert(false);
	}

	return NULL;
}


FILE *FileManager::openFP(const char *filename, const char *mode, bool verboseIfNotFound)
{
	// try cache
	if (dirListIndexCache>=0)
	{
		int allocatedLength=strlen(filename) + strlen(dirList[dirListIndexCache]) + 2;
		char *fn = new char[allocatedLength];
		snprintf(fn, allocatedLength, "%s%c%s", dirList[dirListIndexCache], DIR_SEPARATOR ,filename);
		FILE *fp =  fopen(fn, mode);
		//totTest++;
		delete[] fn;
		if (fp)
			return fp;
	}

	// other wise search
	int index=0;
	for (std::vector<const char *>::iterator dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
	{
		int allocatedLength=strlen(filename) + strlen(dirList[index]) + 2;
		char *fn = new char[allocatedLength];
		snprintf(fn, allocatedLength, "%s%c%s", *dirListIterator, DIR_SEPARATOR ,filename);

		FILE *fp =  fopen(fn, mode);
		//totTest++;
		delete[] fn;
		if (fp)
		{
			dirListIndexCache=index;
			return fp;
		}
		index++;
	}

	if (verboseIfNotFound)
	{
		fprintf(stderr, "FILE %s not found.\n", filename);
		assert(false);
	}

	return NULL;
}

bool FileManager::addListingForDir(const char *realDir, const char *extension)
{
#ifdef WIN32
	// TODO : put Win32 directory listing (findfirst, findnext) here
#else
	DIR *dir=opendir(realDir);
	struct dirent *dirEntry;

	if (!dir)
		return false;

	while ((dirEntry=readdir(dir))!=NULL)
	{
		int l, nl;
		l=strlen(extension);
		nl=strlen(dirEntry->d_name);
		if ((nl>l) &&
			(dirEntry->d_name[nl-l-1]=='.') &&
			(strcmp(extension,dirEntry->d_name+(nl-l))==0))
		{
			// test if name already exists in vector
			bool alreadyIn=false;
			for (std::vector<const char *>::iterator fileListIterator=fileList.begin(); (fileListIterator!=fileList.end())&&(alreadyIn==false); ++fileListIterator)
			{
				if (strcmp(dirEntry->d_name, *fileListIterator)==0)
					alreadyIn=true;
			}
			if (!alreadyIn)
			{
				int len=strlen(dirEntry->d_name)+1;
				char *fileName=new char[len];
				strncpy(fileName, dirEntry->d_name, len);
				fileList.push_back(fileName);
			}
		}
	}

	closedir(dir);
#endif
	return true;
}

bool FileManager::initDirectoryListing(const char *virtualDir, const char *extension)
{
	bool result=false;
	clearFileList();
	for (std::vector<const char *>::iterator dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
	{
		int allocatedLength=strlen(virtualDir) + strlen(*dirListIterator) + 2;
		char *dn = new char[allocatedLength];
		snprintf(dn, allocatedLength,  "%s%c%s", *dirListIterator, DIR_SEPARATOR ,virtualDir);
		result=result||addListingForDir(dn, extension);
		delete[] dn;
	}
	// TODO : for Gabriel, sort result
	if (result)
		fileListIndex=0;
	else
		fileListIndex=-1;
	return result;
}

const char *FileManager::getNextDirectoryEntry(void)
{
	if ((fileListIndex>=0) && (fileListIndex<(signed)fileList.size()))
	{
		return fileList[fileListIndex++];
	}
	return NULL;
}

