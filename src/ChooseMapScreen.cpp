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

	fileList = new Glob2FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", directory, extension, recurse);
	addWidget(fileList);
	
	mapPreview = new MapPreview(640-20-26-128, 70, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	addWidget(mapPreview);
	
	currentDirectoryMode=DisplayRegular;

	deleteMap = NULL;
	if (strcmp(directory, "maps") == 0)
	{
		type1 = MAP;
		type2 = GAME;

		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose map]"));
	}
	else if (strcmp(directory, "games") == 0)
	{
		type1 = GAME;
		type2 = REPLAY;

		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose game]"));
		//deleteMap = new TextButton(225, 380, 200, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Delete game]"), DELETEGAME);
		deleteMap = new TextButton(250, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[delete]"), DELETEGAME);
		addWidget(deleteMap);
	}
	else
	{
		type1 = GAME;
		type2 = NONE;

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

	if(alternateDirectory)
	{
		assert(type2 != NONE);

		std::string alternativeTypeName;

		if (type2 == GAME) alternativeTypeName = Toolkit::getStringTable()->getString("[the games]");
		else if (type2 == MAP) alternativeTypeName = Toolkit::getStringTable()->getString("[the maps]");
		else if (type2 == REPLAY) alternativeTypeName = Toolkit::getStringTable()->getString("[the replays]");
		else assert(false);

		switchType = new TextButton(250, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", alternativeTypeName.c_str(), SWITCHTYPE, 27);
		addWidget(switchType);

		alternateFileList = new Glob2FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", alternateDirectory, alternateExtension, alternateRecurse);
		addWidget(alternateFileList);
		alternateFileList->visible=false;
	}
	
	validMapSelected = false;
	selectedType = NONE;
}

ChooseMapScreen::~ChooseMapScreen()
{
}

void ChooseMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action == LIST_ELEMENT_SELECTED)
	{
		//LoadableType currentDirectoryType;

		//if (currentDirectoryMode == DisplayRegular) currentDirectoryType = type1;
		//else currentDirectoryType = type2;

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

				if (!validMapSelected) selectedType = NONE;

				mapHeader.setMapName(glob2FilenameToName(mapFileName));
				if (validMapSelected)
				{
					updateMapInformation();

					time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
					mapDate->setText(ctime(&mtime));

					if (currentDirectoryMode == DisplayRegular)
						selectedType = type1;
					else
						selectedType = type2;
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
			mapPreview->setMapThumbnail("");
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

			if(currentDirectoryMode == DisplayRegular)
			{
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
			else
			{
				if (alternateFileList->getSelectionIndex() >= 0)
				{
					size_t i = alternateFileList->getSelectionIndex();
					std::string mapFileName = alternateFileList->listToFile(alternateFileList->get().c_str());

					Toolkit::getFileManager()->remove(mapFileName);
					alternateFileList->generateList();
					
					alternateFileList->setSelectionIndex(std::min(i, fileList->getCount()-1));
					alternateFileList->selectionChanged();
				}
			}
		}
		else if (source == switchType)
		{
			if(currentDirectoryMode == DisplayRegular)
			{
				assert(type1 != NONE);

				std::string newTypeName;

				if (type1 == GAME) newTypeName = Toolkit::getStringTable()->getString("[the games]");
				else if (type1 == MAP) newTypeName = Toolkit::getStringTable()->getString("[the maps]");
				else if (type1 == REPLAY) newTypeName = Toolkit::getStringTable()->getString("[the replays]");
				else assert(false);

				currentDirectoryMode = DisplayAlternate;
				fileList->visible=false;
				alternateFileList->visible=true;
				switchType->setText(newTypeName);
				alternateFileList->selectionChanged();
			}
			else
			{
				assert(type2 != NONE);

				std::string newTypeName;

				if (type2 == GAME) newTypeName = Toolkit::getStringTable()->getString("[the games]");
				else if (type2 == MAP) newTypeName = Toolkit::getStringTable()->getString("[the maps]");
				else if (type2 == REPLAY) newTypeName = Toolkit::getStringTable()->getString("[the replays]");
				else assert(false);

				currentDirectoryMode = DisplayRegular;
				fileList->visible=true;
				alternateFileList->visible=false;
				switchType->setText(newTypeName);
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

ChooseMapScreen::LoadableType ChooseMapScreen::getSelectedType()
{
	return selectedType;
}
