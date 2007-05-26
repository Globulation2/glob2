/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifdef DX9_BACKEND
#include <Types.h>
#endif

#include <FileManager.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <FormatableString.h>

#include "CustomGameScreen.h"
#include "EndGameScreen.h"
#include "Engine.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Utilities.h"
#include "YOGScreen.h"
#include "SoundMixer.h"
#include "CampaignScreen.h"

#include <iostream>

using namespace boost;

Engine::Engine()
{
	net=NULL;
	cpuSumStats = 0;
	cpuSumCountStats = 0;
	for (int i=0; i<=40; i++)
		cpuStats[i]=0;
	logFile = globalContainer->logFileManager->getFile("Engine.log");
}

Engine::~Engine()
{
	fprintf(logFile, "\n");
	if (globalContainer->runNoX)
	{
		Sint32 noxTicks = noxEndTick - noxStartTick;
		double speed = (double)1000 * (double)cpuSumCountStats / (double)noxTicks;
		fprintf(logFile, "nox::cpuSumCountStats = %d\n", cpuSumCountStats);
		fprintf(logFile, "nox::noxTicks = %d\n", noxTicks);
		fprintf(logFile, "nox::speed = %f [steps/s]\n", speed);
		
		printf("nox::cpuSumCountStats = %d\n", cpuSumCountStats);
		printf("nox::noxTicks = %d\n", noxTicks);
		printf("nox::speed = %f [steps/s]\n", speed);
	}
	else
	{
		if (cpuSumCountStats)
		{
			fprintf(logFile, "cpuSumCountStats = %d\n", cpuSumCountStats);
			double averageCupUsage = (double)cpuSumStats / (double)cpuSumCountStats;
			fprintf(logFile, "averageCpuUsage = %lf%%\n", (double)2.5 * averageCupUsage);
		}
		fprintf(logFile, "cpu usage stats:\n");
		for (int i=0; i<=40; i++)
			fprintf(logFile, "%3d.%1d %% = %d\n", 100-(i*5)/2, (i&1)*5, cpuStats[i]);
		int sum=0;
		for (int i=0; i<=40; i++)
			sum+=cpuStats[i];
		fprintf(logFile, "cpu usage graph:\n");
		for (int i=0; i<=40; i++)
		{
			fprintf(logFile, "%3d.%1d %% | ", 100-(i*5)/2, (i&1)*5);
			double ratio=100.*(double)cpuStats[i]/(double)sum;
			int jmax=(int)(ratio+0.5);
			for (int j=0; j<jmax; j++)
				fprintf(logFile, "*");
			fprintf(logFile, "\n");
		}
	}

	if (net)
	{
		delete net;
		net=NULL;
	}
}



int Engine::initCampaign(const std::string &mapName, Campaign& campaign, const std::string& missionName)
{
	MapHeader mapHeader = loadMapHeader(mapName);
	GameHeader gameHeader = prepareCampaign(mapHeader);
	int end=initGame(mapHeader, gameHeader);
	gui.setCampaignGame(campaign, missionName);
	return end;
}



int Engine::initCampaign(const std::string &mapName)
{
	MapHeader mapHeader = loadMapHeader(mapName);
	GameHeader gameHeader = prepareCampaign(mapHeader);
	int end=initGame(mapHeader, gameHeader);
	return end;
}



int Engine::initCustom(void)
{
	CustomGameScreen customGameScreen;

	int cgs=customGameScreen.execute(globalContainer->gfx, 40);

	if (cgs==CustomGameScreen::CANCEL)
		return EE_CANCEL;
	if (cgs==-1)
		return -1;
		
	int teamColor=customGameScreen.getSelectedColor(0);
	gui.localPlayer=0;
	gui.localTeamNo=teamColor;

	int ret = initGame(customGameScreen.getMapHeader(), customGameScreen.getGameHeader());
	if(ret != EE_NO_ERROR)
		return EE_CANT_LOAD_MAP;

	// set the correct alliance
	gui.game.setAIAlliance();

	return EE_NO_ERROR;
}

int Engine::initCustom(const std::string &gameName)
{
	MapHeader mapHeader = loadMapHeader(gameName);
	GameHeader gameHeader = loadGameHeader(gameName);

	// If the game is a network saved game, we need to toogle net players to ai players:
	for (int p=0; p<gameHeader.getNumberOfPlayers(); p++)
	{
		if (verbose)
			printf("Engine::initCustom::player[%d].type=%d.\n", p, gameHeader.getBasePlayer(p).type);
		if (gameHeader.getBasePlayer(p).type==BasePlayer::P_IP)
		{
			gameHeader.getBasePlayer(p).makeItAI(AI::toggleAI);
			if (verbose)
				printf("Engine::initCustom::net player (id %d) was made ai.\n", p);
		}
	}

	int ret = initGame(mapHeader, gameHeader);
	if(ret != EE_NO_ERROR)
		return EE_CANT_LOAD_MAP;

	// set the correct alliance
	gui.game.setAIAlliance();

	return EE_NO_ERROR;
}

int Engine::initLoadGame()
{
	ChooseMapScreen loadGameScreen("games", "game", true);;
	int lgs = loadGameScreen.execute(globalContainer->gfx, 40);
	if (lgs == ChooseMapScreen::CANCEL)
		return EE_CANCEL;

	return initCustom(loadGameScreen.getMapHeader().getFileName());
}



int Engine::run(void)
{
	bool doRunOnceAgain=true;
	
	// Stop menu music, load game music
	if (globalContainer->runNoX)
		assert(globalContainer->mix==NULL);
	else
	{
		globalContainer->mix->setNextTrack(2, true);
	}
	if (globalContainer->runNoX)
	{
		printf("nox::game started\n");
		noxStartTick = SDL_GetTicks();
	}
	
	while (doRunOnceAgain)
	{
		const int speed=40;
		Uint32 startTick, endTick;
		bool networkReadyToExecute = true;
		Sint32 ticksSpentInComputation = speed;
		Sint32 computationAvailableTicks = 0;
		Sint32 ticksToDelayInside = 0;
		Sint32 missedTicksToWait = 0;
		unsigned frameNumber = 0;
		
		startTick = SDL_GetTicks();
		while (gui.isRunning)
		{
			// We always allow the user to use the gui:
			if (globalContainer->runNoX)
			{
				if (!gui.getLocalTeam()->isAlive)
				{
					printf("nox::gui.localTeam is dead\n");
					gui.isRunning = false;
					noxEndTick = SDL_GetTicks();
				}
				else if (gui.getLocalTeam()->hasWon)
				{
					printf("nox::gui.localTeam has won\n");
					gui.isRunning = false;
					noxEndTick = SDL_GetTicks();
				}
				else if (gui.game.totalPrestigeReached)
				{
					printf("nox::gui.game.totalPrestigeReached\n");
					gui.isRunning = false;
					noxEndTick = SDL_GetTicks();
				}
			}
			else
				gui.step();
			
			Sint32 ticksDelayedInside=0;
			if (!gui.hardPause)
			{
				// But some jobs have to be executed synchronously:
				if (networkReadyToExecute)
				{
					gui.syncStep();
					
					
					// We get and push local orders
					net->pushOrder(gui.getOrder(), gui.localPlayer);
					
					// We store full recursive checkSums data:
//					gui.game.checkSum(net->getCheckSumsVectorsStorage(), net->getCheckSumsVectorsStorageForBuildings(), net->getCheckSumsVectorsStorageForUnits());

					// we get and push ai orders
					for (int i=0; i<gui.game.gameHeader.getNumberOfPlayers(); i++)
						if (gui.game.players[i]->ai)
						{
							shared_ptr<Order> order=gui.game.players[i]->ai->getOrder(gui.gamePaused);
							net->pushOrder(order, i);
						}
/*
					
					ticksToDelayInside=net->ticksToDelayInside();
					ticksDelayedInside=ticksToDelayInside+computationAvailableTicks/2;
					ticksDelayedInside=(ticksDelayedInside/10)*10; //SDL_Delay() is only 10[ms] acurate
					if (ticksDelayedInside>0)
						SDL_Delay(ticksDelayedInside);//Here we may need to wait a bit more, to wait other computers which are slower.
					else
*/
						ticksDelayedInside=0;
				}
				else
					ticksDelayedInside=0;
				
				// We proceed network:
				networkReadyToExecute=net->allOrdersRecieved();

				// We get all currents orders from the network and execute them:
				for (int i=0; i<gui.game.gameHeader.getNumberOfPlayers(); i++)
				{
					shared_ptr<Order> order=net->retrieveOrder(i);
					gui.executeOrder(order);
				}
				net->advanceStep();

				// here we do the real work
				if (networkReadyToExecute && !gui.gamePaused && !gui.hardPause)
					gui.game.syncStep(gui.localTeamNo);
			}

			if (globalContainer->runNoX)
			{
				if (cpuSumCountStats < (unsigned)-1)
					cpuSumCountStats++;
				if ((int)cpuSumCountStats == globalContainer->runNoXCountSteps)
				{
					gui.isRunning = false;
					noxEndTick = SDL_GetTicks();
					printf("nox::gui.game.checkSum() = %08x\n", gui.game.checkSum());
				}
			}
			else
			{
				// we draw
				gui.drawAll(gui.localTeamNo);
				globalContainer->gfx->nextFrame();
				
				// if required, save videoshot
				if (!(globalContainer->videoshotName.empty()) && 
					!(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
					)
				{
					FormatableString fileName = FormatableString("videoshots/%0.%1.bmp").arg(globalContainer->videoshotName).arg(frameNumber++, 10, 10, '0');
					printf("printing video shot %s\n", fileName.c_str());
					globalContainer->gfx->printScreen(fileName.c_str());
				}
	
				// we compute timing
				endTick=SDL_GetTicks();
				Sint32 spentTicks=endTick-startTick;
				ticksSpentInComputation=spentTicks-ticksDelayedInside;
				computationAvailableTicks=speed-ticksSpentInComputation;
				Sint32 ticksToWait=computationAvailableTicks+ticksToDelayInside+missedTicksToWait;
				if (ticksToWait>0)
				{
					missedTicksToWait=ticksToWait%10;
					ticksToWait=ticksToWait-missedTicksToWait; //SDL_Delay() is only 10[ms] acurate
					if (ticksToWait>0)
						SDL_Delay(ticksToWait);
				}
				else
					missedTicksToWait=0;
				startTick=SDL_GetTicks();
				
				// we set CPU stats
//				net->setLeftTicks(computationAvailableTicks);//We may have to tell others IP players to wait for our slow computer.
				gui.setCpuLoad(ticksSpentInComputation);
				if (networkReadyToExecute && !gui.gamePaused)
				{
					Sint32 i = computationAvailableTicks;
					if (cpuSumCountStats < (unsigned)-1)
					{
						cpuSumStats += speed - computationAvailableTicks;
						cpuSumCountStats++;
					}
					if (i<0)
						i=0;
					else if (i>=speed)
						i=speed;
					cpuStats[i]++;
				}
			}
		}

		delete net;
		net=NULL;
		
		if (gui.exitGlobCompletely)
			return -1; // There is no bypass for the "close window button"

	
		doRunOnceAgain=false;
		
		if (gui.toLoadGameFileName[0])
		{
			int rv=initCustom(gui.toLoadGameFileName);
			if (rv==EE_NO_ERROR)
				doRunOnceAgain=true;
			gui.toLoadGameFileName[0]=0; // Avoid the communication system between GameGUI and Engine to loop.
		}
	}
	
	if (globalContainer->runNoX)
		return -1;
	else
	{
		// Restart menu music
		assert(globalContainer->mix);
		globalContainer->mix->setNextTrack(1);
		
		// Display End Game Screen
		EndGameScreen endGameScreen(&gui);
		int result = endGameScreen.execute(globalContainer->gfx, 40);
		
		// Return
		return (result == -1) ? -1 : EE_NO_ERROR;
	}
}



int Engine::initGame(MapHeader& mapHeader, GameHeader& gameHeader)
{
	if (!gui.loadFromHeaders(mapHeader, gameHeader))
		return EE_CANT_LOAD_MAP;

	// if this has campaign text information, show a screen for it.
	if (gui.game.campaignText.length() > 0)
	{
		CampaignScreen campaignScreen(gui.game.campaignText);
		int retVal = campaignScreen.execute(globalContainer->gfx, 40);
		if (retVal)
			return EE_CANCEL;
	}
	
	// We remove uncontrolled stuff from map
	gui.game.clearingUncontrolledTeams();

	// We do some cosmetic fix
	finalAdjustements();

	// we create the net game
	net=new NetEngine(gui.game.gameHeader.getNumberOfPlayers());

	return EE_NO_ERROR;
}



GameHeader Engine::prepareCampaign(MapHeader& mapHeader)
{
	GameHeader gameHeader;

	// We make a player for each team in the mapHeader
	int playerNumber=0;
	// Incase there are multiple "humans" selected, only the first will actually become human
	bool wasHuman=false;
	// Each team has a variable, type, that designates whether it is a human or an AI in 
	// a campaign match.
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		if (mapHeader.getBaseTeam(i).type==BaseTeam::T_HUMAN && !wasHuman)
		{
			std::string name = FormatableString("Player %0").arg(playerNumber);
			gameHeader.getBasePlayer(i) = BasePlayer(playerNumber, name.c_str(), i, BasePlayer::P_LOCAL);
			wasHuman=true;
		}
		else if (mapHeader.getBaseTeam(i).type==BaseTeam::T_AI || wasHuman)
		{
			std::string name = FormatableString("AI Player %0").arg(playerNumber);
			gameHeader.getBasePlayer(i) = BasePlayer(playerNumber, name.c_str(), i, BasePlayer::P_AI);
		}
		playerNumber+=1;
	}
	
	gameHeader.setNumberOfPlayers(playerNumber);

	return gameHeader;
}

bool Engine::loadGame(const std::string &filename)
{
	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "Engine::loadGame(\"" << filename << "\") : error, can't open file." << std::endl;
		delete stream;
		return false;
	}
	else
	{
		bool res = gui.load(stream);
		delete stream;
		if (!res)
		{
			std::cerr << "Engine::loadGame(\"" << filename << "\") : error, can't load game." << std::endl;
			return false;
		}
	}

	if (verbose)
		std::cout << "Engine::loadGame(\"" << filename << "\") : game successfully loaded." << std::endl;
	return true;
}



MapHeader Engine::loadMapHeader(const std::string &filename)
{
	MapHeader mapHeader;
	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "Engine::loadMapHeader : error, can't open file " << filename  << std::endl;
	}
	else
	{
		if (verbose)
			std::cout << "Engine::loadMapHeader : loading map " << filename << std::endl;
		bool validMapSelected = mapHeader.load(stream);
		if (!validMapSelected)
			std::cerr << "Engine::loadMapHeader : invalid map header for map " << filename << std::endl;
	}
	delete stream;
	return mapHeader;
}



GameHeader Engine::loadGameHeader(const std::string &filename)
{
	MapHeader mapHeader;
	GameHeader gameHeader;
	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "Engine::loadGameHeader : error, can't open file " << filename  << std::endl;
	}
	else
	{
		if (verbose)
			std::cout << "Engine::loadGameHeader : loading map " << filename << std::endl;
		mapHeader.load(stream);
		bool validMapSelected = gameHeader.load(stream, mapHeader.getVersionMinor());
		if (!validMapSelected)
			std::cerr << "Engine::loadGameHeader : invalid game header for map " << filename << std::endl;
	}
	delete stream;
	return gameHeader;

}



void Engine::finalAdjustements(void)
{
	gui.adjustLocalTeam();
	if (!globalContainer->runNoX)
	{
		gui.game.renderMiniMap(gui.localTeamNo);
		gui.adjustInitialViewport();
	}
}
