/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/


#include "CampaignEditor.h"
#include "CampaignMenuScreen.h"
#include "CampaignSelectorScreen.h"
#include "ChooseMapScreen.h"
#include "CreditScreen.h"
#include "Engine.h"
#include "Game.h"
#include "Glob2.h"
#include "GlobalContainer.h"
#include "GUIMessageBox.h"
#include "Header.h"
#include "LANFindScreen.h"
#include "LANMenuScreen.h"
#include "MainMenuScreen.h"
#include "MapEdit.h"
#include "NetBroadcastListener.h"
#include "NewMapScreen.h"
#include "SettingsScreen.h"
#include <StringTable.h>
#include "Utilities.h"
#include "YOGClient.h"
#include "YOGGameServer.h"
#include "YOGLoginScreen.h"


#include <Stream.h>
#include <BinaryStream.h>

#include <stdio.h>
#include <sys/types.h>

#ifndef WIN32
#	include <unistd.h>
#	include <sys/time.h>
#else
#	include <time.h>
#endif

#ifdef __APPLE__
#	include <Carbon/Carbon.h>
#	include <sys/param.h>
#endif

/*!	\mainpage Globulation 2 Reference documentation

	\section intro Introduction
	This is the documentation of Globulation 2 free
	software game. It covers Glob2 itself and
	libgag (graphic and widget).
	\section feedback Feedback
	This documentation is not yet complete, but should help to have an
	overview of Globulation's 2 code. If you have any comments or suggestions,
	do not hesitate to contact the development team at
	http://www.globulation2.org
*/

GlobalContainer *globalContainer=NULL;


void Glob2::drawYOGSplashScreen(void)
{
	int w, h;
	w=globalContainer->gfx->getW();
	h=globalContainer->gfx->getH();
	globalContainer->gfx->drawFilledRect(0, 0, w, h, 0, 0, 0);
	const char *text[3];
	text[0]=Toolkit::getStringTable()->getString("[connecting to]");
	text[1]=Toolkit::getStringTable()->getString("[yog]");
	text[2]=Toolkit::getStringTable()->getString("[please wait]");
	for (int i=0; i<3; ++i)
	{
		int size=globalContainer->menuFont->getStringWidth(text[i]);
		int dec=(w-size)>>1;
		globalContainer->gfx->drawString(dec, 150+i*50, globalContainer->menuFont, text[i]);
	}
	globalContainer->gfx->nextFrame();
}

void Glob2::mutiplayerYOG(void)
{
	if (verbose)
		printf("Glob2:: starting YOGLoginScreen...\n");
	shared_ptr<YOGClient> client(new YOGClient);
	YOGLoginScreen yogLoginScreen(client);
	int yogReturnCode=yogLoginScreen.execute(globalContainer->gfx, 40);
	if (yogReturnCode==YOGLoginScreen::Cancelled)
		return;
	if (yogReturnCode==-1)
	{
		isRunning=false;
		return;
	}
	if (verbose)
		printf("Glob2::YOGLoginScreen has ended ...\n");
}

int Glob2::runNoX()
{
	printf("nox::running %d times %d steps:\n", globalContainer->runNoXCountRuns, globalContainer->runNoXCountSteps);
	for (int runNoXCount = 0; runNoXCount < globalContainer->runNoXCountRuns; runNoXCount++)
	{
		Engine engine;
		if (engine.initCustom(globalContainer->runNoXGameName) != Engine::EE_NO_ERROR)
			return 1;
		engine.run();
	}
	return 0;
}

int Glob2::runHostServer()
{
/*
	if (verbose)
		std::cout << "Glob2::runHostServer():connecting to YOG as %s" << globalContainer->getUsername() << std::endl;
	yog->enableConnection(globalContainer->hostServerUserName, globalContainer->hostServerPassWord, false);
	
	while(yog->yogGlobalState==YOG::YGS_CONNECTING)
	{
		yog->step();
		SDL_Delay(40);
	}
	if (yog->yogGlobalState<YOG::YGS_CONNECTED)
	{
		printf("Glob2::failed to connect to YOG!.\n");
		return 1;
	}
	
	SessionInfo sessionInfo;
	
	char *mapName=globalContainer->hostServerMapName;
	
	if (verbose)
		printf("Glob2::runHostServer():Loading map '%s' ...\n", mapName);
	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapName));
	if (stream->isEndOfStream())
	{
		std::cerr << "Glob2::runHostServer() : error, can't open map " << mapName << std::endl;
		delete stream;
		return 1;
	}
	else
	{
		bool validSessionInfo = sessionInfo.load(stream);
		delete stream;
		if (!validSessionInfo)
		{
			printf("Glob2::runHostServer():Warning, Error during map load.\n");
			return 1;
		}
	}

	printf("Glob2::runHostServer():sharing the game...\n");
	MultiplayersHost *multiplayersHost=new MultiplayersHost(&sessionInfo, true, NULL);
	// TODO : let the user choose the name of the shared game
	yog->shareGame(sessionInfo.getMapNameC());
	
	Uint32 frameStartTime;
	Sint32 frameWaitTime;
	Sint32 stepLength=50;
	
	bool running=true;
	char s[32];
	while (running)
	{
		// get first timer
		frameStartTime=SDL_GetTicks();
		
		multiplayersHost->onTimer(frameStartTime, NULL);
		
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
			size_t l=strlen(s);
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

	yog->unshareGame();

	delete multiplayersHost;

	if (verbose)
		printf("Glob2::runHostServer(): disconnecting YOG.\n");

	yog->deconnect();
	while(yog->yogGlobalState==YOG::YGS_DECONNECTING)
	{
		yog->step();
		SDL_Delay(50);
	}

	if (verbose)
		printf("Glob2::runHostServer():end.\n");

	return 0;
	*/
}

int Glob2::run(int argc, char *argv[])
{
	srand(time(NULL));
	
	globalContainer=new GlobalContainer();
	globalContainer->parseArgs(argc, argv);
	globalContainer->load();

	if ( SDLNet_Init() < 0 )
	{
		fprintf(stderr, "Couldn't initialize net: %s\n", SDLNet_GetError());
		exit(1);
	}
	atexit(SDLNet_Quit);

/*
	yog=new YOG();
	
	// TODO : this structure is ugly, do we have to keep hostServer ?
	if (globalContainer->hostServer)
	{
		int ret=runHostServer();
		delete yog;
		delete globalContainer;
		return ret;
	}
	*/
	
	if (globalContainer->hostServer)
	{
		YOGGameServer server(YOGRequirePassword, YOGMultipleGames);
		int rc = server.run();
		return rc;	
	}
	
	
	if (globalContainer->runNoX)
	{
		int ret=runNoX();
		delete globalContainer;
		return ret;
	}

	isRunning=true;
	while (isRunning)
	{
		switch (MainMenuScreen::menu())
		{
			case -1:
			{
				isRunning = false;
			}
			break;
			case MainMenuScreen::CAMPAIGN:
			{
				CampaignChoiceScreen ccs;
				int rccs=ccs.execute(globalContainer->gfx, 40);
				if(rccs==CampaignChoiceScreen::NEWCAMPAIGN)
				{
					CampaignSelectorScreen css;
					int rc_css=css.execute(globalContainer->gfx, 40);
					if(rc_css==CampaignSelectorScreen::OK)
					{
						CampaignMenuScreen cms(css.getCampaignName());
						cms.setNewCampaign();
						int rc_cms=cms.execute(globalContainer->gfx, 40);
						if(rc_cms==CampaignMenuScreen::EXIT)
						{
						}
						else if(rc_cms == -1)
						{
							isRunning = false;
						}
					}
					else if(rc_css==CampaignSelectorScreen::CANCEL)
					{
					}
					else if(rc_css == -1)
					{
						isRunning = false;
					}
				}
				else if(rccs==CampaignChoiceScreen::LOADCAMPAIGN)
				{
					CampaignSelectorScreen css(true);
					int rc_css=css.execute(globalContainer->gfx, 40);
					if(rc_css==CampaignSelectorScreen::OK)
					{
						CampaignMenuScreen cms(css.getCampaignName());
						int rc_cms=cms.execute(globalContainer->gfx, 40);
						if(rc_cms==CampaignMenuScreen::EXIT)
						{
						}
						else if(rc_cms == -1)
						{
							isRunning = false;
						}
					}
					else if(rc_css==CampaignSelectorScreen::CANCEL)
					{
					}
					else if(rc_css == -1)
					{
						isRunning = false;
					}
				}
				else if(rccs == -1)
				{
					isRunning = false;
				}
			}
			break;
			case MainMenuScreen::TUTORIAL:
			{
				Campaign campaign;
				if(campaign.load("games/tutorial.txt"))
				{
					CampaignMenuScreen cms("games/Tutorial_Campaign.txt");
					int rc_cms=cms.execute(globalContainer->gfx, 40);
					if(rc_cms==CampaignMenuScreen::EXIT)
					{
					}
					else if(rc_cms == -1)
					{
						isRunning = false;
					}
				}
				else
				{
					CampaignMenuScreen cms("campaigns/Tutorial_Campaign.txt");
					int rc_cms=cms.execute(globalContainer->gfx, 40);
					if(rc_cms==CampaignMenuScreen::EXIT)
					{
					}
					else if(rc_cms == -1)
					{
						isRunning = false;
					}
				}
				//Engine engine;
				//if (engine.initCampaign("maps/tutorial.map") == Engine::EE_NO_ERROR)
					//isRunning = (engine.run() != -1);
			}
			break;
			case MainMenuScreen::LOAD_GAME:
			{
				Engine engine;
				if (engine.initLoadGame() == Engine::EE_NO_ERROR)
					isRunning = (engine.run() != -1);
			}
			break;
			case MainMenuScreen::CUSTOM:
			{
				Engine engine;
				if (engine.initCustom() ==  Engine::EE_NO_ERROR)
					isRunning = (engine.run() != -1);
			}
			break;
			case MainMenuScreen::MULTIPLAYERS_YOG:
			{
				mutiplayerYOG();
			}
			break;
			case MainMenuScreen::MULTIPLAYERS_LAN:
			{
				LANMenuScreen lanms;
				int rc = lanms.execute(globalContainer->gfx, 40);
				if(rc == -1)
					isRunning=false;
			}
			break;
			case MainMenuScreen::GAME_SETUP:
			{
				SettingsScreen settingsScreen;
				int rc_ss = settingsScreen.execute(globalContainer->gfx, 40);
				if( rc_ss == -1)
					isRunning=false;
			}
			break;
			case MainMenuScreen::EDITOR:
			{
				HowNewMapScreen howNewMapScreen;
				int rc=howNewMapScreen.execute(globalContainer->gfx, 40);
				if (rc==HowNewMapScreen::NEWMAP)
				{
					bool retryNewMapScreen=true;
					while (retryNewMapScreen)
					{
						NewMapScreen newMapScreen;
						int rc_nms = newMapScreen.execute(globalContainer->gfx, 40);
						if (rc_nms==NewMapScreen::OK)
						{
							MapEdit mapEdit;
							//mapEdit.resize(newMapScreen.sizeX, newMapScreen.sizeY);
							setRandomSyncRandSeed();
							if (mapEdit.game.generateMap(newMapScreen.descriptor))
							{
								mapEdit.mapHasBeenModiffied(); // make all map as modified by default
								if (mapEdit.run()==-1)
									isRunning=false;
								retryNewMapScreen=false;
							}
							else
							{
								//TODO: popup a widow to explain that the generateMap() has failed.
								retryNewMapScreen=true;
							}
						}
						else if(rc_nms == -1)
						{
							isRunning = false;
							retryNewMapScreen=false;
						}
						else
						{
							retryNewMapScreen=false;
						}
					}
				}
				else if (rc==HowNewMapScreen::LOADMAP)
				{
					ChooseMapScreen chooseMapScreen("maps", "map", false, "games", "game", false);
					int rc=chooseMapScreen.execute(globalContainer->gfx, 40);
					if (rc==ChooseMapScreen::OK)
					{
						MapEdit mapEdit;
						std::string filename = chooseMapScreen.getMapHeader().getFileName();
						mapEdit.load(filename.c_str());
						if (mapEdit.run()==-1)
							isRunning=false;
					}
					else if (rc==-1)
						isRunning=false;
				}
				else if (rc==HowNewMapScreen::NEWCAMPAIGN)
				{
					CampaignEditor ce("");
					ce.execute(globalContainer->gfx, 40);

				}
				else if (rc==HowNewMapScreen::LOADCAMPAIGN)
				{
					CampaignSelectorScreen css;
					int rc_css=css.execute(globalContainer->gfx, 40);
					if(rc_css==CampaignSelectorScreen::OK)
					{
						CampaignEditor ce(css.getCampaignName());
						ce.execute(globalContainer->gfx, 40);
					}
					else if(rc_css==CampaignSelectorScreen::CANCEL)
					{
					}
				}
				else if (rc==HowNewMapScreen::CANCEL)
				{
					// Let's sing.
				}
				else if (rc==-1)
				{
					isRunning=false;
				}
				else
					assert(false);

			}
			break;
			case MainMenuScreen::CREDITS:
			{
				CreditScreen creditScreen;
				if (creditScreen.execute(globalContainer->gfx, 40)==-1)
					isRunning=false;
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

	// This is for the textshot code
	GAGCore::DrawableSurface::printFinishingText();
//	delete yog;
	delete globalContainer;
	return 0;
}

int main(int argc, char *argv[])
{
#ifdef __APPLE__
	/* SDL has this annoying "feature" of setting working directory to parent
	   of bundle during static initalization.  We want to set it back to the
	   main bundle directory so we can find our Resources directory. */
	CFBundleRef mainBundle = CFBundleGetMainBundle();
	assert(mainBundle);
	CFURLRef mainBundleURL = CFBundleCopyBundleURL(mainBundle);
	assert(mainBundleURL);
	CFStringRef cfStringRef = CFURLCopyFileSystemPath(mainBundleURL, kCFURLPOSIXPathStyle);
	assert(cfStringRef);
	
	char path[MAXPATHLEN];
	CFStringGetCString(cfStringRef, path, MAXPATHLEN, kCFStringEncodingASCII);
	chdir(path);
	
	CFRelease(mainBundleURL);
	CFRelease(cfStringRef);
#endif

	Glob2 glob2;
	return glob2.run(argc, argv);
}
