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
