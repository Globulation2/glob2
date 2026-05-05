// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FormatableString.h>
#include <GraphicContext.h>

#include "AINames.h"
#include "ChecksumSidecar.h"
#include "DatasetWriter.h"
#include "engine.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "NetMessage.h"
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include "SDLCompat.h"

#include <iostream>


// Body of the outer "play one game and possibly load another" loop in run().
// On entry: the game has been initialised (initGame) and audio/cursor set up.
// On exit: doRunOnceAgain==true means run() should call this again
// (e.g. user picked a save during play); false means run() returns.
void Engine::runOneGameSession(bool& doRunOnceAgain)
{
	int speed=40;
	bool networkReadyToExecute = true;

	// If playing in fast-forward, we process the GUI and draw everything only once every 3 game-steps
	// This way, the overall fps stays about the same
	int nextGuiStep = 1;

	cpuStats.reset(speed);

	Sint64 needToBeTime = 0;
	Uint64 startTime = SDL_GetTicks64();
	unsigned frameNumber = 0;
	bool sendBumpUp=false;

	while (gui.isRunning)
	{
		nextGuiStep--;

		// Set the replay speed
		if (globalContainer->replaying)
		{
			if (globalContainer->replayFastForward && !gui.gamePaused)
			{
				speed = 12;
				if (nextGuiStep < 0) nextGuiStep = 2;
			}
			else
			{
				speed = 40;
				if (nextGuiStep < 0) nextGuiStep = 0;
			}
		}
		else
		{
			// Process the GUI as usual, every step
			nextGuiStep = 0;
		}

		// We always allow the user to use the gui:
		if (globalContainer->automaticEndingGame)
		{
			if (!gui.getLocalTeam()->isAlive && !globalContainer->automaticGameGlobalEndConditions)
			{
				printf("nox::gui.localTeam is dead\n");
				gui.isRunning = false;
				automaticGameEndTick = SDL_GetTicks64();
			}
			else if (gui.getLocalTeam()->hasWon && !globalContainer->automaticGameGlobalEndConditions)
			{
				printf("nox::gui.localTeam has won\n");
				gui.isRunning = false;
				automaticGameEndTick = SDL_GetTicks64();
			}
			else if (gui.game.totalPrestigeReached)
			{
				printf("nox::gui.game.totalPrestigeReached\n");
				gui.isRunning = false;
				automaticGameEndTick = SDL_GetTicks64();
			}
			else if (gui.game.isGameEnded)
			{
				printf("nox::gui.game.isGameEnded\n");
				gui.isRunning = false;
				automaticGameEndTick = SDL_GetTicks64();
			}
		}
		if(!globalContainer->runNoX && nextGuiStep == 0)
			gui.step();

		if (!gui.hardPause)
		{
			if(multiplayer && multiplayer->getMultiplayerMode() == MultiplayerGame::NoMode)
			{
				gui.isRunning = false;
			}

			// But some jobs have to be executed synchronously:
			if (networkReadyToExecute)
			{
				gui.syncStep();

				// The gui.localPlayer may have been updated (in replays)
				// Keep them synchronized here
				net->setLocalPlayer(gui.localPlayer);

				// We get and push local orders
				shared_ptr<Order> localOrder = gui.getOrder();
				net->addLocalOrder(localOrder);
			}

			// we get and push ai orders, if they are needed for this frame
			for (int i=0; i<gui.game.gameHeader.getNumberOfPlayers(); i++)
			{
				if (gui.game.players[i]->ai && !net->orderRecieved(i))
				{
					shared_ptr<Order> order=gui.game.players[i]->ai->getOrder(gui.gamePaused);
					net->pushOrder(order, i, true);
				}
			}

			gui.game.setWaitingOnMask(net->getWaitingOnMask());

			if(multiplayer)
				multiplayer->update();

			if(networkReadyToExecute)
			{
				Uint32 checksum = gui.game.checkSum(NULL, NULL, NULL);
				net->advanceStep(checksum);

				// Enable this to do test if checksums in the replay match
				//if (globalContainer->replayReader) globalContainer->replayReader->setCheckSum(checksum);
				if (globalContainer->replayWriter) globalContainer->replayWriter->setCheckSum(checksum);

				if (checksumSidecar)
					checksumSidecar->writeTick(gui.game.stepCounter, gui.game);
			}

			// We proceed network:
			networkReadyToExecute=net->allOrdersRecieved();


			if(networkReadyToExecute)
			{
				sendBumpUp=false;
				if(!net->matchCheckSums())
				{
					std::cout<<"Game desychronized."<<std::endl;
					gui.game.dumpAllData("glob2.world-desynchronization.dump.txt");
					assert(false);
				}
				else
				{
					// We get all currents orders from the network and execute them:
					for (int i=0; i<gui.game.gameHeader.getNumberOfPlayers(); i++)
					{
						shared_ptr<Order> order=net->retrieveOrder(i);
						if (!globalContainer->replaying)
						{
							gui.executeOrder(order);
						}
						else if (order->getOrderType() == ORDER_PLAYER_QUIT_GAME ||
						         order->getOrderType() == ORDER_PAUSE_GAME)
						{
							gui.executeOrder(order);
						}
					}
					net->clearTopOrders();
				}
			}
			/*
			//The network latency bump-up has been disabled for beta 4 release
			else if(!sendBumpUp)
			{
				sendBumpUp=true;
				net->increaseLatencyAdjustment();
			}
			*/

			// Load the replay's orders
			if (globalContainer->replaying)
			{
				assert(globalContainer->replayReader);
				assert(globalContainer->replayReader->isValid());

				while (globalContainer->replayReader->hasMoreOrdersThisStep())
				{
					shared_ptr<Order> order = globalContainer->replayReader->retrieveOrder();

					if (order->getOrderType() != ORDER_PLAYER_QUIT_GAME &&
					    order->getOrderType() != ORDER_PAUSE_GAME &&
					    order->getOrderType() != ORDER_NULL)
					{
						gui.executeOrder(order);
					}
				}

				if (globalContainer->replayReader->isFinished())
				{
					gui.showEndOfReplayScreen();
				}
			}

			// here we do the real work
			if (networkReadyToExecute && !gui.gamePaused && !gui.hardPause)
			{
				if (globalContainer->replaying)
				{
					assert(globalContainer->replayReader);
					globalContainer->replayReader->advanceStep();
				}

				gui.game.syncStep(gui.localTeamNo);
			}
		}

		if (globalContainer->automaticEndingGame)
		{
			if ((int)gui.game.stepCounter == globalContainer->automaticEndingSteps)
			{
				gui.isRunning = false;
				automaticGameEndTick = SDL_GetTicks64();
				printf("nox::gui.game.checkSum() = %08x\n", gui.game.checkSum());
			}
		}
		if(!globalContainer->runNoX)
		{
			if (nextGuiStep == 0)
			{
				// we draw
				gui.drawAll(gui.localTeamNo);
				globalContainer->gfx->nextFrame();
			}

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
			needToBeTime += speed;
			Sint64 currentTime = static_cast<Sint64>(SDL_GetTicks64()) - static_cast<Sint64>(startTime);
			//if we are more than 500 milliseconds behind where we should be,
			//then truncate it. This is to avoid playing "catchup" for long
			//periods of time if Glob2 recieved allmost no cpu time
			if((currentTime - needToBeTime) > 500)
				needToBeTime = currentTime - 500;

			//Any inconsistancies in the delays will be smoothed throughout the following frames,
			Uint64 delay = std::max<Sint64>(0, needToBeTime - currentTime);
			SDL_Delay(delay);

			// we set CPU stats
//				net->setLeftTicks(computationAvailableTicks);//We may have to tell others IP players to wait for our slow computer.
			gui.setCpuLoad((4000-(delay*100)) / 40);
			if (networkReadyToExecute && !gui.gamePaused)
			{
				cpuStats.addFrameData(delay);
			}
		}

		if(gui.flushOutgoingAndExit)
		{
			shared_ptr<Order> localOrder = gui.getOrder();
			while(localOrder->getOrderType() != ORDER_NULL)
			{
				net->addLocalOrder(localOrder);
				localOrder = gui.getOrder();
			}

			gui.isRunning=false;
			net->flushAllOrders();
			break;
		}
	}

	if(globalContainer->automaticEndingGame)
	{
		int time = gui.game.stepCounter;
		int seconds = (time / 25) % 60;
		int minutes = (time / 25) / 60;
		std::cout<< "automaticEndingGame ended: "<<time<<" ticks, "<<minutes<<" minutes, "<<seconds<<" seconds"<<std::endl;

		// Machine-parseable summary line for the AI-trainer pipeline (and any
		// external driver scraping headless output). One line, key=value pairs,
		// space-separated. Winner is the first team with hasWon set, else -1
		// (timeout / no winner).
		int winnerTeam = -1;
		for (int t = 0; t < gui.game.mapHeader.getNumberOfTeams(); t++)
		{
			if (gui.game.teams[t] && gui.game.teams[t]->hasWon)
			{
				winnerTeam = t;
				break;
			}
		}
		Uint32 orders = globalContainer->replayWriter
			? globalContainer->replayWriter->getOrderCount() : 0;
		std::cout << "GLOB2_GAME_END ticks=" << time
			<< " winner_team=" << winnerTeam
			<< " seed=" << gui.game.gameHeader.getRandomSeed()
			<< " map=\"" << gui.game.mapHeader.getMapName() << "\""
			<< " orders=" << orders
			<< " players=";
		for (int p = 0; p < gui.game.gameHeader.getNumberOfPlayers(); p++)
		{
			const BasePlayer& bp = gui.game.gameHeader.getBasePlayer(p);
			if (p > 0) std::cout << ",";
			std::cout << "team" << bp.teamNumber << ":";
			if (bp.type == BasePlayer::P_LOCAL)
				std::cout << "local";
			else if (bp.type == BasePlayer::P_IP)
				std::cout << "ip";
			else if (bp.type >= BasePlayer::P_AI)
				std::cout << AINames::getAIText(BasePlayer::implementitionIdFromPlayerType(bp.type));
			else
				std::cout << "none";
		}
		std::cout << std::endl;
	}

	cpuStats.format();

	if(multiplayer)
	{
		if (gui.game.totalPrestigeReached)
		{
			Team *t=gui.game.getTeamWithMostPrestige();
			assert(t);
			if (t==gui.getLocalTeam())
			{
				multiplayer->setGameResult(YOGGameResultWonGame);
			}
			else
			{
				if ((t->allies) & (gui.getLocalTeam()->me))
					multiplayer->setGameResult(YOGGameResultWonGame);
				else
					multiplayer->setGameResult(YOGGameResultLostGame);
			}
		}
		else if(gui.getLocalTeam()->hasWon)
		{
			multiplayer->setGameResult(YOGGameResultWonGame);
		}
		else if (!gui.getLocalTeam()->isAlive)
		{
			multiplayer->setGameResult(YOGGameResultLostGame);
		}
		else if (!gui.game.isGameEnded)
		{
			multiplayer->setGameResult(YOGGameResultQuitGame);
		}
	}

	if (checksumSidecar)
	{
		checksumSidecar->close();
		delete checksumSidecar;
		checksumSidecar = NULL;
	}

	if (globalContainer->datasetWriter)
	{
		globalContainer->datasetWriter->close();
		delete globalContainer->datasetWriter;
		globalContainer->datasetWriter = NULL;
	}

	delete net;
	net=NULL;
	multiplayer.reset();

	if (gui.exitGlobCompletely)
	{
		doRunOnceAgain = false;
		return; // There is no bypass for the "close window button"
	}


	doRunOnceAgain=false;

	if (gui.toLoadGameFileName[0])
	{
		int rv;

		if (globalContainer->replaying) rv = loadReplay(gui.toLoadGameFileName);
		else rv = initCustom(gui.toLoadGameFileName);

		if (rv==EE_NO_ERROR)
			doRunOnceAgain=true;
		gui.toLoadGameFileName[0]=0; // Avoid the communication system between GameGUI and Engine to loop.
	}
}
