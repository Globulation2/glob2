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

#include <GUIFileList.h>
#include <SupportFunctions.h>
#include <functional>
#include <algorithm>
#include <iostream>
#include <Toolkit.h>
#include "TextSort.h"

using namespace GAGCore;

namespace GAGGUI
{
	FileList::FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font,
										const std::string dir,
										const std::string extension, const bool recurse)
		: List(x, y, w, h, hAlign, vAlign, font),
			dir(dir),
			extension(extension), recurse(recurse), 
			current("")
	{
		// There is a problem with this call: it doesn't use the child's virtual methods (fileToList and listToFile)
		// For now: call it explicitly from the child's constructor or the program
		//this->generateList();
	}
	
	FileList::FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font,
										const std::string dir, const std::string& tooltip, const std::string &tooltipFont,
										const std::string extension, const bool recurse)
		: List(x, y, w, h, hAlign, vAlign, font, tooltip, tooltipFont),
			dir(dir),
			extension(extension), recurse(recurse), 
			current("")
	{
		// There is a problem with this call: it doesn't use the child's virtual methods (fileToList and listToFile)
		// For now: call it explicitly from the child's constructor or the program
		//this->generateList();
	}
	FileList::~FileList()
	{}
	
	void FileList::generateList()
	{
		// we free the current list
		this->strings.clear();
	
		std::string fullDir = this->dir;
		// we add the parent directory
		if (! this->current.empty())
		{
			this->addText("../");
			fullDir += DIR_SEPARATOR + this->current;
		}
		// we add the other files
		if (Toolkit::getFileManager()->initDirectoryListing(fullDir.c_str(), this->extension, this->recurse))
		{
			std::string filename;
			while (!(filename = Toolkit::getFileManager()->getNextDirectoryEntry()).empty())
			{
				std::string fullFileName = fullDir + DIR_SEPARATOR + filename;
				if (Toolkit::getFileManager()->isDir(fullFileName.c_str()))
				{
					std::string dirName = std::string(filename) + DIR_SEPARATOR;
					this->addText(dirName.c_str());
				}
				else
				{
					std::string listName = this->fileToList(filename);
					if (listName.length())
						this->addText(listName.c_str());
				}
			}
			this->sort();
		}
		
		// we deselect
		this->nth = -1;
	}
	
	void FileList::selectionChanged()
	{
		if(this->nth != -1)
		{
			std::string selName = this->strings[this->nth];
			std::string::iterator last = selName.end();
			last--;
			// this will only work if only the directories have a trailing DIR_SEPARATOR
			if (*last == DIR_SEPARATOR)
			{
				selName.erase(last);
				// parent directory, unstack a folder
				if (selName == "..")
				{
					std::string::size_type lastDirSep = this->current.find_last_of(DIR_SEPARATOR);
					if (lastDirSep == std::string::npos)
						lastDirSep = 0;
					this->current.erase(lastDirSep, this->current.length());
				}
				// child directory, stack selection
				else
				{
					if (! current.empty())
						this->current += DIR_SEPARATOR;
					this->current += selName;
				}
				this->generateList();
				this->nth = -1;
			}
			else
				this->parent->onAction(this, LIST_ELEMENT_SELECTED, this->nth, 0);
		}
		else
		{
			this->current = "";
			this->parent->onAction(this, LIST_ELEMENT_SELECTED, this->nth, 0);
		}
	}
	
	std::string FileList::fileToList(const std::string fileName) const
	{
		// this default behaviour is probably not what you want
		//std::cout << "FileList::fileToList(\"" << fileName << "\") !" << std::endl;
		std::string listName(fileName);
		if (! extension.empty())
			listName.resize(listName.size() - (extension.size() + 1));
		return listName;
	}
	
	std::string FileList::listToFile(const std::string listName) const
	{
		// this default behaviour is probably not what you want
		//std::cout << "FileList::listToFile(\"" << listName << "\") !" << std::endl;
		std::string fileName(listName);
		if (! this->extension.empty())
		{
			fileName += "." + extension;
		}
		return fileName;
	}
	
	std::string FileList::fullDir() const
	{
		std::string fullDir = this->dir;
		if (! this->current.empty())
			fullDir += DIR_SEPARATOR + this->current;
		return fullDir.c_str();
	}
	
	std::string FileList::fullName(const std::string fileName) const
	{
		std::string fullName = fullDir();
		fullName += DIR_SEPARATOR;
		fullName += fileName;
		return fullName;
	}
	
	struct strfilecmp_functor : public std::binary_function<std::string, std::string, bool>
	{
		bool operator()(std::string x, std::string y)
		{
			bool xIsNotDir = (x[x.length()-1] != DIR_SEPARATOR);
			bool yIsNotDir = (y[y.length()-1] != DIR_SEPARATOR);
			return ((xIsNotDir == yIsNotDir)?(naturalStringSort(x,y)):xIsNotDir<yIsNotDir);
		}
	};
	
	void FileList::sort(void)
	{
		std::sort(strings.begin(), strings.end(), strfilecmp_functor());
	}
}
