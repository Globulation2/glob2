/*
 * Globulation 2 file support
 * (c) 2001 Stephane Magnenat, Julien Pilet, Ysagoon
 */

#ifndef __FILEMANAGER_H
#define __FILEMANAGER_H

#include <SDL.h>
#include <vector>

#ifndef DIR_SEPARATOR
#define DIR_SEPARATOR '/'
#endif

class FileManager 
{
private:
	std::vector<const char *> dirList;		

public:
	FileManager();
	virtual ~FileManager();

	void addDir(const char *dir);

	SDL_RWops *open(const char *filename, const char *mode="rb");
	FILE *openFP(const char *filename, const char *mode);
};

#endif
