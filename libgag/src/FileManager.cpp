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

#include <FileManager.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <SDL_endian.h>

// here we handle compile time options
#ifdef HAVE_CONFIG_H
#  include <config.h>
#else
#	ifdef WIN32
#		define PACKAGE_DATA_DIR ".."
#		define PACKAGE_SOURCE_DIR "../.."
#	else
#		define PACKAGE_DATA_DIR ".."
#		define PACKAGE_SOURCE_DIR "../.."
#	endif
#endif

// include for directory listing
#ifdef WIN32
#	include <windows.h>
#	include <io.h>
#	include <direct.h>
#	include "win32_dirent.h"
#else
#	include <sys/types.h>
#	include <dirent.h>
#	include <sys/stat.h>
#endif

FileManager::FileManager(const char *gameName)
{
#ifndef WIN32
	char gameLocal[256];
	snprintf(gameLocal, sizeof(gameLocal), "%s/.%s", getenv("HOME"), gameName);
	mkdir(gameLocal, S_IRWXU);
	addDir(gameLocal);
#endif
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
	size_t len=strlen(dir);
	char *newDir=new char[len+1];
	strncpy(newDir, dir, len+1);
	dirList.push_back(newDir);
	dirListIndexCache=-1;
}

void FileManager::addWriteSubdir(const char *subdir)
{
	size_t subdirLen=strlen(subdir);
	for (std::vector<const char *>::iterator dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
	{
		const char *baseDir=*dirListIterator;
		size_t dirLen=strlen(baseDir);
		char *toCreate=new char[subdirLen+dirLen+2];
		
		strncpy(toCreate, baseDir, dirLen);
		toCreate[dirLen]='/';
		strncpy(toCreate+dirLen+1, subdir, subdirLen+1);
		
#ifdef WIN32
		int result=_mkdir(toCreate);
#else
		int result=mkdir(toCreate, S_IRWXU);
#endif
		
		delete[] toCreate;
		if (result==0)
			break;
		if ((result==-1) && (errno==EEXIST))
			break;
	}
}

SDL_RWops *FileManager::openWithbackup(const char *filename, const char *mode)
{
	if (strchr(mode, 'w'))
	{
		char backupText[512];
		snprintf(backupText, sizeof(backupText), "%s~", filename);
		rename(filename, backupText);
	}
	return SDL_RWFromFile(filename, mode);
}

FILE *FileManager::openWithbackupFP(const char *filename, const char *mode)
{
	if (strchr(mode, 'w'))
	{
		char backupText[512];
		snprintf(backupText, sizeof(backupText), "%s~", filename);
		rename(filename, backupText);
	}
	return fopen(filename, mode);
}

SDL_RWops *FileManager::open(const char *filename, const char *mode, bool verboseIfNotFound)
{
	std::vector<const char *>::iterator dirListIterator;

	// try cache
	if ((strchr(mode, 'w')==NULL) && (dirListIndexCache>=0))
	{
		size_t allocatedLength=strlen(filename) + strlen(dirList[dirListIndexCache]) + 2;
		char *fn = new char[allocatedLength];
		snprintf(fn, allocatedLength, "%s%c%s", dirList[dirListIndexCache], DIR_SEPARATOR ,filename);
		SDL_RWops *fp = openWithbackup(fn, mode);
		delete[] fn;
		//totTest++;

		if (fp)
			return fp;
	}

	// other wise search
	int index=0;
	for (dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
	{
		size_t allocatedLength=strlen(filename) + strlen(dirList[index]) + 2;
		char *fn = new char[allocatedLength];
		snprintf(fn, allocatedLength, "%s%c%s", *dirListIterator, DIR_SEPARATOR ,filename);

		SDL_RWops *fp =  openWithbackup(fn, mode);
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
		fprintf(stderr, "GAG : FILE %s not found in mode %s.\n", filename, mode);
		fprintf(stderr, "Searched path :\n");
		for (dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
		{
			printf("%s\n", *dirListIterator);
		}
	}

	return NULL;
}


FILE *FileManager::openFP(const char *filename, const char *mode, bool verboseIfNotFound)
{
	std::vector<const char *>::iterator dirListIterator;

	// try cache
	if ((strchr(mode, 'w')==NULL) && (dirListIndexCache>=0))
	{
		size_t allocatedLength=strlen(filename) + strlen(dirList[dirListIndexCache]) + 2;
		char *fn = new char[allocatedLength];
		snprintf(fn, allocatedLength, "%s%c%s", dirList[dirListIndexCache], DIR_SEPARATOR ,filename);
		FILE *fp =  openWithbackupFP(fn, mode);
		//totTest++;
		delete[] fn;
		if (fp)
			return fp;
	}

	// other wise search
	int index=0;
	for (dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
	{
		size_t allocatedLength=strlen(filename) + strlen(dirList[index]) + 2;
		char *fn = new char[allocatedLength];
		snprintf(fn, allocatedLength, "%s%c%s", *dirListIterator, DIR_SEPARATOR ,filename);

		FILE *fp =  openWithbackupFP(fn, mode);
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
		fprintf(stderr, "GAG : FILE %s not found in mode %s.\n", filename, mode);
		fprintf(stderr, "Searched path :\n");
		for (dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
		{
			printf("%s\n", *dirListIterator);
		}
	}

	return NULL;
}

Uint32 FileManager::checksum(const char *filename)
{
	Uint32 cs = 0;
	SDL_RWops *stream = open(filename);
	if (stream)
	{
		int length = SDL_RWseek(stream, 0, SEEK_END);
		SDL_RWseek(stream, 0, SEEK_SET);
		
		int lengthBlock = length & (~0x3);
		for (int i=0; i<(lengthBlock>>2); i++)
		{
			cs ^= SDL_ReadBE32(stream);
			cs = (cs<<31)|(cs>>1);
		}
		int lengthRest = length & 0x3;
		for (int i=0; i<lengthRest; i++)
		{
			unsigned char c;
			SDL_RWread(stream, &c, 1, 1);
			cs ^= (static_cast<Uint32>(c))<<(8*i);
		}
		SDL_RWclose(stream);
	}
	return cs;
}

void FileManager::remove(const char *filename)
{
	std::vector<const char *>::iterator dirListIterator;

	// other wise search
	for (dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
	{
		size_t allocatedLength=strlen(filename) + strlen(*dirListIterator) + 2;
		char *fn = new char[allocatedLength];
		snprintf(fn, allocatedLength, "%s%c%s", *dirListIterator, DIR_SEPARATOR, filename);
		std::remove(fn);
		delete[] fn;
	}
}

bool FileManager::isDir(const char *filename)
{
	std::vector<const char *>::iterator dirListIterator = dirList.begin();
#ifdef WIN32
	struct _stat s;
#else
	struct stat s;
#endif

	int serr = 1;
	while ((serr) && (dirListIterator != dirList.end()))
	{
		size_t allocatedLength=strlen(filename) + strlen(*dirListIterator) + 2;
		char *fn = new char[allocatedLength];
		snprintf(fn, allocatedLength, "%s%c%s", *dirListIterator, DIR_SEPARATOR, filename);
#ifdef WIN32
		serr = ::_stat(fn, &s);
#else
		serr = stat(fn, &s);
#endif
		delete[] fn;
		dirListIterator++;
	}
	return (s.st_mode&S_IFDIR)!=0;
}

bool FileManager::addListingForDir(const char *realDir, const char *extension, const bool dirs)
{
	DIR *dir=opendir(realDir);
	struct dirent *dirEntry;

	if (!dir)
	{
#ifdef DBG_VPATH_LIST
		fprintf(stderr, "GAG : Open dir failed for dir %s\n", realDir);
#endif
		return false;
	}

	while ((dirEntry=readdir(dir))!=NULL)
	{
#ifdef DBG_VPATH_LIST
		fprintf(stderr, "%s\n", dirEntry->d_name);
#endif
		// there might be a way to optimize the decision of the ok
		bool ok = true;
		// hide hidden stuff
		if (dirEntry->d_name[0] == '.')
		{
			ok = false;
		}
		// take directories if asked
		else if (dirEntry->d_type == DT_DIR)
		{
			ok = dirs;
		}
		// check extension if provided
		else if (extension) 
		{
			size_t l, nl;
			l=strlen(extension);
			nl=strlen(dirEntry->d_name);
			ok = ((nl>l) &&
			      (dirEntry->d_name[nl-l-1]=='.') &&
			      (strcmp(extension,dirEntry->d_name+(nl-l))==0));
		}
		if (ok)
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
				size_t len=strlen(dirEntry->d_name)+1;
				char *fileName=new char[len];
				strncpy(fileName, dirEntry->d_name, len);
				fileList.push_back(fileName);
			}
		}
	}

	closedir(dir);
	return true;
}

bool FileManager::initDirectoryListing(const char *virtualDir, const char *extension, const bool dirs)
{
	bool result=false;
	clearFileList();
	for (std::vector<const char *>::iterator dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
	{
		size_t allocatedLength=strlen(virtualDir) + strlen(*dirListIterator) + 2;
		char *dn = new char[allocatedLength];
		snprintf(dn, allocatedLength,  "%s%c%s", *dirListIterator, DIR_SEPARATOR ,virtualDir);
#ifdef DBG_VPATH_LIST
		fprintf(stderr, "GAG : Listing from dir %s :\n", dn);
#endif
		result=addListingForDir(dn, extension, dirs) || result;
		delete[] dn;
	}
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

