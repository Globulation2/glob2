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
#include <SDL_net.h>

GlobalContainer *globalContainer=0;

void drawYOGSplashScreen(void)
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

int main(int argc, char *argv[])
{
	globalContainer = new GlobalContainer();

	globalContainer->parseArgs(argc, argv);
	globalContainer->load();
	globalContainer->gfx->setCaption("Globulation 2", "glob 2");
	
	globalContainer->buildingsTypes.load("data/buildings.txt");


	if ( SDLNet_Init() < 0 ) {
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
				// TODO : call create game screen with special parameters if it is custom/yog/lan
			}
			break;
			case 2:
			{
				YOGScreen yogScreen;
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
				}
			}
			break;
			case 3:
			{
				switch (MultiplayersOfferScreen::menu())
				{
					case MultiplayersOfferScreen::HOST :
					{
						Engine engine;
						if (engine.initMutiplayerHost()==Engine::NO_ERROR)
							if (engine.run()==-1)
								run=false;
					}
					break;

					case MultiplayersOfferScreen::JOIN :
					{
						Engine engine;
						if (engine.initMutiplayerJoin()==Engine::NO_ERROR)
							if (engine.run()==-1)
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
				MapEdit mapEdit;
				if (mapEdit.run()==-1)
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
	return 0;
}


