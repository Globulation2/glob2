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

#ifndef __GUIFILELIST_H
#define __GUIFILELIST_H

#include "FileManager.h"
#include "GUIList.h"
#include <string>

namespace GAGGUI
{
	//! A widget that display a list of file, with the possibility to enter folders.
	class FileList: public List
	{
	protected:
		//! the starting directory, can't go upper, only recurse in subfolder
		std::string dir;
		//! extension to show
		std::string extension;
		//! if true, allow subfolder entrance
		bool recurse;
		//! current subfolder
		std::string current;
	
	public:
		//! Constructor
		FileList():List() { }
		//! Constructor, with arguments. x, y, w, h are the positional information. hAlign and vAlign the layouting flags. font the name of the font to use, dir is the initial directory in the CVS, extension is the extension to show, recurse is to allow subfolder entrance
		FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font,
						const std::string dir,
						const std::string extension="", const bool recurse=false);
		//!With a tooltip
		FileList(const std::string& tooltip, const std::string &tooltipFont):List(tooltip, tooltipFont) { }
		FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font,
						const std::string dir, const std::string& tooltip, const std::string &tooltipFont,
						const std::string extension="", const bool recurse=false);
		//! Destructor
		virtual ~FileList();
	
		//! converts file name to displayed name (default removes .extension)
		virtual std::string fileToList(const std::string fileName) const;
		//! converts displayed name to file constname (default appends .extension)
		virtual std::string listToFile(const std::string listName) const;
		//! returns the current full directory name (dir/current)
		std::string fullDir() const;
		//! returns the full file name (by prepending fullDir()/)
		std::string fullName(const std::string fileName) const;
	
		//! Sorts the list (puts directories first)
		virtual void sort(void); 
	
	public:
		//! Regenerate the list content from the current directory
		void generateList();
		//! Called when selection changes. Override List behaviour, enter subfolder if enabled and possible and signal parent otherwise
		void selectionChanged();
	};
}

#endif
