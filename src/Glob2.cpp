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

GlobalContainer *globalContainer=0;

int main(int argc, char *argv[])
{
	globalContainer = new GlobalContainer();

	globalContainer->parseArgs(argc, argv);
	globalContainer->load();
	globalContainer->gfx->setCaption("Globulation 2", "glob 2");

	if ( SDLNet_Init() < 0 )
	{
		fprintf(stderr, "Couldn't initialize net: %s\n", SDLNet_GetError());
		exit(1);
	}
	atexit(SDLNet_Quit);

	bool run=true;
	while (run)
	{
		switch (MainMenuScreen::menu())
		{
			case -1:
			{
				run=false;
			}
			break;
			case 0:
			{
				Engine engine;
				if (engine.initCampain()==Engine::NO_ERROR)
					if (engine.run()==-1)
						run=false;
			}
			break;
			case 1:
			{
				Engine engine;
				if (engine.initCustom()==Engine::NO_ERROR)
					if (engine.run()==-1)
						run=false;
			}
			break;
			case 2:
			{
				Engine engine;
				int rc=engine.initMutiplayerYOG();
				if (rc==Engine::NO_ERROR)
				{
					if (engine.run()==-1)
						run=false;
				}
				else if (rc==-1)
					run=false;
				
				/*YOGScreen yogScreen;
				drawYOGSplashScreen();
				yogScreen.createConnection();
				if (yogScreen.socket!=NULL)
				{
					int yogReturnCode=yogScreen.execute(globalContainer->gfx, 20);
					yogScreen.closeConnection();
					if (yogReturnCode==1)
					{
						// TODO : join game in joyScreen.ip;
					}
					else if (yogReturnCode==2)
					{
						// TODO : create game
					}
				}*/
			}
			break;
			case 3:
			{
				switch (MultiplayersOfferScreen::menu())
				{
					case MultiplayersOfferScreen::HOST :
					{
						Engine engine;
						int rc=engine.initMutiplayerHost();
						if (rc==Engine::NO_ERROR)
						{
							if (engine.run()==-1)
								run=false;
						}
						else if (rc==-1)
							run=false;
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
								run=false;
						}
						else if (rc==-1)
							run=false;
					}
					break;

					case MultiplayersOfferScreen::QUIT :
					{

					}
					break;

					case -1 :
					{
						run=false;
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
					run=false;
			}
			break;
			case 6:
			{
				run=false;
			}
			break;
			default:
			break;
		}
	}

	delete globalContainer;
	return 0;
}


