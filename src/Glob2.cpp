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

#include "Glob2.h"
#include "GAG.h"
#include "Game.h"
#include "PreparationGui.h"
#include "MapEdit.h"
#include "Engine.h"
#include "GlobalContainer.h"
#include "YOGScreen.h"
#include "SettingsScreen.h"
#include "NewMapScreen.h"
#include <SDL_net.h>
#include "MultiplayersHost.h"

GlobalContainer *globalContainer=0;


void Glob2::drawYOGSplashScreen(void)
{
	int w, h;
	w=globalContainer->gfx->getW();
	h=globalContainer->gfx->getH();
	globalContainer->gfx->drawFilledRect(0, 0, w, h, 0, 0, 0);
	char *text[3];
	text[0]=globalContainer->texts.getString("[connecting to]");
	text[1]=globalContainer->texts.getString("[yog]");
	text[2]=globalContainer->texts.getString("[please wait]");
	for (int i=0; i<3; ++i)
	{
		int size=globalContainer->menuFont->getStringWidth(text[i]);
		int dec=(w-size)>>1;
		globalContainer->gfx->drawString(dec, 150+i*50, globalContainer->menuFont, text[i]);
	}
	globalContainer->gfx->updateRect(0, 0, w, h);
}

void Glob2::mutiplayerYOG(void)
{
	YOGScreen yogScreen;
	drawYOGSplashScreen();
	yogScreen.createConnection();
	if (yogScreen.socket!=NULL)
	{
		int yogReturnCode=yogScreen.execute(globalContainer->gfx, 20);
		yogScreen.closeConnection();
		printf("Engine::yogReturnCode=%d\n", yogReturnCode);
		if (yogReturnCode==YOGScreen::CANCEL)
			return;
		if (yogReturnCode==-1)
		{
			isRunning=false;
			return;
		}
		printf("Engine::YOG game has ended ...\n");
	}
}

int Glob2::runHostServer(int argc, char *argv[])
{
	bool validSessionInfo;
	SessionInfo sessionInfo;
	
	char *mapName=globalContainer->hostServerMapName;
	
	printf("Glob2::runHostServer():Loading map '%s' ...\n", mapName);
	SDL_RWops *stream=globalContainer->fileManager.open(mapName,"rb");
	if (stream==NULL)
	{
		printf("Map '%s' not found!\n", mapName);
		return 0;
	}
	else
	{
		validSessionInfo=sessionInfo.load(stream);
		SDL_RWclose(stream);
		if (validSessionInfo)
		{
			sessionInfo.map.mapName[31]=0;
		}
		else
		{
			printf("Glob2::runHostServer():Warning, Error during map load.\n");
			return 0;
		}
	}
	
	MultiplayersHost *multiplayersHost=new MultiplayersHost(&sessionInfo, true);
	
	Uint32 frameStartTime;
	Sint32 frameWaitTime;
	Sint32 stepLength=20;
	
	bool running=true;
	while (running)
	{
		// get first timer
		frameStartTime=SDL_GetTicks();
		
		multiplayersHost->onTimer(frameStartTime);
		
		frameWaitTime=SDL_GetTicks()-frameStartTime;
		frameWaitTime=stepLength-frameWaitTime;
		if (frameWaitTime>0)
			SDL_Delay(frameWaitTime);
	}
	/*multiplayersHost->onTimer(tick);
	multiplayersHost->startGame();
	multiplayersHost->stopHosting();*/
	
	multiplayersHost->startGame();
	
	delete multiplayersHost;
		
	printf("Glob2::runHostServer():end.\n");

	return 0;
}

int Glob2::run(int argc, char *argv[])
{
	globalContainer = new GlobalContainer();

	globalContainer->parseArgs(argc, argv);
	globalContainer->load();
	if (!globalContainer->hostServer)
		globalContainer->gfx->setCaption("Globulation 2", "glob 2");

	if ( SDLNet_Init() < 0 )
	{
		fprintf(stderr, "Couldn't initialize net: %s\n", SDLNet_GetError());
		exit(1);
	}
	atexit(SDLNet_Quit);
	
	if (globalContainer->hostServer)
	{
		return runHostServer(argc, argv);
	}

	isRunning=true;
	while (isRunning)
	{
		switch (MainMenuScreen::menu())
		{
			case -1:
			{
				isRunning=false;
			}
			break;
			case 0:
			{
				Engine engine;
				if (engine.initCampain()==Engine::NO_ERROR)
					if (engine.run()==-1)
						isRunning=false;
			}
			break;
			case 1:
			{
				Engine engine;
				if (engine.initCustom()==Engine::NO_ERROR)
					if (engine.run()==-1)
						isRunning=false;
			}
			break;
			case 2:
			{
				mutiplayerYOG();
			}
			break;
			case 3:
			{
				switch (MultiplayersOfferScreen::menu())
				{
					case MultiplayersOfferScreen::HOST :
					{
						Engine engine;
						int rc=engine.initMutiplayerHost(false);
						if (rc==Engine::NO_ERROR)
						{
							if (engine.run()==-1)
								isRunning=false;
						}
						else if (rc==-1)
							isRunning=false;
					}
					break;

					case MultiplayersOfferScreen::JOIN :
					{
						Engine engine;
						printf("join\n");
						int rc=engine.initMutiplayerJoin();
						if (rc==Engine::NO_ERROR)
						{
							if (engine.run()==-1)
								isRunning=false;
						}
						else if (rc==-1)
							isRunning=false;
					}
					break;

					case MultiplayersOfferScreen::QUIT :
					{

					}
					break;

					case -1 :
					{
						isRunning=false;
					}
					break;
				}
			}
			break;
			case 4:
			{
				int settingReturnValue;
				do
				{
					settingReturnValue=SettingsScreen::menu();
				}
				while (settingReturnValue);
			}
			break;
			case 5:
			{
				NewMapScreen newMapScreen;
				newMapScreen.execute(globalContainer->gfx, 30);
				MapEdit mapEdit;
				if (mapEdit.run(newMapScreen.sizeX, newMapScreen.sizeY, newMapScreen.defaultTerrainType))
					isRunning=false;
			}
			break;
			case 6:
			{
				isRunning=false;
			}
			break;
			default:
			break;
		}
	}

	delete globalContainer;
	return 0;
}

int main(int argc, char *argv[])
{
	Glob2 glob2;
	return glob2.run(argc, argv);
}


