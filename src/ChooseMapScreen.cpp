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

#include "ChooseMapScreen.h"
#include "GUIGlob2FileList.h"
#include "GUIMapPreview.h"
#include "GlobalContainer.h"
#include <FormatableString.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>
#include <BinaryStream.h>

#include "Game.h"

ChooseMapScreen::ChooseMapScreen(const char *directory, const char *extension, bool recurse, const char* alternateDirectory, const char* alternateExtension, const char* alternateRecurse)
{
	ok = new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	addWidget(ok);
	
	cancel = new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
	addWidget(cancel);
	
	if(alternateDirectory)
	{
		switchType = new TextButton(250, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[the games]"), SWITCHTYPE, 27);
		addWidget(switchType);

		alternateFileList = new Glob2FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", alternateDirectory, alternateExtension, alternateRecurse);
		addWidget(alternateFileList);
		alternateFileList->visible=false;
	}

	fileList = new Glob2FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", directory, extension, recurse);
	addWidget(fileList);
	
	mapPreview = new MapPreview(640-20-26-128, 70, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	addWidget(mapPreview);
	
	currentDirectoryMode=DisplayRegular;

	deleteMap = NULL;
	if (strcmp(directory, "maps") == 0)
	{
		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose map]"));
	}
	else if (strcmp(directory, "games") == 0)
	{
		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose game]"));
		deleteMap = new TextButton(220, 430, 200, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Delete game]"), DELETEGAME);
		addWidget(deleteMap);
	}
	else
	{
		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose game]"));
	}
	addWidget(title);
	mapName=new Text(440, 60+128+25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapName);
	mapInfo=new Text(440, 60+128+50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapInfo);
	mapVersion=new Text(440, 60+128+75, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapVersion);
	mapSize=new Text(440, 60+128+100, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapSize);
	mapDate=new Text(440, 60+128+125, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapDate);
	
	validMapSelected = false;
}

ChooseMapScreen::~ChooseMapScreen()
{
}

void ChooseMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action == LIST_ELEMENT_SELECTED)
	{
		if((currentDirectoryMode == DisplayRegular && fileList->getSelectionIndex() != -1) || (currentDirectoryMode == DisplayAlternate && alternateFileList->getSelectionIndex() != -1))
		{
			std::string mapFileName;
			if(currentDirectoryMode == DisplayRegular)
				mapFileName = fileList->listToFile(fileList->getText(par1).c_str());
			else
				mapFileName = alternateFileList->listToFile(alternateFileList->getText(par1).c_str());
			
			mapPreview->setMapThumbnail(mapFileName.c_str());

			InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapFileName));
			if (stream->isEndOfStream())
			{
				std::cerr << "ChooseMapScreen::onAction() : error, can't open file " << mapFileName  << std::endl;
			}
			else
			{
				if (verbose)
					std::cout << "ChooseMapScreen::onAction : loading map " << mapFileName << std::endl;
				validMapSelected = mapHeader.load(stream);
				mapHeader.setMapName(glob2FilenameToName(mapFileName));
				if (validMapSelected)
				{
					updateMapInformation();

					time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
					mapDate->setText(ctime(&mtime));
				}
				else
					std::cerr << "ChooseMapScreen::onAction : invalid map header for map " << mapFileName << std::endl;
			}
			delete stream;
		}
		else 
		{
			mapDate->setText("");
			mapVersion->setText("");
			mapInfo->setText("");
			mapSize->setText("");
			mapName->setText("");
			mapPreview->setMapThumbnail(NULL);
			validMapSelected = false;
		}
	}
	else if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (source == ok)
		{
			// we accept only if a valid map is selected
			if (validMapSelected)
				endExecute(OK);
		}
		else if (source == cancel)
		{
			endExecute(par1);
		}
		else if (source == deleteMap)
		{
			// if a valid file is selected, delete it
			if (fileList->getSelectionIndex() >= 0)
			{
				size_t i = fileList->getSelectionIndex();
				std::string mapFileName = fileList->listToFile(fileList->get().c_str());
				Toolkit::getFileManager()->remove(mapFileName);
				fileList->generateList();
				
				fileList->setSelectionIndex(std::min(i, fileList->getCount()-1));
				fileList->selectionChanged();
			}
		}
		else if (source == switchType)
		{
			if(currentDirectoryMode == DisplayRegular)
			{
				currentDirectoryMode = DisplayAlternate;
				fileList->visible=false;
				alternateFileList->visible=true;
				switchType->setText(Toolkit::getStringTable()->getString("[the maps]"));
				alternateFileList->selectionChanged();
			}
			else
			{
				currentDirectoryMode = DisplayRegular;
				fileList->visible=true;
				alternateFileList->visible=false;
				switchType->setText(Toolkit::getStringTable()->getString("[the games]"));
				fileList->selectionChanged();
			}
		}
	}
}


void ChooseMapScreen::updateMapInformation()
{
	// update map name & info
	mapName->setText(mapHeader.getMapName());
	std::string textTemp;
	textTemp = FormatableString("%0%1").arg(mapHeader.getNumberOfTeams()).arg(Toolkit::getStringTable()->getString("[teams]"));
	mapInfo->setText(textTemp);
	textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(mapHeader.getVersionMajor()).arg(mapHeader.getVersionMinor());
	mapVersion->setText(textTemp);
	textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
	mapSize->setText(textTemp);
	
	// call subclass handler
	validMapSelectedhandler();
}


MapHeader& ChooseMapScreen::getMapHeader()
{
	return mapHeader;
}


GameHeader& ChooseMapScreen::getGameHeader()
{
	return gameHeader;
}


