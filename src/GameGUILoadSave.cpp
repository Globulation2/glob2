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

#include "GameGUILoadSave.h"
#include "GlobalContainer.h"
#include "Utilities.h"
#include <GUIFileList.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <GUITextInput.h>
#include <Toolkit.h>
#include <StringTable.h>

class FuncFileList: public FileList
{

public:
	FuncFileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, 
		const char *dir, const char *extension, const bool recurse, 
		std::string (*filenameToNameFunc)(const std::string& filename),
		std::string (*nameToFilenameFunc)(const std::string& dir, const std::string& name, const std::string& extension))
		: FileList(x, y, w, h, hAlign, vAlign, font, dir, extension, recurse), 
			filenameToNameFunc(filenameToNameFunc), nameToFilenameFunc(nameToFilenameFunc)
	{
		this->generateList();
	}
	
	~FuncFileList()
	{}

private:
	std::string fileToList(const char* fileName) const
	{
		return filenameToNameFunc(fullName(fileName).c_str());
	}
	
	std::string listToFile(const char* listName) const
	{
		return nameToFilenameFunc(fullDir().c_str(), listName, extension.c_str());
	}

private:
	std::string (*filenameToNameFunc)(const std::string& filename);
	std::string (*nameToFilenameFunc)(const std::string& dir, const std::string& name, const std::string& extension);

};

//! Load/Save screen

LoadSaveScreen::LoadSaveScreen(const char *directory, const char *extension, bool isLoad, bool isScript, const char *defaultFileName,
	std::string (*filenameToNameFunc)(const std::string& filename),
	std::string (*nameToFilenameFunc)(const std::string& dir, const std::string& name, const std::string& extension))
:OverlayScreen(globalContainer->gfx, 300, 275)
{
	this->isLoad = isLoad;
	if (nameToFilenameFunc)
	{
		this->extension = extension;
		this->directory = directory;
	}
	else
	{
		this->extension = std::string(".") + extension;
		this->directory = std::string(directory) + "/";
	}
	this->filenameToNameFunc = filenameToNameFunc;
	this->nameToFilenameFunc = nameToFilenameFunc;

	if(isLoad)
		fileList=new FuncFileList(10, 40, 280, 175, ALIGN_LEFT, ALIGN_LEFT, "standard", directory, extension, true, filenameToNameFunc, nameToFilenameFunc);
	else
		fileList=new FuncFileList(10, 40, 280, 140, ALIGN_LEFT, ALIGN_LEFT, "standard", directory, extension, true, filenameToNameFunc, nameToFilenameFunc);
	addWidget(fileList);

	if (!defaultFileName)
		defaultFileName="";
	fileNameEntry=new TextInput(10, 190, 280, 25, ALIGN_LEFT, ALIGN_LEFT, "standard", defaultFileName, true);
	addWidget(fileNameEntry);
	
	if(isLoad)
		fileNameEntry->visible=false;

	addWidget(new TextButton(10, 225, 135, 40, ALIGN_LEFT, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13));
	addWidget(new TextButton(155, 225, 135, 40, ALIGN_LEFT, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27));

	if (isLoad)
		if(isScript)
			addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[load script]")));
		else
			addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[load game]")));
	else
		if(isScript)
			addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[save script]")));
		else
			addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[save game]")));

	generateFileName();
	dispatchInit();
}

LoadSaveScreen::LoadSaveScreen(const char *directory, const char *extension, bool isLoad, std::string title, const char *defaultFileName,
	std::string (*filenameToNameFunc)(const std::string& filename),
	std::string (*nameToFilenameFunc)(const std::string& dir, const std::string& name, const std::string& extension))
:OverlayScreen(globalContainer->gfx, 300, 275)
{
	this->isLoad = isLoad;
	if (nameToFilenameFunc)
	{
		this->extension = extension;
		this->directory = directory;
	}
	else
	{
		this->extension = std::string(".") + extension;
		this->directory = std::string(directory) + "/";
	}
	this->filenameToNameFunc = filenameToNameFunc;
	this->nameToFilenameFunc = nameToFilenameFunc;

	if(isLoad)
		fileList=new FuncFileList(10, 40, 280, 175, ALIGN_LEFT, ALIGN_LEFT, "standard", directory, extension, true, filenameToNameFunc, nameToFilenameFunc);
	else
		fileList=new FuncFileList(10, 40, 280, 140, ALIGN_LEFT, ALIGN_LEFT, "standard", directory, extension, true, filenameToNameFunc, nameToFilenameFunc);
	addWidget(fileList);

	if (!defaultFileName)
		defaultFileName="";
	fileNameEntry=new TextInput(10, 190, 280, 25, ALIGN_LEFT, ALIGN_LEFT, "standard", defaultFileName, true);
	addWidget(fileNameEntry);
	
	if(isLoad)
		fileNameEntry->visible=false;

	addWidget(new TextButton(10, 225, 135, 40, ALIGN_LEFT, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13));
	addWidget(new TextButton(155, 225, 135, 40, ALIGN_LEFT, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27));

	addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_LEFT, "menu", title));

	generateFileName();
	dispatchInit();
}

LoadSaveScreen::~LoadSaveScreen()
{
	
}

void LoadSaveScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1 == OK)
		{
			if (fileName.size())
				endValue = OK;
		}
		else
			endValue = par1;
	}
	else if (action == LIST_ELEMENT_SELECTED)
	{
		fileNameEntry->setText(fileList->getText(par1));
		generateFileName();
	}
	else if (action == TEXT_MODIFIED)
	{
		generateFileName();
	}
}

void LoadSaveScreen::generateFileName(void)
{
	if (nameToFilenameFunc)
		fileName = nameToFilenameFunc(directory.c_str(), fileNameEntry->getText().c_str(), extension.c_str());
	else
		fileName = directory + fileNameEntry->getText() + extension;
}

void LoadSaveScreen::onSDLEvent(SDL_Event *event)
{

}

const char *LoadSaveScreen::getFileName(void)
{
	return fileName.c_str();
}

const char *LoadSaveScreen::getName(void)
{
	return fileNameEntry->getText().c_str();
}
