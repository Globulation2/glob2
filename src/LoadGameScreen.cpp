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

#include "LoadGameScreen.h"
#include "GlobalContainer.h"
#include "Utilities.h"

LoadGameScreen::LoadGameScreen()
{
	ok=new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), OK, 13);
	cancel=new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[cancel]"), CANCEL, 27);
	fileList=new List(20, 60, 180, 400, globalContainer->standardFont);
	mapPreview=new MapPreview(640-20-26-128, 70, NULL);

	addWidget(new Text(20, 18, globalContainer->menuFont, globalContainer->texts.getString("[choose game]"), 600));
	mapName=new Text(440, 60+128+30, globalContainer->standardFont, "", 180);
	addWidget(mapName);
	mapInfo=new Text(440, 60+128+60, globalContainer->standardFont, "", 180);
	addWidget(mapInfo);
	mapVersion=new Text(440, 60+128+90, globalContainer->standardFont, "", 180);
	addWidget(mapVersion);
	mapSize=new Text(440, 60+128+120, globalContainer->standardFont, "", 180);
	addWidget(mapSize);

	addWidget(ok);
	addWidget(cancel);
	addWidget(mapPreview);

	if (globalContainer->fileManager->initDirectoryListing(".", "game"))
	{
		const char *fileName;
		while ((fileName=globalContainer->fileManager->getNextDirectoryEntry())!=NULL)
		{
			char *newText=Utilities::dencat(fileName, ".game");
			fileList->addText(newText);
			delete[] newText;
		}
		fileList->sort();
	}
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
		const char *mapSelectedName=fileList->getText(par1);
		char *mapFileName=Utilities::concat(mapSelectedName, ".game");
		
		mapPreview->setMapThumbnail(mapFileName);
		printf("CGS : Loading map '%s' ...\n", mapFileName);
		SDL_RWops *stream=globalContainer->fileManager->open(mapFileName,"rb");
		if (stream==NULL)
			printf("Map '%s' not found!\n", mapFileName);
		else
		{
			validSessionInfo=sessionInfo.load(stream);
			SDL_RWclose(stream);
			if (validSessionInfo)
			{
				// update map name & info
				mapName->setText(sessionInfo.map.getMapName());
				char textTemp[256];
				snprintf(textTemp, 256, "%d%s", sessionInfo.numberOfTeam, globalContainer->texts.getString("[teams]"));
				mapInfo->setText(textTemp);
				snprintf(textTemp, 256, "%s %d.%d", globalContainer->texts.getString("[Version]"), sessionInfo.versionMajor, sessionInfo.versionMinor);
				mapVersion->setText(textTemp);
				snprintf(textTemp, 256, "%d x %d", mapPreview->getLastWidth(), mapPreview->getLastHeight());
				mapSize->setText(textTemp);
			}
			else
				printf("CGS : Warning, Error during map load\n");
		}
		delete[] mapFileName; 
	}
	else if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (source==ok)
		{
			if (validSessionInfo)
				endExecute(OK);
			else
				printf("CGS : This is not a valid map!\n");
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

void LoadGameScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
}
