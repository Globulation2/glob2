/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "Engine.h"
#include "GlobalContainer.h"
#include "MultiplayersHostScreen.h"
#include "MultiplayersJoinScreen.h"
#include "MultiplayersChooseMapScreen.h"
#include "CustomGameScreen.h"
#include "LoadGameScreen.h"
#include "YOGScreen.h"
#include "Utilities.h"
#include "NetGame.h"
#include "EndGameScreen.h"

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
	SDL_RWops *stream=globalContainer->fileManager->open("default.map","rb");
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
	char name[BasePlayer::MAX_NAME_LENGTH];
	int i;
	for (i=0; i<gui.game.session.numberOfTeam; i++)
	{
		if (gui.game.teams[i]->type==BaseTeam::T_AI)
		{
			snprintf(name, BasePlayer::MAX_NAME_LENGTH, "AI Player %d", playerNumber);
			gui.game.players[playerNumber]=new Player(playerNumber, name, gui.game.teams[i], BasePlayer::P_AI);
		}
		else if (gui.game.teams[i]->type==BaseTeam::T_HUMAN)
		{
			if (!wasHuman)
			{
				gui.localPlayer=playerNumber;
				gui.localTeamNo=i;
				snprintf(name, BasePlayer::MAX_NAME_LENGTH, "Player %d", playerNumber);
				wasHuman=true;
				gui.game.players[playerNumber]=new Player(playerNumber, name, gui.game.teams[i], BasePlayer::P_LOCAL);
			}
			else
			{
				snprintf(name, BasePlayer::MAX_NAME_LENGTH, "AI Player %d", playerNumber);
				gui.game.players[playerNumber]=new Player(playerNumber, name, gui.game.teams[i], BasePlayer::P_AI);
			}
		}
		gui.game.teams[i]->numberOfPlayer=1;
		gui.game.teams[i]->playersMask=(1<<playerNumber);
		playerNumber++;
	}

	if (!wasHuman)
	{
		fprintf(stderr, "ENG : Error, can't find any human player\n");
		return EE_CANT_FIND_PLAYER;
	}

	gui.game.session.numberOfPlayer=playerNumber;
	gui.game.renderMiniMap(gui.localTeamNo);
	gui.adjustInitialViewport();

	// FIXME : delete Team that hasn't any players and defrag array

	// we create the net game
	net=new NetGame(NULL, gui.game.session.numberOfPlayer, gui.game.players);

	globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight, 32, globalContainer->graphicFlags);

	return EE_NO_ERROR;
}

int Engine::initCustom(void)
{
	CustomGameScreen customGameScreen;

	int cgs=customGameScreen.execute(globalContainer->gfx, 40);

	if (cgs==CustomGameScreen::CANCEL)
		return EE_CANCEL;
	
	//if (!gui.game.loadBase(&(customGameScreen.sessionInfo)))
	if (!gui.loadBase(&(customGameScreen.sessionInfo)))
	{
		printf("Engine : Can't load map\n");
		return EE_CANCEL;
	}
	int nbTeam=gui.game.session.numberOfTeam;
	if (nbTeam==0)
		return EE_CANCEL;

	char name[BasePlayer::MAX_NAME_LENGTH];
	int i;
	int nbPlayer=0;

	for (i=0; i<8; i++)
	{
		if (customGameScreen.isAIactive(i))
		{
			int teamColor=customGameScreen.getSelectedColor(i);
			if (i==0)
			{
				gui.game.players[nbPlayer]=new Player(0, globalContainer->userName, gui.game.teams[teamColor], BasePlayer::P_LOCAL);
				gui.localPlayer=nbPlayer;
				gui.localTeamNo=teamColor;
			}
			else
			{
				snprintf(name, BasePlayer::MAX_NAME_LENGTH, "%s %d", globalContainer->texts.getString("[ai]"), nbPlayer-1);
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
	gui.game.renderMiniMap(gui.localTeamNo);
	gui.adjustInitialViewport();

	// set the correct alliance
	gui.game.setAIAlliance();

	net=new NetGame(NULL, gui.game.session.numberOfPlayer, gui.game.players);

	return EE_NO_ERROR;
}

int Engine::initCustom(const char *gameName)
{
	assert(gameName);
	assert(gameName[0]);
	SDL_RWops *stream=globalContainer->fileManager->open(gameName,"rb");
	if (stream)
	{
		if (gui.load(stream))
		{
			printf("Engine : game is loaded\n");
			SDL_RWclose(stream);
		}
		else
		{
			printf("Engine : Error during load of %s\n", gameName);
			SDL_RWclose(stream);
			return EE_CANCEL;
		}
	}
	else
	{
		printf("Engine : Can't load map %s\n", gameName);
		return EE_CANCEL;
	}

	printf("Engine::initCustom:: numberOfPlayer=%d numberOfTeam=%d.\n", gui.game.session.numberOfTeam, gui.game.session.numberOfPlayer);

	// If the game is a network saved game, we need to toogle net players to ai players:
	for (int p=0; p<gui.game.session.numberOfPlayer; p++)
	{
		printf("Engine::initCustom::player[%d].type=%d.\n", p, gui.game.players[p]->type);
		if (gui.game.players[p]->type==BasePlayer::P_IP)
		{
			gui.game.players[p]->makeItAI();
			printf("Engine::initCustom::net player (id %d) was made ai.\n", p);
		}
	}

	gui.game.renderMiniMap(gui.localTeamNo);
	gui.adjustInitialViewport();

	// set the correct alliance
	gui.game.setAIAlliance();

	net=new NetGame(NULL, gui.game.session.numberOfPlayer, gui.game.players);

	return EE_NO_ERROR;
}

int Engine::initLoadGame()
{
	// TODO: It's maybe cleaner to only use a string instead of all sessionInfo.
	LoadGameScreen loadGameScreen;
	int lgs=loadGameScreen.execute(globalContainer->gfx, 40);
	if (lgs==LoadGameScreen::CANCEL)
		return EE_CANCEL;

	return initCustom(loadGameScreen.sessionInfo.getGameFileName());
}

void Engine::startMultiplayer(MultiplayersJoin *multiplayersJoin)
{
	int p=multiplayersJoin->myPlayerNumber;

	multiplayersJoin->destroyNet=false;
	for (int j=0; j<multiplayersJoin->sessionInfo.numberOfPlayer; j++)
		multiplayersJoin->sessionInfo.players[j].destroyNet=false;

	multiplayersJoin->sessionInfo.setLocal(p);

	gui.loadBase(&multiplayersJoin->sessionInfo);

	gui.localPlayer=p;
	gui.localTeamNo=multiplayersJoin->sessionInfo.players[p].teamNumber;
	assert(gui.localTeamNo<multiplayersJoin->sessionInfo.numberOfTeam);
	gui.localTeamNo=gui.localTeamNo % multiplayersJoin->sessionInfo.numberOfTeam; // Ugly relase case.

	gui.game.renderMiniMap(gui.localTeamNo);
	gui.adjustInitialViewport();
	
	// set the correct alliance
	gui.game.setAIAlliance();
	
	// we create the net game
	net=new NetGame(multiplayersJoin->socket, gui.game.session.numberOfPlayer, gui.game.players);

	globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight, 32, globalContainer->graphicFlags);

	printf("Engine::localPlayer=%d, localTeamNb=%d\n", gui.localPlayer, gui.localTeamNo);
}


int Engine::initMutiplayerHost(bool shareOnYOG)
{
	MultiplayersChooseMapScreen multiplayersChooseMapScreen(shareOnYOG);

	int mpcms=multiplayersChooseMapScreen.execute(globalContainer->gfx, 40);

	if (mpcms==MultiplayersChooseMapScreen::CANCEL)
		return EE_CANCEL;
	if (mpcms==-1)
		return -1;

	printf("Engine::the game is sharing ...\n");
	
	MultiplayersHostScreen multiplayersHostScreen(&(multiplayersChooseMapScreen.sessionInfo), shareOnYOG);
	int rc=multiplayersHostScreen.execute(globalContainer->gfx, 40);
	if (rc==MultiplayersHostScreen::STARTED)
	{
		if (multiplayersHostScreen.multiplayersJoin==NULL)
			return EE_CANCEL;
		else
		{
			if (multiplayersHostScreen.multiplayersJoin->myPlayerNumber!=-1)
				startMultiplayer(multiplayersHostScreen.multiplayersJoin);
			else
				return EE_CANCEL;
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

	int rc=multiplayersJoinScreen.execute(globalContainer->gfx, 40);
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
		bool networkReadyToExecute=true;
		while (gui.isRunning)
		{
			startTick=SDL_GetTicks();

			// We allways allow the user ot use the gui:
			gui.step();
			
			// But some jobs have to be executed synchronously:
			if (networkReadyToExecute)
				gui.synchroneStep();

			if (!gui.paused) // TODO: this is an ugly pause !
			{
				if (networkReadyToExecute)
				{
					// We get and push local orders
					net->pushOrder(gui.getOrder(), gui.localPlayer);

					// we get and push ai orders
					for (int i=0; i<gui.game.session.numberOfPlayer; i++)
						if (gui.game.players[i]->ai)
							net->pushOrder(gui.game.players[i]->ai->getOrder(), i);
				}
				
				// We proceed network:
				networkReadyToExecute=net->stepReadyToExecute();

				// We get all currents orders from the network and execute them:
				for (int i=0; i<gui.game.session.numberOfPlayer; ++i)
				{
					Order *order=net->getOrder(i);
					gui.executeOrder(order);
					
					// Some orders needs to be freed:
					net->orderHasBeenExecuted(order);
				}
				
				net->stepExecuted();

				// here we do the real work
				gui.game.step(gui.localTeamNo);
			}

			// we draw
			gui.drawAll(gui.localTeamNo);

			//globalContainer->gfx->drawLine(ticknb, 0, ticknb, 480, 255, 0 ,0);
			//ticknb=(ticknb+1)%(640-128);

			globalContainer->gfx->nextFrame();

			endTick=SDL_GetTicks();
			deltaTick=endTick-startTick;
			if (deltaTick<(unsigned)gui.game.session.gameTPF)
				SDL_Delay((unsigned)gui.game.session.gameTPF-deltaTick);
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
	
	// Display End Game Screen
	EndGameScreen endGameScreen(&gui);
	endGameScreen.execute(globalContainer->gfx, 40);
	
	return EE_NO_ERROR;
}
