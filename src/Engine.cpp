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

#include "Engine.h"
#include "GlobalContainer.h"
#include "MultiplayersHostScreen.h"
#include "MultiplayersJoinScreen.h"
#include "CustomGameScreen.h"
#include "YOGScreen.h"

Engine::Engine()
{
	net=NULL;
}

Engine::~Engine()
{
	if (net)
	{
		delete net;
		net=NULL;
	}
}

int Engine::initCampain(void)
{
	// we load map
	SDL_RWops *stream=globalContainer->fileManager.open("default.map","rb");
	if (gui.game.load(stream)==false)
	{
		fprintf(stderr, "ENG : Error during map load\n");
		SDL_RWclose(stream);
		return EE_CANT_LOAD_MAP;
	}
	SDL_RWclose(stream);

	// we make a player for each team
	int playerNumber=0;
	bool wasHuman=false;
	char name[16];
	int i;
	for (i=0; i<gui.game.session.numberOfTeam; i++)
	{
		if (gui.game.teams[i]->type==BaseTeam::T_AI)
		{
			snprintf(name, 16, "AI Player %d", playerNumber);
			gui.game.players[playerNumber]=new Player(playerNumber, name, gui.game.teams[i], BasePlayer::P_AI);
		}
		else if (gui.game.teams[i]->type==BaseTeam::T_HUMAN)
		{
			if (!wasHuman)
			{
				gui.localPlayer=playerNumber;
				gui.localTeam=i;
				snprintf(name, 16, "Player %d", playerNumber);
				wasHuman=true;
				gui.game.players[playerNumber]=new Player(playerNumber, name, gui.game.teams[i], BasePlayer::P_LOCAL);
			}
			else
			{
				snprintf(name, 16, "AI Player %d", playerNumber);
				gui.game.players[playerNumber]=new Player(playerNumber, name, gui.game.teams[i], BasePlayer::P_AI);
			}
		}
		gui.game.teams[i]->numberOfPlayer=1;
		gui.game.teams[i]->playersMask=(1<<playerNumber);
		playerNumber++;
	}

	gui.game.session.numberOfPlayer=playerNumber;
	gui.game.renderMiniMap(gui.localTeam);
	gui.adjustInitialViewport();

	// FIXME : delete Team that hasn't any players and defrag array

	if (!wasHuman)
	{
		fprintf(stderr, "ENG : Error, can't find any human player\n");
		return EE_CANT_FIND_PLAYER;
	}

	// we create the net game
	net=new NetGame(NULL, gui.game.session.numberOfPlayer, gui.game.players);

	globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight, 32, globalContainer->graphicFlags);

	return EE_NO_ERROR;
}

int Engine::initCustom(void)
{
	CustomGameScreen customGameScreen;

	int cgs = customGameScreen.execute(globalContainer->gfx, 20);

	if (cgs == CustomGameScreen::CANCEL)
		return EE_CANCEL;

	gui.game.loadBase(&(customGameScreen.sessionInfo));
	int nbTeam=gui.game.session.numberOfTeam;
	if (nbTeam==0)
		return EE_CANCEL;

	char name[16];
	int i;
	int nbPlayer=0;

	for (i=0; i<8; i++)
	{
		if (customGameScreen.isAIactive(i))
		{
			int teamColor=customGameScreen.getSelectedColor(i);
			if (i==0)
			{
				gui.game.players[nbPlayer]=new Player(0, globalContainer->settings.userName, gui.game.teams[teamColor], BasePlayer::P_LOCAL);
				gui.localPlayer=nbPlayer;
				gui.localTeam=teamColor;
			}
			else
			{
				snprintf(name, 16, "%s %d", globalContainer->texts.getString("[ai]"), nbPlayer-1);
				gui.game.players[nbPlayer]=new Player(i, name, gui.game.teams[teamColor], BasePlayer::P_AI);
			}
			gui.game.teams[teamColor]->numberOfPlayer++;
			gui.game.teams[teamColor]->playersMask|=(1<<nbPlayer);
			nbPlayer++;
		}
	}
	// TODO : destroy team that have no attached player
	// should be in game.defragTeamArray();

	gui.game.session.numberOfPlayer=nbPlayer;
	gui.game.renderMiniMap(gui.localTeam);
	gui.adjustInitialViewport();

	net=new NetGame(NULL, gui.game.session.numberOfPlayer, gui.game.players);

	return EE_NO_ERROR;
}

int Engine::initCustom(char *gameName)
{
	SDL_RWops *stream=globalContainer->fileManager.open(gameName,"rb");
	if (stream)
	{
		gui.load(stream);
		SDL_RWclose(stream);
		printf("Engine : game is loaded\n");
	}
	else
	{
		printf("Engine : Can't load map\n"); 
		return EE_CANCEL;
	}
	
	gui.game.renderMiniMap(gui.localTeam);
	gui.adjustInitialViewport();

	net=new NetGame(NULL, gui.game.session.numberOfPlayer, gui.game.players);

	return EE_NO_ERROR;
}

void Engine::startMultiplayer(SessionConnection *screen)
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
	gui.viewportX=gui.game.teams[gui.localTeam]->startPosX-((globalContainer->gfx->getW()-128)>>6);
	gui.viewportY=gui.game.teams[gui.localTeam]->startPosY-(globalContainer->gfx->getH()>>6);
	gui.viewportX=(gui.viewportX+gui.game.map.getW())%gui.game.map.getW();
	gui.viewportY=(gui.viewportY+gui.game.map.getH())%gui.game.map.getH();

	// we create the net game
	net=new NetGame(screen->socket, gui.game.session.numberOfPlayer, gui.game.players);

	globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight, 32, globalContainer->graphicFlags);

	printf("Engine::localPlayer=%d, localTeam=%d\n", gui.localPlayer, gui.localTeam);
}


int Engine::initMutiplayerHost(bool shareOnYOG)
{
	MultiplayersChooseMapScreen multiplayersChooseMapScreen;

	int mpcms=multiplayersChooseMapScreen.execute(globalContainer->gfx, 20);

	if (mpcms==MultiplayersChooseMapScreen::CANCEL)
		return EE_CANCEL;
	if (mpcms==-1)
		return -1;

	printf("Engine::the game is sharing ...\n");

	MultiplayersHostScreen multiplayersHostScreen(&(multiplayersChooseMapScreen.sessionInfo), shareOnYOG);
	int rc=multiplayersHostScreen.execute(globalContainer->gfx, 20);
	if (rc==MultiplayersHostScreen::STARTED)
	{
		if (multiplayersHostScreen.multiplayersJoin==NULL)
			return EE_CANCEL;
		else
		{
			assert(multiplayersHostScreen.multiplayersJoin->myPlayerNumber!=-1);
			startMultiplayer(multiplayersHostScreen.multiplayersJoin);
		}
		return EE_NO_ERROR;
	}
	else if (rc==-1)
		return -1;
		
	printf("Engine::initMutiplayerHost() rc=%d\n", rc);

	return EE_CANCEL;
}

int Engine::initMutiplayerJoin(void)
{
	MultiplayersJoinScreen multiplayersJoinScreen;

	int rc=multiplayersJoinScreen.execute(globalContainer->gfx, 20);
	if (rc==MultiplayersJoinScreen::STARTED)
	{
		startMultiplayer(multiplayersJoinScreen.multiplayersJoin);

		return EE_NO_ERROR;
	}
	else if (rc==-1)
		return -1;

	return EE_CANCEL;
}

int Engine::run(void)
{
	bool doRunOnceAggain=true;
	
	while (doRunOnceAggain)
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
			for (int i=0; i<gui.game.session.numberOfPlayer; ++i)
			{
				if (gui.game.players[i]->ai && gui.game.players[i]->team->isAlive)
					net->pushOrder(gui.game.players[i]->ai->getOrder(), i);
			}

			//printf ("Engine::bns:%d\n", globalContainer->safe());

			// we proceed network
			net->step();

			//printf ("Engine::bge:%d\n", globalContainer->safe());

			for (int i=0; i<gui.game.session.numberOfPlayer; ++i)
			{
				if (gui.game.players[i]->team->isAlive)
					gui.executeOrder(net->getOrder(i));
			}

			//printf ("Engine::bne:%d\n", globalContainer->safe());

			// here we do the real work
			gui.game.step(gui.localTeam);

			//printf ("Engine::bdr:%d\n", globalContainer->safe());

			// we draw
			gui.drawAll(gui.localTeam);

			//globalContainer->gfx->drawLine(ticknb, 0, ticknb, 480, 255, 0 ,0);
			//ticknb=(ticknb+1)%(640-128);

			globalContainer->gfx->nextFrame();

			endTick=SDL_GetTicks();
			deltaTick=endTick-startTick-net->advance();
			//if (net->advance())
			//	printf("advance=%d\n", net->advance());
			if (deltaTick<(unsigned)gui.game.session.gameTPF)
				SDL_Delay((unsigned)gui.game.session.gameTPF-deltaTick);

			//printf ("Engine::end:%d\n", globalContainer->safe());
		}

		delete net;
		net=NULL;
		
		if (gui.exitGlobCompletely)
			return -1; // There is no bypass for the "close window button"
		
		doRunOnceAggain=false;
		
		if (gui.toLoadGameFileName[0])
		{
			int rv=initCustom(gui.toLoadGameFileName);
			if (rv==EE_NO_ERROR)
				doRunOnceAggain=true;
			gui.toLoadGameFileName[0]=0; // Avoid the communication system between GameGUI and Engine to loop.
		}
	}
	
	return EE_NO_ERROR;
}
