/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "MultiplayersChooseMapScreen.h"

MultiplayersChooseMapScreen::MultiplayersChooseMapScreen()
{
	ok=new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), OK, 13);
	cancel=new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[cancel]"), CANCEL, 27);
	fileList=new List(20, 60, 200, 400, globalContainer->standardFont);
	mapPreview=new MapPreview(240, 60, "net.map");

	addWidget(new Text(20, 18, globalContainer->menuFont, globalContainer->texts.getString("[choose map]"), 600));

	addWidget(ok);
	addWidget(cancel);
	addWidget(mapPreview);

	mapName=new Text(440, 60+128+30, globalContainer->standardFont, "", 180);
	addWidget(mapName);
	mapInfo=new Text(440, 60+128+60, globalContainer->standardFont, "", 180);
	addWidget(mapInfo);
	mapVersion=new Text(440, 60+128+90, globalContainer->standardFont, "", 180);
	addWidget(mapVersion);
	mapSize=new Text(440, 60+128+120, globalContainer->standardFont, "", 180);
	addWidget(mapSize);

	if (globalContainer->fileManager.initDirectoryListing(".", "map"))
	{
		const char *file;
		while ((file=globalContainer->fileManager.getNextDirectoryEntry())!=NULL)
			fileList->addText(file);
	}
	addWidget(fileList);

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
		const char *mapFileName=fileList->getText(par1);
		mapPreview->setMapThumbnail(mapFileName);
		printf("PGU : Loading map '%s' ...\n", mapFileName);
		SDL_RWops *stream=globalContainer->fileManager.open(mapFileName,"rb");
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
				printf("PGU : Warning, Error during map load\n");
		}
	}
	else if (action==BUTTON_RELEASED)
	{
		if (source==ok)
		{
			if (validSessionInfo)
				endExecute(OK);
			else
				printf("PGU : This is not a valid map!\n");
		}
		else
			endExecute(par1);
	}
}

void MultiplayersChooseMapScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
}








