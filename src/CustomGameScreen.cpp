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

#include "CustomGameScreen.h"
#include "GlobalContainer.h"

CustomGameScreen::CustomGameScreen()
{
	int i;

	ok=new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), OK);
	cancel=new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[cancel]"), CANCEL);
	fileList=new List(20, 60, 180, 400, globalContainer->standardFont);
	mapPreview=new MapPreview(640-20-26-128, 70, "net.map");

	addWidget(new Text(20, 18, globalContainer->menuFont, globalContainer->texts.getString("[choose map]"), 600));
	mapName=new Text(440, 60+128+30, globalContainer->standardFont, "", 180);
	addWidget(mapName);
	mapInfo=new Text(440, 60+128+60, globalContainer->standardFont, "", 180);
	addWidget(mapInfo);

	addWidget(ok);
	addWidget(cancel);
	addWidget(mapPreview);

	for (i=0; i<8; i++)
	{
		isAI[i]=new OnOffButton(230, 60+i*30, 25, 25, true, 10+i);
		addWidget(isAI[i]);
		color[i]=new ColorButton(265, 60+i*30, 25, 25, 20+i);
		addWidget(color[i]);
		isAItext[i]=new Text(300, 60+i*30, globalContainer->standardFont, (i==0)  ? globalContainer->texts.getString("[player]") : globalContainer->texts.getString("[ai]"));
		addWidget(isAItext[i]);
	}

	if (globalContainer->fileManager.initDirectoryListing(".", "map"))
	{
		const char *file;
		while ((file=globalContainer->fileManager.getNextDirectoryEntry())!=NULL)
			fileList->addText(file);
	}
	addWidget(fileList);

	validSessionInfo=false;
}

CustomGameScreen::~CustomGameScreen()
{
}

void CustomGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==LIST_ELEMENT_SELECTED)
	{
		const char *mapSelectedName=fileList->getText(par1);
		mapPreview->setMapThumbnail(mapSelectedName);
		printf("CGS : Loading map '%s' ...\n", mapSelectedName);
		SDL_RWops *stream=globalContainer->fileManager.open(mapSelectedName,"rb");
		if (stream==NULL)
			printf("Map '%s' not found!\n", mapSelectedName);
		else
		{
			validSessionInfo=sessionInfo.load(stream);
			SDL_RWclose(stream);
			if (validSessionInfo)
			{
				// update map name & info
				sessionInfo.map.mapName[31]=0;
				mapName->setText(sessionInfo.map.mapName);
				char textTemp[256];
				snprintf(textTemp, 256, "%d%s", sessionInfo.numberOfTeam, globalContainer->texts.getString("[teams]"));
				mapInfo->setText(textTemp);

				int i, j;
				for (i=0; i<8; i++)
				{
					for (j=0; j<sessionInfo.numberOfTeam; j++)
					{
						color[i]->addColor(sessionInfo.team[j].colorR, sessionInfo.team[j].colorG, sessionInfo.team[j].colorB);
					}
					color[i]->setSelectedColor();
				}
			}
			else
				printf("CGS : Warning, Error during map load\n");
		}
	}
	else if (action==BUTTON_RELEASED)
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
		else if (par1==10)
		{
			isAI[0]->setState(true);
		}
		else if (par1>10)
		{
			int n=par1-10;
			if (isAI[n]->getState())
				isAItext[n]->setText(globalContainer->texts.getString("[ai]"));
			else
				isAItext[n]->setText(globalContainer->texts.getString("[closed]"));
		}
	}
}

void CustomGameScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
}
