/*
  Copyright (C) 2003 Stephane Magnenat & Luc-Olivier de Charri�e
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

#ifndef __GUIGLOB2FILELIST_H
#define __GUIGLOB2FILELIST_H

#include <GUIFileList.h>

class Glob2FileList: public FileList
{
public:
	Glob2FileList(int x, int y, int w, int h, const Font *font, 
								const char *dir, 
								const char *extension=NULL, const bool recurse=false);
	virtual ~Glob2FileList();

	//! converts glob2 file name to displayed name
	virtual const char* fileToList(const char* fileName) const;
	//! converts displayed name to glob2 file name
	virtual const char* listToFile(const char* listName) const;

};


#endif
