/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�e
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

#include <GUIGlob2FileList.h>
#include "GlobalContainer.h"
#include "Game.h"

Glob2FileList::Glob2FileList(int x, int y, int w, int h, const Font *font, 
														 const char *dir, 
														 const char *extension, const bool recurse)
	: FileList(x, y, w, h, font, globalContainer->fileManager, dir, extension, recurse)
{
	this->generateList();
}

Glob2FileList::~Glob2FileList()
{}

const char* Glob2FileList::fileToList(const char* fileName) const
{
	const char* fullName = this->fullName(fileName);
	const char* listName = glob2FilenameToName(fullName);
	delete[] fullName;
	return listName;
}

const char* Glob2FileList::listToFile(const char* listName) const
{
	const char* fullDir = this->fullDir();
	const char* fullName = glob2NameToFilename(fullDir, listName, this->extension.c_str());
	delete[] fullDir;
	return fullName;
}
