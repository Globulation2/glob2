 /*
 * Globulation 2 Engine
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "Engine.h"
#include "GlobalContainer.h"
#include "PreparationGui.h"



int Engine::init(void)
{
	assert(false);
	return 0; // not sure about this but should return a value...
}

int Engine::initCampain(void)
{
	// we load map
	SDL_RWops *stream=globalContainer->fileManager.open("default.map","rb");
	if (gui.game.load(stream)==false)
	{
		fprintf(stderr, "ENG : Error during map load\n");
		SDL_RWclose(stream);
		return CANT_LOAD_MAP;
	}
	SDL_RWclose(stream);

	// we make a player for each team
	int playerNumber=0;
	bool wasHuman=false;
	char name[16];
	{
		for (int i=0; i<gui.game.session.numberOfTeam; i++)
		{
			if (gui.game.teams[i]->type==BaseTeam::T_AI)
			{
				sprintf(name, "AI Player %d", playerNumber);
				gui.game.players[playerNumber]=new Player(playerNumber, name, gui.game.teams[i], BasePlayer::P_AI);
			}
			else if (gui.game.teams[i]->type==BaseTeam::T_HUMAM)
			{
				if (!wasHuman)
				{
					gui.localPlayer=playerNumber;
					gui.localTeam=i;
					sprintf(name, "Player %d", playerNumber);
					wasHuman=true;
					gui.game.players[playerNumber]=new Player(playerNumber, name, gui.game.teams[i], BasePlayer::P_LOCAL);
				}
				else
					continue;
			}
			gui.game.teams[i]->numberOfPlayer=1;
			gui.game.teams[i]->playersMask=(1<<playerNumber);
			playerNumber++;
		}
	}
	gui.game.session.numberOfPlayer=playerNumber;
	gui.game.renderMiniMap(gui.localTeam);
	gui.viewportX=gui.game.teams[gui.localTeam]->startPosX-((globalContainer->gfx.getW()-128)>>6);
	gui.viewportY=gui.game.teams[gui.localTeam]->startPosY-(globalContainer->gfx.getH()>>6);
	gui.viewportX=(gui.viewportX+gui.game.map.w)%gui.game.map.w;
	gui.viewportY=(gui.viewportY+gui.game.map.h)%gui.game.map.h;

	// FIXME : delete Team that hasn't any players and defrag array

	if (!wasHuman)
	{
		fprintf(stderr, "ENG : Error, can't find any human player\n");
		return CANT_FIND_PLAYER;
	}

	// we create the net game
	net=new NetGame(NULL, gui.game.session.numberOfPlayer, gui.game.players);

	globalContainer->gfx.setRes(640, 480, 32, globalContainer->graphicFlags);

	return NO_ERROR;
}

void Engine::startMultiplayer(SessionScreen *screen)
{
	int p=screen->myPlayerNumber;

	screen->destroyNet=false;
	for (int j=0; j<screen->sessionInfo.numberOfPlayer; j++)
		screen->sessionInfo.players[j].destroyNet=false;

	screen->sessionInfo.setLocal(p);

	gui.game.loadBase(&screen->sessionInfo);

	gui.localPlayer=p;
	gui.localTeam=screen->sessionInfo.players[p].teamNumber;
	assert(gui.localTeam<screen->sessionInfo.numberOfTeam);
	gui.localTeam=gui.localTeam % screen->sessionInfo.numberOfTeam; // Ugly relase case.

	gui.game.renderMiniMap(gui.localTeam);
	gui.viewportX=gui.game.teams[gui.localTeam]->startPosX-((globalContainer->gfx.getW()-128)>>6);
	gui.viewportY=gui.game.teams[gui.localTeam]->startPosY-(globalContainer->gfx.getH()>>6);
	gui.viewportX=(gui.viewportX+gui.game.map.w)%gui.game.map.w;
	gui.viewportY=(gui.viewportY+gui.game.map.h)%gui.game.map.h;

	// we create the net game
	net=new NetGame(screen->socket, gui.game.session.numberOfPlayer, gui.game.players);

	globalContainer->gfx.setRes(640, 480, 32, globalContainer->graphicFlags);

	printf("localPlayer=%d, localTeam=%d\n", gui.localPlayer, gui.localTeam);
}

int Engine::initMutiplayerHost(void)
{
	MultiplayersChooseMapScreen multiplayersChooseMapScreen;
	
	int mpcms = multiplayersChooseMapScreen.execute(&globalContainer->gfx, 20);
	
	if (mpcms == MultiplayersChooseMapScreen::CANCEL)
		return CANCEL;
	
	printf("the game is sharing ...\n");
	
	MultiplayersHostScreen multiplayersHostScreen( &(multiplayersChooseMapScreen.sessionInfo) );
	multiplayersHostScreen.newHostPlayer();
	if (multiplayersHostScreen.execute(&globalContainer->gfx, 20)==MultiplayersHostScreen::STARTED)
	{
		if (multiplayersHostScreen.myPlayerNumber==-1)
			return CANCEL;
		else
			startMultiplayer(&multiplayersHostScreen);

		return NO_ERROR;
	}
	
	return CANCEL;
}

int Engine::initMutiplayerJoin(void)
{
	MultiplayersJoinScreen multiplayersJoinScreen;
	
	if (multiplayersJoinScreen.execute(&globalContainer->gfx, 20)==MultiplayersJoinScreen::STARTED)
	{
		startMultiplayer(&multiplayersJoinScreen);

		return NO_ERROR;
	}
	
	return CANCEL;
}


int Engine::run(void)
{
	//int ticknb=0;
	Uint32 startTick, endTick, deltaTick;
	while (gui.isRunning)
	{
		//printf ("Engine::begin:%d\n", globalContainer->safe());
		startTick=SDL_GetTicks();
	
		// we get and push local orders
		
		//printf ("Engine::bgu:%d\n", globalContainer->safe());
		
		gui.step();

		//printf ("Engine::bnp:%d\n", globalContainer->safe());
		
		net->pushOrder(gui.getOrder(), gui.localPlayer);

		// we get and push ai orders
		{
			for (int i=0; i<gui.game.session.numberOfPlayer; ++i)
			{
				if (gui.game.players[i]->ai)
					net->pushOrder(gui.game.players[i]->ai->getOrder(), i);
			}
		}
		
		//printf ("Engine::bns:%d\n", globalContainer->safe());

		// we proceed network
		net->step();
		
		//printf ("Engine::bge:%d\n", globalContainer->safe());

		{
			for (int i=0; i<gui.game.session.numberOfPlayer; ++i)
			{
				gui.executeOrder(net->getOrder(i));
			}
		}

		//printf ("Engine::bne:%d\n", globalContainer->safe());
		
		// here we do the real work
		gui.game.step(gui.localTeam);
		
		//printf ("Engine::bdr:%d\n", globalContainer->safe());

		// we draw
		gui.drawAll(gui.localTeam);
		
		//globalContainer->gfx.drawLine(ticknb, 0, ticknb, 480, 255, 0 ,0);
		//ticknb=(ticknb+1)%(640-128);
		
		globalContainer->gfx.nextFrame();
		
		endTick=SDL_GetTicks();
		deltaTick=endTick-startTick-net->advance();
		//if (net->advance())
		//	printf("advance=%d\n", net->advance());
		if (deltaTick<(unsigned)gui.game.session.gameTPF)
			SDL_Delay((unsigned)gui.game.session.gameTPF-deltaTick);
		
		//printf ("Engine::end:%d\n", globalContainer->safe());
	}

	delete net;

	return 0;
}
