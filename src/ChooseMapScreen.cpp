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
#include <GUIButton.h>
#include <GUIText.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>

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
	
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose map]")));
	mapName=new Text(440, 60+128+30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapName);
	mapInfo=new Text(440, 60+128+60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapInfo);
	mapVersion=new Text(440, 60+128+90, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapVersion);
	mapSize=new Text(440, 60+128+120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapSize);
	
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
		GAGCore::InputStream *stream = Toolkit::getFileManager()->openInputStream(mapFileName);
		if (stream == NULL)
		{
			std::cerr << "ChooseMapScreen::onAction : can't open file " << mapFileName << std::endl;
		}
		else
		{
			std::cout << "ChooseMapScreen::onAction : loading map " << mapFileName << std::endl;
			validMapSelected = sessionInfo.load(stream);
			delete stream;
			if (validMapSelected)
			{
				// update map name & info
				mapName->setText(sessionInfo.getMapName());
				char textTemp[256];
				snprintf(textTemp, 256, "%d%s", sessionInfo.numberOfTeam, Toolkit::getStringTable()->getString("[teams]"));
				mapInfo->setText(textTemp);
				snprintf(textTemp, 256, "%s %d.%d", Toolkit::getStringTable()->getString("[Version]"), sessionInfo.versionMajor, sessionInfo.versionMinor);
				mapVersion->setText(textTemp);
				snprintf(textTemp, 256, "%d x %d", mapPreview->getLastWidth(), mapPreview->getLastHeight());
				mapSize->setText(textTemp);
				
				// call subclass handler
				validMapSelectedhandler();
			}
			else
				std::cerr << "ChooseMapScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
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
	}
}
