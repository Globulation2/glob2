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
#include "YOGServer.h"
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



int Glob2::runTestGames()
{
	globalContainer->runNoXCountSteps=90000;
	globalContainer->runNoX=true;
	while(true)
	{
		Engine engine;
		engine.createRandomGame();
		engine.run();
	}
	return 0;
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
		YOGServer server(YOGRequirePassword, YOGMultipleGames);
		int rc = server.run();
		return rc;	
	}
	
	
	if (globalContainer->runTestGames)
	{
		int ret=runTestGames();
		delete globalContainer;
		return ret;
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
				else if(rccs == CampaignChoiceScreen::CANCEL)
				{
				
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
				//Engine engine;
				//if (engine.initCampaign("maps/tutorial.map") == Engine::EE_NO_ERROR)
					//isRunning = (engine.run() != -1);
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
				Engine engine;
				int rc_e = engine.initCustom();
				if (rc_e ==  Engine::EE_NO_ERROR)
					isRunning = (engine.run() != -1);
				else if(rc_e == -1)
					isRunning = false;
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
					int rc=ce.execute(globalContainer->gfx, 40);
					if(rc == -1)
						isRunning=false;

				}
				else if (rc==HowNewMapScreen::LOADCAMPAIGN)
				{
					CampaignSelectorScreen css;
					int rc_css=css.execute(globalContainer->gfx, 40);
					if(rc_css==CampaignSelectorScreen::OK)
					{
						CampaignEditor ce(css.getCampaignName());
						int rc_ce=ce.execute(globalContainer->gfx, 40);
						if(rc_ce == -1)
						{
							isRunning=false;
						}
					}
					else if(rc_css==CampaignSelectorScreen::CANCEL)
					{
					}
					else if(rc_css == -1)
					{
						isRunning=false;
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
