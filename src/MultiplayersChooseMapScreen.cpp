/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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
#include "GlobalContainer.h"
#include "YOG.h"

MultiplayersChooseMapScreen::MultiplayersChooseMapScreen(bool shareOnYOG)
{
	this->shareOnYOG=shareOnYOG;
	
	ok=new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), OK, 13);
	cancel=new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[cancel]"), CANCEL, 27);
	toogleButton=new TextButton(240, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[the games]"), TOOGLE);
	mapPreview=new MapPreview(240, 60, "net.map");
	title=new Text(20, 18, globalContainer->menuFont, globalContainer->texts.getString("[choose map]"), 600);
	
	addWidget(ok);
	addWidget(cancel);
	addWidget(toogleButton);
	addWidget(mapPreview);
	addWidget(title);

	mapName=new Text(440, 60+128+30, globalContainer->standardFont, "", 180);
	addWidget(mapName);
	mapInfo=new Text(440, 60+128+60, globalContainer->standardFont, "", 180);
	addWidget(mapInfo);
	mapVersion=new Text(440, 60+128+90, globalContainer->standardFont, "", 180);
	addWidget(mapVersion);
	mapSize=new Text(440, 60+128+120, globalContainer->standardFont, "", 180);
	addWidget(mapSize);
	methode=new Text(440, 60+128+150, globalContainer->standardFont, "", 180);
	addWidget(methode);
	
	mapFileList=new List(20, 60, 200, 400, globalContainer->standardFont);
	if (globalContainer->fileManager->initDirectoryListing(".", "map"))
	{
		const char *fileName;
		while ((fileName=globalContainer->fileManager->getNextDirectoryEntry())!=NULL)
		{
			char *newText=Utilities::dencat(fileName, ".map");
			mapFileList->addText(newText);
			delete[] newText;
		}
		mapFileList->sort();
	}
	addWidget(mapFileList);

	gameFileList=new List(20, 60, 200, 400, globalContainer->standardFont);
	if (globalContainer->fileManager->initDirectoryListing(".", "game"))
	{
		const char *fileName;
		while ((fileName=globalContainer->fileManager->getNextDirectoryEntry())!=NULL)
		{
			char *newText=Utilities::dencat(fileName, ".game");
			gameFileList->addText(newText);
			delete[] newText;
		}
		gameFileList->sort();
	}
	addWidget(gameFileList);
	
	mapMode=true;
	mapFileList->visible=mapMode;
	gameFileList->visible=!mapMode;
	validSessionInfo=false;

	globalContainer->gfx->setClipRect();
}

MultiplayersChooseMapScreen::~MultiplayersChooseMapScreen()
{
}

void MultiplayersChooseMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==LIST_ELEMENT_SELECTED)
	{
		char *mapFileName=NULL;
		if (mapMode)
		{
			const char *mapSelectedName=mapFileList->getText(par1);
			mapFileName=Utilities::concat(mapSelectedName, ".map");
			mapPreview->setMapThumbnail(mapFileName);
			printf("PGU : Loading map '%s' ...\n", mapFileName);
		}
		else
		{
			const char *mapSelectedName=gameFileList->getText(par1);
			mapFileName=Utilities::concat(mapSelectedName, ".game");
			mapPreview->setMapThumbnail(mapFileName);
			printf("PGU : Loading game '%s' ...\n", mapFileName);
		}
		
		SDL_RWops *stream=globalContainer->fileManager->open(mapFileName,"rb");
		if (stream==NULL)
			printf("File '%s' not found!\n", mapFileName);
		else
		{
			validSessionInfo=sessionInfo.load(stream);
			SDL_RWclose(stream);
			if (validSessionInfo)
			{
				// update map name & info
				mapName->setText(sessionInfo.getMapName());
				char textTemp[256];
				snprintf(textTemp, 256, "%d%s", sessionInfo.numberOfTeam, globalContainer->texts.getString("[teams]"));
				mapInfo->setText(textTemp);
				snprintf(textTemp, 256, "%s %d.%d", globalContainer->texts.getString("[Version]"), sessionInfo.versionMajor, sessionInfo.versionMinor);
				mapVersion->setText(textTemp);
				snprintf(textTemp, 256, "%d x %d", mapPreview->getLastWidth(), mapPreview->getLastHeight());
				mapSize->setText(textTemp);
				
				methode->setText(mapPreview->getMethode());
				
				if ((!mapMode)&&(sessionInfo.versionMinor<5)&&(sessionInfo.fileIsAMap))
				{
					sessionInfo.fileIsAMap=false;
					printf("PGU : Old game file version: Warning, data has been modiffied because this is a game and not a map...\n");
				}
				//printf("PGU:sessionInfo.fileIsAMap=%d.\n", sessionInfo.fileIsAMap);
			}
			else
				printf("PGU : Warning, Error during map load\n");
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
				printf("PGU : This is not a valid map!\n");
		}
		else if (source==cancel)
			endExecute(par1);
		else if (source==toogleButton)
		{
			mapMode=!mapMode;
			if (mapMode)
			{
				gameFileList->visible=false;
				mapFileList->setVisible(true);
				title->setText(globalContainer->texts.getString("[choose map]"));
				toogleButton->setText(globalContainer->texts.getString("[the games]"));
			}
			else
			{
				mapFileList->visible=false;
				gameFileList->setVisible(true);
				title->setText(globalContainer->texts.getString("[choose game]"));
				toogleButton->setText(globalContainer->texts.getString("[the maps]"));
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

void MultiplayersChooseMapScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
}
