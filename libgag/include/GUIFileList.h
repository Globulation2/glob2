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

#ifndef __GUIFILELIST_H
#define __GUIFILELIST_H

#include "FileManager.h"
#include "GUIList.h"
#include <string>

namespace GAGGUI
{
	class FileList: public List
	{
	protected:
		std::string dir;
		std::string extension;
		bool recurse;
		std::string current;
	
	public:
		FileList():List() { }
		FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font,
						const char *dir,
						const char *extension=NULL, const bool recurse=false);
		virtual ~FileList();
	
		//! converts file name to displayed name (default removes .extension)
		virtual const char* fileToList(const char* fileName) const;
		//! converts displayed name to file constname (default appends .extension)
		virtual const char* listToFile(const char* listName) const;
		//! returns the current full directory name (dir/current)
		const char* fullDir() const;
		//! returns the full file name (by prepending fullDir()/)
		const char* fullName(const char* fileName) const;
	
		//! Sorts the list (puts directories first)
		virtual void sort(void); 
	
	public:
		void generateList();
		void selectionChanged();
	};
}

#endif
