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

#include "CustomGameScreen.h"
#include "GlobalContainer.h"

CustomGameScreen::CustomGameScreen()
{
	ok=new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), OK, 13);
	cancel=new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[Cancel]"), CANCEL, 27);
	fileList=new List(20, 60, 180, 400, globalContainer->standardFont);
	mapPreview=new MapPreview(640-20-26-128, 70, NULL);

	addWidget(new Text(20, 18, globalContainer->menuFont, globalContainer->texts.getString("[choose map]"), 600));
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

	for (int i=0; i<8; i++)
	{
		isAI[i]=new OnOffButton(230, 60+i*30, 25, 25, true, 10+i);
		addWidget(isAI[i]);
		color[i]=new ColorButton(265, 60+i*30, 25, 25, 20+i);
		addWidget(color[i]);
		isAItext[i]=new Text(300, 60+i*30, globalContainer->standardFont, (i==0)  ? globalContainer->texts.getString("[player]") : globalContainer->texts.getString("[ai]"));
		addWidget(isAItext[i]);
	}

	if (globalContainer->fileManager->initDirectoryListing(".", "map"))
	{
		const char *fileName;
		while ((fileName=globalContainer->fileManager->getNextDirectoryEntry())!=NULL)
		{
			int textLength=strlen(fileName)-strlen(".map");
			assert(textLength>0);
			char *newText=new char[textLength+1];
			memcpy(newText, fileName, textLength+1);
			newText[textLength]=0;
			fileList->addText(newText);
			delete[] newText;
		}
		fileList->sort();
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
		int oldTextLength=strlen(mapSelectedName);
		int newTextLength=oldTextLength+strlen(".map");
		char *mapFileName=new char[newTextLength+1];
		memcpy(mapFileName, mapSelectedName, oldTextLength);
		memcpy(&mapFileName[oldTextLength], ".map", strlen(".map")+1);
		
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
				mapName->setText(sessionInfo.getMapName());
				char textTemp[256];
				snprintf(textTemp, 256, "%d%s", sessionInfo.numberOfTeam, globalContainer->texts.getString("[teams]"));
				mapInfo->setText(textTemp);
				snprintf(textTemp, 256, "%s %d.%d", globalContainer->texts.getString("[Version]"), sessionInfo.versionMajor, sessionInfo.versionMinor);
				mapVersion->setText(textTemp);
				snprintf(textTemp, 256, "%d x %d", mapPreview->getLastWidth(), mapPreview->getLastHeight());
				mapSize->setText(textTemp);

				int i;
				int nbTeam=sessionInfo.numberOfTeam;
				// set the correct number of colors
				for (i=0; i<8; i++)
				{
					color[i]->clearColors();
					for (int j=0; j<nbTeam; j++)
					{
						color[i]->addColor(sessionInfo.team[j].colorR, sessionInfo.team[j].colorG, sessionInfo.team[j].colorB);
					}
					color[i]->setSelectedColor();
				}
				// find team for human player
				for (i=0; i<nbTeam; i++)
				{
					if (sessionInfo.team[i].type==BaseTeam::T_HUMAN)
					{
						color[0]->setSelectedColor(i);
						break;
					}
				}
				// Fill the others
				int c=color[0]->getSelectedColor();
				for (i=1; i<nbTeam; i++)
				{
					c=(c+1)%nbTeam;
					color[i]->setSelectedColor(c);
					isAI[i]->setState(true);
					isAItext[i]->setText(globalContainer->texts.getString("[ai]"));
				}
				// Close the rest
				for (;i<8; i++)
				{
					isAI[i]->setState(false);
					isAItext[i]->setText(globalContainer->texts.getString("[closed]"));
				}
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
		if (par1==10)
		{
			isAI[0]->setState(true);
		}
		else if ((par1>10) && (par1<20))
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
