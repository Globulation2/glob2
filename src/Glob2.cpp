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
#include "SDL_net.h"

GlobalContainer globalContainer;

int main(int argc, char *argv[])
{
	globalContainer.parseArgs(argc, argv);
	globalContainer.gfx.setRes(640, 480, 32, globalContainer.graphicFlags);
	globalContainer.load();

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

			}
			break;
			case 2:
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
			case 3: break;
			case 4:
			{
				MapEdit mapEdit;
				if (mapEdit.run()==-1)
					run=false;
			}
			break;
			case 5:
			{
				run=false;
			}
			break;
			default:
			break;
		}
	}

}


