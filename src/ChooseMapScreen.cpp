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

#include "ChooseMapScreen.h"
#include "GUIGlob2FileList.h"
#include "GUIMapPreview.h"
#include "Session.h"
#include "GlobalContainer.h"
#include <FormatableString.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>
#include <BinaryStream.h>

ChooseMapScreen::ChooseMapScreen(const char *directory, const char *extension, bool recurse)
{
	ok = new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	addWidget(ok);
	
	cancel = new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
	addWidget(cancel);
	
	fileList = new Glob2FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", directory, extension, recurse);
	addWidget(fileList);
	
	mapPreview = new MapPreview(640-20-26-128, 70, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	addWidget(mapPreview);
	
	deleteMap = NULL;
	if (strcmp(directory, "maps") == 0)
	{
		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose map]"));
	}
	else if (strcmp(directory, "games") == 0)
	{
		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose game]"));
		deleteMap = new TextButton(220, 430, 200, 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Delete game]"), DELETEGAME);
		addWidget(deleteMap);
		
		map1 = map2 = map3 = map4 = map5 = map6 = map7 = 0;
	}
	else
	{
		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", "choose scenario");
		fileList->visible = false;
		
		map1 = new TextButton(20, 75, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", "Escape", OK, 13);
		map2 = new TextButton(20, 125, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", "Growth", OK, 13);
		map3 = new TextButton(20, 175, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", "Aquaglobs", OK, 13);
		map4 = new TextButton(20, 225, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", "Capture", OK, 13);
		map5 = new TextButton(20, 275, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", "Invasion", OK, 13);
		map6 = new TextButton(20, 325, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", "Retaliation", OK, 13);
		map7 = new TextButton(20, 375, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", "Domination", OK, 13);
		
		addWidget(map1);
		addWidget(map2);
		addWidget(map3);
		addWidget(map4);
		addWidget(map5);
		addWidget(map6);
		addWidget(map7);
		if (globalContainer->settings.campaignPlace < 7)
			map7->visible = false;
		if (globalContainer->settings.campaignPlace < 6)
			map6->visible = false;
		if (globalContainer->settings.campaignPlace < 5)
			map5->visible = false;
		if (globalContainer->settings.campaignPlace < 4)
			map4->visible = false;
		if (globalContainer->settings.campaignPlace < 3)
			map3->visible = false;
		if (globalContainer->settings.campaignPlace < 2)
			map2->visible = false;
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
		std::string mapFileName = fileList->listToFile(fileList->getText(par1).c_str());
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
			validMapSelected = sessionInfo.load(stream);
			if (validMapSelected)
			{
				// update map name & info
				mapName->setText(sessionInfo.getMapName());
				std::string textTemp;
				textTemp = FormatableString("%0%1").arg(sessionInfo.numberOfTeam).arg(Toolkit::getStringTable()->getString("[teams]"));
				mapInfo->setText(textTemp);
				textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(sessionInfo.versionMajor).arg(sessionInfo.versionMinor);
				mapVersion->setText(textTemp);
				textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
				mapSize->setText(textTemp);
				std::time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
				mapDate->setText(std::ctime(&mtime));
				
				// call subclass handler
				validMapSelectedhandler();
			}
			else
				std::cerr << "ChooseMapScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
		}
		delete stream;
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
			globalContainer->settings.campaignPlayed = 0;
			endExecute(par1);
		}
		else if (source == deleteMap)
		{
			// if a valid file is selected, delete it
			if (fileList->getSelectionIndex() >= 0)
			{
				std::string mapFileName = fileList->listToFile(fileList->get().c_str());
				Toolkit::getFileManager()->remove(mapFileName);
				fileList->generateList();
			}
		}
		else if (source == map1)
		{
			globalContainer->settings.campaignPlayed = 1;
			std::string mapFileName = "campaigns/Joey_mission_1.map";
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
				validMapSelected = sessionInfo.load(stream);
				if (validMapSelected)
				{
					// update map name & info
					mapName->setText(sessionInfo.getMapName());
					std::string textTemp;
					textTemp = FormatableString("%0%1").arg(sessionInfo.numberOfTeam).arg(Toolkit::getStringTable()->getString("[teams]"));
					mapInfo->setText(textTemp);
					textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(sessionInfo.versionMajor).arg(sessionInfo.versionMinor);
					mapVersion->setText(textTemp);
					textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
					mapSize->setText(textTemp);
					std::time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
					mapDate->setText(std::ctime(&mtime));
					
					// call subclass handler
					validMapSelectedhandler();
				}
				else
					std::cerr << "ChooseMapScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
			}
			delete stream;
		}
		else if (source == map2)
		{
			globalContainer->settings.campaignPlayed = 2;
			std::string mapFileName = "campaigns/Joey_mission_2.map";
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
				validMapSelected = sessionInfo.load(stream);
				if (validMapSelected)
				{
					// update map name & info
					mapName->setText(sessionInfo.getMapName());
					std::string textTemp;
					textTemp = FormatableString("%0%1").arg(sessionInfo.numberOfTeam).arg(Toolkit::getStringTable()->getString("[teams]"));
					mapInfo->setText(textTemp);
					textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(sessionInfo.versionMajor).arg(sessionInfo.versionMinor);
					mapVersion->setText(textTemp);
					textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
					mapSize->setText(textTemp);
					std::time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
					mapDate->setText(std::ctime(&mtime));
					
					// call subclass handler
					validMapSelectedhandler();
				}
				else
					std::cerr << "ChooseMapScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
			}
			delete stream;
		}
		else if (source == map3)
		{
			globalContainer->settings.campaignPlayed = 3;
			std::string mapFileName = "campaigns/Joey_mission_3.map";
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
				validMapSelected = sessionInfo.load(stream);
				if (validMapSelected)
				{
					// update map name & info
					mapName->setText(sessionInfo.getMapName());
					std::string textTemp;
					textTemp = FormatableString("%0%1").arg(sessionInfo.numberOfTeam).arg(Toolkit::getStringTable()->getString("[teams]"));
					mapInfo->setText(textTemp);
					textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(sessionInfo.versionMajor).arg(sessionInfo.versionMinor);
					mapVersion->setText(textTemp);
					textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
					mapSize->setText(textTemp);
					std::time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
					mapDate->setText(std::ctime(&mtime));
					
					// call subclass handler
					validMapSelectedhandler();
				}
				else
					std::cerr << "ChooseMapScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
			}
			delete stream;
		}
		else if (source == map4)
		{
			globalContainer->settings.campaignPlayed = 4;
			std::string mapFileName = "campaigns/Joey_mission_4.map";
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
				validMapSelected = sessionInfo.load(stream);
				if (validMapSelected)
				{
					// update map name & info
					mapName->setText(sessionInfo.getMapName());
					std::string textTemp;
					textTemp = FormatableString("%0%1").arg(sessionInfo.numberOfTeam).arg(Toolkit::getStringTable()->getString("[teams]"));
					mapInfo->setText(textTemp);
					textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(sessionInfo.versionMajor).arg(sessionInfo.versionMinor);
					mapVersion->setText(textTemp);
					textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
					mapSize->setText(textTemp);
					std::time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
					mapDate->setText(std::ctime(&mtime));
					
					// call subclass handler
					validMapSelectedhandler();
				}
				else
					std::cerr << "ChooseMapScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
			}
			delete stream;
		}
		else if (source == map5)
		{
			globalContainer->settings.campaignPlayed = 5;
			std::string mapFileName = "campaigns/Joey_mission_5.map";
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
				validMapSelected = sessionInfo.load(stream);
				if (validMapSelected)
				{
					// update map name & info
					mapName->setText(sessionInfo.getMapName());
					std::string textTemp;
					textTemp = FormatableString("%0%1").arg(sessionInfo.numberOfTeam).arg(Toolkit::getStringTable()->getString("[teams]"));
					mapInfo->setText(textTemp);
					textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(sessionInfo.versionMajor).arg(sessionInfo.versionMinor);
					mapVersion->setText(textTemp);
					textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
					mapSize->setText(textTemp);
					std::time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
					mapDate->setText(std::ctime(&mtime));
					
					// call subclass handler
					validMapSelectedhandler();
				}
				else
					std::cerr << "ChooseMapScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
			}
			delete stream;
		}
		else if (source == map6)
		{
			globalContainer->settings.campaignPlayed = 6;
			std::string mapFileName = "campaigns/Joey_mission_6.map";
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
				validMapSelected = sessionInfo.load(stream);
				if (validMapSelected)
				{
					// update map name & info
					mapName->setText(sessionInfo.getMapName());
					std::string textTemp;
					textTemp = FormatableString("%0%1").arg(sessionInfo.numberOfTeam).arg(Toolkit::getStringTable()->getString("[teams]"));
					mapInfo->setText(textTemp);
					textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(sessionInfo.versionMajor).arg(sessionInfo.versionMinor);
					mapVersion->setText(textTemp);
					textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
					mapSize->setText(textTemp);
					std::time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
					mapDate->setText(std::ctime(&mtime));
					
					// call subclass handler
					validMapSelectedhandler();
				}
				else
					std::cerr << "ChooseMapScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
			}
			delete stream;
		}
		else if (source == map7)
		{
			globalContainer->settings.campaignPlayed = 7;
			std::string mapFileName = "campaigns/Joey_mission_7.map";
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
				validMapSelected = sessionInfo.load(stream);
				if (validMapSelected)
				{
					// update map name & info
					mapName->setText(sessionInfo.getMapName());
					std::string textTemp;
					textTemp = FormatableString("%0%1").arg(sessionInfo.numberOfTeam).arg(Toolkit::getStringTable()->getString("[teams]"));
					mapInfo->setText(textTemp);
					textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(sessionInfo.versionMajor).arg(sessionInfo.versionMinor);
					mapVersion->setText(textTemp);
					textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
					mapSize->setText(textTemp);
					std::time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
					mapDate->setText(std::ctime(&mtime));
					
					// call subclass handler
					validMapSelectedhandler();
				}
				else
					std::cerr << "ChooseMapScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
			}
			delete stream;
		}
	}
}
