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

#include "LoadGameScreen.h"
#include "Utilities.h"
#include "Game.h"
#include "GUIMapPreview.h"
#include <GUIButton.h>
#include <GUIText.h>
#include <GUIList.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>
#include "GUIGlob2FileList.h"

LoadGameScreen::LoadGameScreen()
{
	ok=new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	cancel=new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, NULL, -1, -1, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
	fileList=new Glob2FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "games", "game", true);
	mapPreview=new MapPreview(640-20-26-128, 70, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);

	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose game]")));
	mapName=new Text(440, 60+128+30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapName);
	mapInfo=new Text(440, 60+128+60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapInfo);
	mapVersion=new Text(440, 60+128+90, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapVersion);
	mapSize=new Text(440, 60+128+120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapSize);

	addWidget(ok);
	addWidget(cancel);
	addWidget(mapPreview);

	addWidget(fileList);

	validSessionInfo=false;
}

LoadGameScreen::~LoadGameScreen()
{
}

void LoadGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==LIST_ELEMENT_SELECTED)
	{
		std::string mapFileName = glob2NameToFilename("games", fileList->getText(par1).c_str(), "game");

		mapPreview->setMapThumbnail(mapFileName.c_str());
		
		GAGCore::InputStream *stream = Toolkit::getFileManager()->openInputStream(mapFileName);
		if (stream == NULL)
		{
			std::cerr << "LoadGameScreen::onAction : can't open file " << mapFileName << std::endl;
		}
		else
		{
			std::cout << "LoadGameScreen::onAction : loading map " << mapFileName << std::endl;
			validSessionInfo = sessionInfo.load(stream);
			delete stream;
			if (validSessionInfo)
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
			}
			else
				std::cerr << "LoadGameScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
		}
	}
	else if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (source==ok)
		{
			if (validSessionInfo)
				endExecute(OK);
			else
				std::cerr << "LoadGameScreen::onAction : No valid map is selected" << std::endl;
		}
		else if (source==cancel)
		{
			endExecute(par1);
		}
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
	}
}
