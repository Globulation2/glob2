/*
 * Globulation 2 file support
 * (c) 2001 Stephane Magnenat, Julien Pilet, Ysagoon
 */

#include "FileManager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

FileManager::FileManager()
{
    addDir(".");
    addDir(PACKAGE_DATA_DIR);
    addDir(PACKAGE_SOURCE_DIR);
}

FileManager::~FileManager()
{
    for (std::vector<const char *>::iterator dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
    {
        delete[] const_cast<char*>(*dirListIterator);
    }
}


void FileManager::addDir(const char *dir) 
{
	int len=strlen(dir);
	char *newDir=new char[len+1];
	strcpy(newDir, dir);
	dirList.push_back(newDir);
}

SDL_RWops *FileManager::open(const char *filename, const char *mode)
{
	for (std::vector<const char *>::iterator dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
	{
		char *fn = new char[strlen(filename) + strlen(*dirListIterator) + 2];
		sprintf(fn, "%s%c%s", *dirListIterator, DIR_SEPARATOR ,filename);

		SDL_RWops *fp =  SDL_RWFromFile(fn, mode);
		delete[] fn;
		if (fp) 
			return fp;
	}

	fprintf(stderr, "FILE %s not found.\n", filename);
	assert(false);

	return NULL;
}


FILE *FileManager::openFP(const char *filename, const char *mode)
{
	for (std::vector<const char *>::iterator dirListIterator=dirList.begin(); dirListIterator!=dirList.end(); ++dirListIterator)
	{
		char *fn = new char[strlen(filename) + strlen(*dirListIterator) + 2];
		sprintf(fn, "%s%c%s", *dirListIterator, DIR_SEPARATOR ,filename);

		FILE *fp =  fopen(fn, mode);
		delete[] fn;
		if (fp) 
			return fp;
	}

	fprintf(stderr, "FILE %s not found.\n", filename);
	assert(false);

	return NULL;
}

