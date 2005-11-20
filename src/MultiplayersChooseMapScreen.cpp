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

#include "MultiplayersChooseMapScreen.h"
#include "Utilities.h"
#include "YOG.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "GUIMapPreview.h"
#include <GUIButton.h>
#include <GUIText.h>
#include <GUIList.h>
#include <Toolkit.h>
#include <StringTable.h>
#include "GUIGlob2FileList.h"

MultiplayersChooseMapScreen::MultiplayersChooseMapScreen(bool shareOnYOG)
{
	this->shareOnYOG=shareOnYOG;

	ok=new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	cancel=new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
	toogleButton=new TextButton(240, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[the games]"), TOOGLE);
	mapPreview=new MapPreview(640-20-26-128, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	title=new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose map]"));

	addWidget(ok);
	addWidget(cancel);
	addWidget(toogleButton);
	addWidget(mapPreview);
	addWidget(title);

	mapName=new Text(440, 60+128+30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapName);
	mapInfo=new Text(440, 60+128+60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapInfo);
	mapVersion=new Text(440, 60+128+90, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapVersion);
	mapSize=new Text(440, 60+128+120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapSize);
	methode=new Text(440, 60+128+150, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(methode);


	mapFileList=new Glob2FileList(20, 60, 200, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "maps", "map", true);
	addWidget(mapFileList);

	gameFileList=new Glob2FileList(20, 60, 200, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "games", "game", true);
	addWidget(gameFileList);
	
	mapMode=true;
	mapFileList->visible=mapMode;
	gameFileList->visible=!mapMode;
	validSessionInfo=false;
}

MultiplayersChooseMapScreen::~MultiplayersChooseMapScreen()
{
}

void MultiplayersChooseMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==LIST_ELEMENT_SELECTED)
	{
		const char *mapSelectedName=static_cast<Glob2FileList *>(source)->getText(par1).c_str();
		std::string mapFileName;
		if (mapMode)
		{
			mapFileName = glob2NameToFilename("maps", mapSelectedName, "map");
			if (verbose)
				std::cout << "MultiplayersChooseMapScreen::onAction : loading map " << mapFileName << std::endl;
		}
		else
		{
			mapFileName = glob2NameToFilename("games", mapSelectedName, "game");
			if (verbose)
				std::cout << "MultiplayersChooseMapScreen::onAction : loading game " << mapFileName << std::endl;
		}
		mapPreview->setMapThumbnail(mapFileName.c_str());

		InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapFileName));
		if (stream->isEndOfStream())
		{
			std::cerr << "MultiplayersChooseMapScreen::onAction() : error, can't open file " << mapFileName<< std::endl;
		}
		else
		{
			if (verbose)
				std::cout << "MultiplayersChooseMapScreen::onAction : loading map " << mapFileName << std::endl;
			validSessionInfo = sessionInfo.load(stream);
			
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
				
				methode->setText(mapPreview->getMethode());
			}
			else
				std::cerr << "MultiplayersChooseMapScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
		}
		delete stream;
	}
	else if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (source==ok)
		{
			if (validSessionInfo)
				endExecute(OK);
			else
				std::cerr << "MultiplayersChooseMapScreen::onAction : No valid game selected" << std::endl;
		}
		else if (source==cancel)
			endExecute(par1);
		else if (source==toogleButton)
		{
			mapMode=!mapMode;
			if (mapMode)
			{
				gameFileList->hide();
				mapFileList->show();
				title->setText(Toolkit::getStringTable()->getString("[choose map]"));
				toogleButton->setText(Toolkit::getStringTable()->getString("[the games]"));
			}
			else
			{
				mapFileList->hide();
				gameFileList->show();
				title->setText(Toolkit::getStringTable()->getString("[choose game]"));
				toogleButton->setText(Toolkit::getStringTable()->getString("[the maps]"));
			}
		}
		else
			assert(false);
	}
}

void MultiplayersChooseMapScreen::onTimer(Uint32 tick)
{
	if (shareOnYOG)
		yog->step();
}
