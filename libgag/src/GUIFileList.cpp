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

#include <GUIFileList.h>
#include <SupportFunctions.h>
#include <functional>
#include <algorithm>
#include <iostream>
#include <Toolkit.h>

FileList::FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font,
									 const char *dir,
									 const char *extension, const bool recurse)
	: List(x, y, w, h, hAlign, vAlign, font),
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
	if (Toolkit::getFileManager()->initDirectoryListing(fullDir.c_str(), this->extension.c_str(), this->recurse))
	{
		const char* fileName;
		while ((fileName=(Toolkit::getFileManager()->getNextDirectoryEntry()))!=NULL)
		{
			std::string fullFileName = fullDir + DIR_SEPARATOR + fileName;
			if (Toolkit::getFileManager()->isDir(fullFileName.c_str()))
			{
				std::string dirName = std::string(fileName) + DIR_SEPARATOR;
				this->addText(dirName.c_str());
			}
			else
			{
				const char* listName = this->fileToList(fileName);
				this->addText(listName);
				delete[] listName;
			}
		}
		this->sort();
	}
}

void FileList::selectionChanged()
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
		else {
			if (! current.empty())
				this->current += DIR_SEPARATOR;
			this->current += selName;
		}
		this->generateList();
		this->nth = -1;
	}
	this->parent->onAction(this, LIST_ELEMENT_SELECTED, this->nth, 0);
}

const char* FileList::fileToList(const char* fileName) const
{
	// this default behaviour is probably not what you want
	std::cout << "FileList::fileToList !!!" << std::endl;
	std::string listName(fileName);
	if (! extension.empty())
		listName.resize(listName.size() - (extension.size() + 1));
	return GAG::newstrdup(listName.c_str());
}

const char* FileList::listToFile(const char* listName) const
{
	// this default behaviour is probably not what you want
	std::cout << "FileList::listToFile !!!" << std::endl;
	std::string fileName(listName);
	if (! this->extension.empty())
	{
		fileName += "." + extension;
	}
	return GAG::newstrdup(fileName.c_str());
}

const char* FileList::fullDir() const
{
	std::string fullDir = this->dir;
	if (! this->current.empty())
		fullDir += DIR_SEPARATOR + this->current;
	return GAG::newstrdup(fullDir.c_str());
}

const char* FileList::fullName(const char* fileName) const
{
	const char* fullDir = this->fullDir();
	std::string fullName = fullDir;
	fullName += DIR_SEPARATOR;
	fullName += fileName;
	delete[] fullDir;
	return GAG::newstrdup(fullName.c_str());
}

struct strfilecmp_functor : public std::binary_function<std::string, std::string, bool>
{
	bool operator()(std::string x, std::string y)
	{
		bool xIsNotDir = (x[x.length()-1] != DIR_SEPARATOR);
		bool yIsNotDir = (y[y.length()-1] != DIR_SEPARATOR);
		return ((xIsNotDir == yIsNotDir)?(x<y):xIsNotDir<yIsNotDir);
	}
};

void FileList::sort(void)
{
	std::sort(strings.begin(), strings.end(), strfilecmp_functor());
}
