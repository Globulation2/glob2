/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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
#include "MainMenuScreen.h"
#include "MultiplayersOfferScreen.h"
#include "MapEdit.h"
#include "Engine.h"
#include "GlobalContainer.h"
#include "YOGPreScreen.h"
#include "SettingsScreen.h"
#include "NewMapScreen.h"
#include "MultiplayersHost.h"
#include "MultiplayersChooseMapScreen.h"
#include "Utilities.h"

#include <stdio.h>
#include <sys/types.h>

#ifndef WIN32
#	include <unistd.h>
#	include <sys/time.h>
#endif

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
	printf("Glob2:: starting YOGPreScreen...\n");
	YOGPreScreen yogPreScreen;
	int yogReturnCode=yogPreScreen.execute(globalContainer->gfx, 50);
	if (yogReturnCode==YOGPreScreen::CANCEL)
		return;
	if (yogReturnCode==-1)
	{
		isRunning=false;
		return;
	}
	printf("Glob2::YOGPreScreen has ended ...\n");
	/*YOGScreen yogScreen;
	drawYOGSplashScreen();
	
	// try to open YOG
	if (globalContainer->yog.connect(globalContainer->settings.ircURL, globalContainer->settings.ircPort, globalContainer->settings.userName))
	{
		int yogReturnCode=yogScreen.execute(globalContainer->gfx, 50);
		globalContainer->yog.forceDisconnect();
		printf("Engine::yogReturnCode=%d\n", yogReturnCode);
		if (yogReturnCode==YOGScreen::CANCEL)
			return;
		if (yogReturnCode==-1)
		{
			isRunning=false;
			return;
		}
		printf("Engine::YOG game has ended ...\n");
	}*/
}

int Glob2::runHostServer(int argc, char *argv[])
{
	//TODO: test runHostServer !!! zzz
	
	printf("Glob2::runHostServer():connecting to YOG as %s\n", globalContainer->userName);
	globalContainer->yog->enableConnection(globalContainer->userName);
	
	while(globalContainer->yog->yogGlobalState==YOG::YGS_CONNECTING)
	{
		globalContainer->yog->step();
		SDL_Delay(50);
	}
	if (globalContainer->yog->yogGlobalState<YOG::YGS_CONNECTED)
	{
		printf("Glob2::failed to connect to YOG!.\n");
		return 0;
	}
	
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
		bool validSessionInfo=sessionInfo.load(stream);
		SDL_RWclose(stream);
		if (!validSessionInfo)
		{
			printf("Glob2::runHostServer():Warning, Error during map load.\n");
			return 0;
		}
	}
	
	printf("Glob2::runHostServer():sharing the game...\n");
	MultiplayersHost *multiplayersHost=new MultiplayersHost(&sessionInfo, true, NULL);
	globalContainer->yog->shareGame(mapName);
	
	Uint32 frameStartTime;
	Sint32 frameWaitTime;
	Sint32 stepLength=50;
	
	bool running=true;
	char s[32];
	while (running)
	{
		// get first timer
		frameStartTime=SDL_GetTicks();
		
		multiplayersHost->onTimer(frameStartTime);
		
		fd_set rfds;
		struct timeval tv;
		int retval;
		
		// Watch stdin (fd 0) to see when it has input.
		FD_ZERO(&rfds);
		FD_SET(0, &rfds);
		// Wait up to one second.
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		
		retval = select(1, &rfds, NULL, NULL, &tv);
		// Don't rely on the value of tv now!
		
		if (retval)
		{
			fgets(s, 32, stdin);
			int l=strlen(s);
			if ((l>1)&&(s[l-1]=='\n'))
				s[l-1]=0;
			
			if (strncmp(s, "start", 5)==0)
			{
				multiplayersHost->startGame();
			}
			else if ((strncmp(s, "quit", 4)==0) || (strncmp(s, "exit", 4)==0) || (strncmp(s, "bye", 4)==0))
			{
				multiplayersHost->stopHosting();
				running=false;
			}
			else
				printf("Glob2::runHostServer():not understood (%s).\n", s);
		}
		
		if (multiplayersHost->hostGlobalState>=MultiplayersHost::HGS_PLAYING_COUNTER)
		{
			printf("Glob2::runHostServer():state high enough.\n");
			running=false;
		}
		
		frameWaitTime=SDL_GetTicks()-frameStartTime;
		frameWaitTime=stepLength-frameWaitTime;
		if (frameWaitTime>0)
			SDL_Delay(frameWaitTime);
	}
	
	globalContainer->yog->unshareGame();
	
	
	delete multiplayersHost;
	
	printf("Glob2::runHostServer(): disconnecting YOG.\n");
	
	globalContainer->yog->deconnect();
	while(globalContainer->yog->yogGlobalState==YOG::YGS_DECONNECTING)
	{
		globalContainer->yog->step();
		SDL_Delay(50);
	}
	
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
			case MainMenuScreen::CAMPAIN:
			{
				Engine engine;
				if (engine.initCampain()==Engine::EE_NO_ERROR)
					if (engine.run()==-1)
						isRunning=false;
			}
			break;
			case MainMenuScreen::LOAD_GAME:
			{
				Engine engine;
				if (engine.initLoadGame()==Engine::EE_NO_ERROR)
					if (engine.run()==-1)
						isRunning=false;
			}
			break;
			case MainMenuScreen::CUSTOM:
			{
				Engine engine;
				if (engine.initCustom()==Engine::EE_NO_ERROR)
					if (engine.run()==-1)
						isRunning=false;
			}
			break;
			case MainMenuScreen::MULTIPLAYERS_YOG:
			{
				mutiplayerYOG();
			}
			break;
			case MainMenuScreen::MULTIPLAYERS_LAN:
			{
				switch (MultiplayersOfferScreen::menu())
				{
					case MultiplayersOfferScreen::HOST:
					{
						Engine engine;
						int rc=engine.initMutiplayerHost(false);
						if (rc==Engine::EE_NO_ERROR)
						{
							if (engine.run()==-1)
								isRunning=false;
						}
						else if (rc==-1)
							isRunning=false;
					}
					break;

					case MultiplayersOfferScreen::JOIN:
					{
						Engine engine;
						int rc=engine.initMutiplayerJoin();
						if (rc==Engine::EE_NO_ERROR)
						{
							if (engine.run()==-1)
								isRunning=false;
						}
						else if (rc==-1)
							isRunning=false;
					}
					break;
					case MultiplayersOfferScreen::QUIT:
					{
						//continue;
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
			case MainMenuScreen::GAME_SETUP:
			{
				SettingsScreen settingsScreen;
				settingsScreen.execute(globalContainer->gfx, 50);
			}
			break;
			case MainMenuScreen::EDITOR:
			{
				HowNewMapScreen howNewMapScreen;
				int rc=howNewMapScreen.execute(globalContainer->gfx, 50);
				if (rc==HowNewMapScreen::NEW)
				{
					NewMapScreen newMapScreen;
					if (newMapScreen.execute(globalContainer->gfx, 20)==NewMapScreen::OK)
					{
						MapEdit mapEdit;
						//mapEdit.resize(newMapScreen.sizeX, newMapScreen.sizeY);
						setRandomSyncRandSeed();
						mapEdit.game.generateMap(newMapScreen.descriptor);
						if (mapEdit.run()==-1)
							isRunning=false;
					}
				}
				else if (rc==HowNewMapScreen::LOAD)
				{
					MultiplayersChooseMapScreen multiplayersChooseMapScreen;
					int rc=multiplayersChooseMapScreen.execute(globalContainer->gfx, 50);
					if (rc==MultiplayersChooseMapScreen::OK)
					{
						MapEdit mapEdit;
						mapEdit.load(multiplayersChooseMapScreen.sessionInfo.getFileName());
						if (mapEdit.run()==-1)
							isRunning=false;
					}
					else if (rc==-1)
						isRunning=false;
				}
				else if (rc==HowNewMapScreen::CANCEL)
				{
					// Let's sing.
				}
				else
					assert(false);
			}
			break;
			case MainMenuScreen::QUIT:
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


