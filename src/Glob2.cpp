/*
 * Globulation 2 main file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "Glob2.h"
#include "GAG.h"
#include "Game.h"
#include "PreparationGui.h"
#include "MapEdit.h"
#include "Engine.h"
#include "GlobalContainer.h"
#include "YOGScreen.h"
#include <SDL_net.h>

GlobalContainer *globalContainer=0;

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
			case 4: break;
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


