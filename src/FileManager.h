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

	SDL_RWops *open(const char *filename, const char *mode="rb", bool verboseIfNotFound=true);
	FILE *openFP(const char *filename, const char *mode, bool verboseIfNotFound=true);
};

#endif
