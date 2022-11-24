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

#include "Glob2.h"
#include "GlobalContainer.h"
#include "YOGServer.h"
#include <thread>
#include <atomic>

#ifndef YOG_SERVER_ONLY
#include "CampaignEditor.h"
#include "CampaignMenuScreen.h"
#include "CampaignMainMenu.h"
#include "CampaignSelectorScreen.h"
#include "ChooseMapScreen.h"
#include "CreditScreen.h"
#include "EditorMainMenu.h"
#include "Engine.h"
#include "Game.h"
#include "GUIMessageBox.h"
#include "Header.h"
#include "LANFindScreen.h"
#include "LANMenuScreen.h"
#include "MainMenuScreen.h"
#include "MapEdit.h"
#include "NetBroadcastListener.h"
#include "MapGenerator.h"
#include "NewMapScreen.h"
#include "SettingsScreen.h"
#include <StringTable.h>
#include "Utilities.h"
#include "YOGClient.h"
#include "YOGLoginScreen.h"
#include "YOGServerRouter.h"
#include "YOGClientRouterAdministrator.h"


#include <Stream.h>
#include <BinaryStream.h>

#include <stdio.h>
#include <sys/types.h>

#endif  // !YOG_SERVER_ONLY

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

using boost::shared_ptr;

/*!	\mainpage Globulation 2 Reference documentation

	\section intro Introduction
	This is the documentation of Globulation 2, a free
	software game. It covers Glob2 itself and
	libgag (graphic and widget).
	\section feedback Feedback
	This documentation is not yet complete, but should help to have an
	overview of Globulation's 2 code. If you have any comments or suggestions,
	do not hesitate to contact the development team at
	http://www.globulation2.org
*/

GlobalContainer *globalContainer=NULL;


#ifndef YOG_SERVER_ONLY

void Glob2::drawYOGSplashScreen(void)
{
	int w, h;
	w=globalContainer->gfx->getW();
	h=globalContainer->gfx->getH();
	globalContainer->gfx->drawFilledRect(0, 0, w, h, 0, 0, 0);
	std::string text[3];
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
	printf("nox::running %d times %d steps:\n", globalContainer->runNoXCountRuns, globalContainer->automaticEndingSteps);
	for (int runNoXCount = 0; runNoXCount < globalContainer->runNoXCountRuns; runNoXCount++)
	{
		Engine engine;
		if (engine.initCustom(globalContainer->runNoXGameName) != Engine::EE_NO_ERROR)
			return 1;
		engine.run();
	}
	return 0;
}



int Glob2::runTestGames()
{
	globalContainer->automaticEndingSteps=90000;
	while(true)
	{
		long t = time(NULL);
		setSyncRandSeed(t);
		std::cout<<"Random Seed initial: "<<t<<std::endl;
		Engine engine;
		engine.createRandomGame();
		engine.run();
	}
	return 0;
}



int Glob2::runTestMapGeneration()
{
	long t = time(NULL);
	setSyncRandSeed(t);
	while(true)
	{
		MapGenerationDescriptor descriptor;
		
		int type = (syncRand() % 7) + 1;
		int wDec = (syncRand() % 4) + 6;
		int hDec = (syncRand() % 4) + 6;
		int teams = (syncRand() % 12) + 1;
		int workers = (syncRand() % 8) + 1;
		int repeat = (syncRand() % 5);
		int smooth = (syncRand() % 8) + 1;
		
		int oldBeach = (syncRand() % 4);
		
		descriptor.methode = static_cast<MapGenerationDescriptor::Methode>(type);
		descriptor.nbTeams = teams;
		descriptor.wDec=wDec;
		descriptor.hDec=hDec;
		descriptor.smooth = smooth;
		descriptor.oldBeach=oldBeach;
		descriptor.nbWorkers=workers;
		descriptor.logRepeatAreaTimes = repeat;
		
		descriptor.waterRatio=syncRand() % 100;
		descriptor.sandRatio=syncRand() % 100;
		descriptor.grassRatio=syncRand() % 100;
		descriptor.desertRatio=syncRand() % 100;
		descriptor.wheatRatio=syncRand() % 100;
		descriptor.woodRatio=syncRand() % 100;
		descriptor.algaeRatio=syncRand() % 100;
		descriptor.stoneRatio=syncRand() % 100;
		descriptor.fruitRatio=syncRand() % 100;
		descriptor.riverDiameter=syncRand() % 100;
		descriptor.craterDensity=syncRand() % 100;
		descriptor.extraIslands=syncRand() % 9;
		//eISLANDS
		descriptor.oldIslandSize=syncRand() % 74;
		

		std::cout<<"Generating Map"<<std::endl;		
		MapGenerator generator;
		Game game(NULL);
		generator.generateMap(game, descriptor);
	}
	return 0;
}
#endif  // !YOG_SERVER_ONLY

int Glob2::run(int argc, char *argv[])
{
	srand(time(NULL));
	if (!globalContainer) {
		globalContainer=new GlobalContainer();
		globalContainer->parseArgs(argc, argv);
	}
	if (!globalContainer->mainthrSet) {
		globalContainer->mainthr = std::this_thread::get_id();
		globalContainer->mainthrSet = true;
		if (!globalContainer->hostServer) {
			globalContainer->otherthread = new std::thread(&Glob2::run, this, argc, argv);
		}
	}
	if (!globalContainer->hostServer &&
	    std::this_thread::get_id() == globalContainer->mainthr) {
		globalContainer->load(true);
	}
	else {
		globalContainer->load(false);
	}

	if ( SDLNet_Init() < 0 )
	{
		fprintf(stderr, "Couldn't initialize net: %s\n", SDLNet_GetError());
		exit(1);
	}
	atexit(SDLNet_Quit);

	if (globalContainer->hostServer)
	{
		YOGServer server(YOGRequirePassword, YOGMultipleGames);
		int rc = server.run();
		delete globalContainer;
		return rc;
	}

// Glob2::run ends here for server.
#ifndef YOG_SERVER_ONLY

	if (globalContainer->hostRouter)
	{
		YOGServerRouter router;
		int rc = router.run();
		return rc;	
	}
	if(globalContainer->adminRouter)
	{
		YOGClientRouterAdministrator admin;
		return admin.execute();
	}
	
	if (globalContainer->runTestGames)
	{
		int ret=runTestGames();
		delete globalContainer;
		return ret;
	}
	
	if(globalContainer->runTestMapGeneration)
	{
		runTestMapGeneration();
	}
	
	if (globalContainer->runNoX)
	{
		int ret=runNoX();
		delete globalContainer;
		return ret;
	}

	isRunning=true;

	// Replay the game specified by the command line
	if (globalContainer->replaying)
	{
		Engine engine;
		int rc_e = engine.loadReplay(globalContainer->replayFileName);
		if (rc_e == Engine::EE_NO_ERROR)
			isRunning = (engine.run() != -1);
		else if(rc_e == -1)
			isRunning = false;
	}
 
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
				CampaignMainMenu ccs;
				int rccs=ccs.execute(globalContainer->gfx, 40);
				if(rccs == -1)
				{
					isRunning = false;
				}
			}
			break;
			case MainMenuScreen::TUTORIAL:
			{
				Campaign campaign;
				if(campaign.load("games/Tutorial_Campaign.txt"))
				{
					CampaignMenuScreen cms("games/Tutorial_Campaign.txt");
					int rc_cms=cms.execute(globalContainer->gfx, 40);
					if(rc_cms == -1)
					{
						isRunning = false;
					}
				}
				else
				{
					CampaignMenuScreen cms("campaigns/Tutorial_Campaign.txt");
					cms.setNewCampaign();
					int rc_cms=cms.execute(globalContainer->gfx, 40);
					if(rc_cms == -1)
					{
						isRunning = false;
					}
				}
			}
			break;
			case MainMenuScreen::LOAD_GAME:
			{
				Engine engine;
				int rc_e = engine.initLoadGame();
				if (rc_e == Engine::EE_NO_ERROR)
					isRunning = (engine.run() != -1);
				else if(rc_e == -1)
					isRunning = false;
			}
			break;
			case MainMenuScreen::CUSTOM:
			{
				bool cont=true;
				while(cont && isRunning)
				{
					Engine engine;
					int rc_e = engine.initCustom();
					if (rc_e ==  Engine::EE_NO_ERROR)
					{
						isRunning = (engine.run() != -1);
					}
					else if(rc_e == -1)
					{
						isRunning = false;
					}
					else
					{
						cont=false;	
					}
				}
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
				int rc_lms = lanms.execute(globalContainer->gfx, 40);
				if(rc_lms == -1)
					isRunning=false;
			}
			break;
			case MainMenuScreen::GAME_SETUP:
			{
				SettingsScreen settingsScreen;
				int rc_ss = settingsScreen.execute(globalContainer->gfx, 40);
				if( rc_ss == -1)
				{
					isRunning=false;
				}
			}
			break;
			case MainMenuScreen::EDITOR:
			{
				EditorMainMenu editorMainMenu;
				int rc=editorMainMenu.execute(globalContainer->gfx, 40);
				if (rc==-1)
				{
					isRunning=false;
				}
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
	delete globalContainer;

#endif  // !YOG_SERVER_ONLY

	return 0;
}

int main(int argc, char *argv[])
{
#ifdef __APPLE__
	/* SDL has this annoying "feature" of setting working directory to parent
	   of bundle during static initialization.  We want to set it back to the
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
