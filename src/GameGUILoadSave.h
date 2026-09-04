// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __GAME_GUI_LOAD_SAVE_H
#define __GAME_GUI_LOAD_SAVE_H

#include <GUIBase.h>
using namespace GAGGUI;
#include "GameGUIDialog.h"
#include <string>

namespace GAGGUI
{
	class List;
	class TextInput;
}

class LoadSaveScreen:public OverlayScreen
{
public:
	enum
	{
		OK = 0,
		CANCEL = 1
	};
	
private:
	List *fileList;
	TextInput *fileNameEntry;
	bool isLoad;
	std::string extension;
	std::string directory;
	std::string fileName;
	std::string (*filenameToNameFunc)(const std::string& filename);
	std::string (*nameToFilenameFunc)(const std::string& dir, const std::string& name, const std::string& extension);
	
private:
	//! create a filename from user friendly's name
	void generateFileName(void);

public:
	//! Constructor : directory and extension must be given without the / and the .
	LoadSaveScreen(const char *directory, const char *extension, bool isLoad=true, bool isScript=false, const char *defaultFileName=NULL,
		std::string (*filenameToNameFunc)(const std::string& filename)=NULL,
		std::string (*nameToFilenameFunc)(const std::string& dir, const std::string& name, const std::string& extension)=NULL);
	LoadSaveScreen(const char *directory, const char *extension, bool isLoad=true, std::string title="", const char *defaultFileName=NULL,
		std::string (*filenameToNameFunc)(const std::string& filename)=NULL,
		std::string (*nameToFilenameFunc)(const std::string& dir, const std::string& name, const std::string& extension)=NULL);
	virtual ~LoadSaveScreen();
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	virtual void onSDLEvent(SDL_Event *event);
	const char *getFileName(void);
	const char *getName(void);
};

#endif
