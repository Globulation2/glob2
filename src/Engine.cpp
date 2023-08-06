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

#include <FileManager.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <FormatableString.h>

#include "AINames.h"
#include "CustomGameScreen.h"
#include "EndGameScreen.h"
#include "Engine.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Utilities.h"
#include "YOGClientLobbyScreen.h"
#include "SoundMixer.h"
#include "Player.h"
#include "NetMessage.h"
#include "GameGUIDialog.h"
#include "GUIMessageBox.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include "SDLCompat.h"

#include <iostream>


Engine::Engine()
{
	net=NULL;
	logFile = globalContainer->logFileManager->getFile("Engine.log");
}

Engine::~Engine()
{
	fprintf(logFile, "\n");

	if (net)
	{
		delete net;
		net=NULL;
	}
}



int Engine::initCampaign(const std::string &mapName, Campaign& campaign, const std::string& missionName)
{
	MapHeader mapHeader = loadMapHeader(mapName);
	GameHeader gameHeader = loadGameHeader(mapName);
	if(gameHeader.getNumberOfPlayers() == 0)
	{
		gameHeader = prepareCampaign(mapHeader, gui.localPlayer, gui.localTeamNo);
	}
	else
	{
		gui.localPlayer = 0;
		gui.localTeamNo = gameHeader.getBasePlayer(0).teamNumber;
	}
	
	gameHeader.getBasePlayer(0).name = campaign.getPlayerName();
	
	int end=initGame(mapHeader, gameHeader);
	gui.setCampaignGame(campaign, missionName);
	return end;
}



int Engine::initCampaign(const std::string &mapName)
{
	MapHeader mapHeader = loadMapHeader(mapName);
	GameHeader gameHeader = loadGameHeader(mapName);
	if(gameHeader.getNumberOfPlayers() == 0)
	{
		gameHeader = prepareCampaign(mapHeader, gui.localPlayer, gui.localTeamNo);
	}
	else
	{
		gui.localPlayer = 0;
		gui.localTeamNo = gameHeader.getBasePlayer(0).teamNumber;
	}
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
	else if(ret == -1)
		return -1;

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

	int ret = initGame(mapHeader, gameHeader, true, false, true);
	if(ret != EE_NO_ERROR)
		return EE_CANT_LOAD_MAP;
	else if(ret == -1)
		return -1;

	return EE_NO_ERROR;
}

int Engine::initLoadGame()
{
	ChooseMapScreen loadGameScreen("games", "game", true, "replays", "replay", false);
	int lgs = loadGameScreen.execute(globalContainer->gfx, 40);
	if (lgs == ChooseMapScreen::CANCEL)
		return EE_CANCEL;
	else if(lgs == -1)
		return -1;

	assert(loadGameScreen.getSelectedType() != ChooseMapScreen::NONE);
	assert(loadGameScreen.getSelectedType() != ChooseMapScreen::MAP);

	if (loadGameScreen.getSelectedType() == ChooseMapScreen::GAME)
		return initCustom(loadGameScreen.getMapHeader().getFileName());
	else if (loadGameScreen.getSelectedType() == ChooseMapScreen::REPLAY)
		return loadReplay(loadGameScreen.getMapHeader().getFileName(false,true));
	else
		assert(false);
}

int Engine::initMultiplayer(boost::shared_ptr<MultiplayerGame> multiplayerGame, boost::shared_ptr<YOGClient> client, int localPlayer)
{
	gui.localPlayer = localPlayer;
	gui.localTeamNo = multiplayerGame->getGameHeader().getBasePlayer(localPlayer).teamNumber;
	multiplayer = multiplayerGame;
	initGame(multiplayerGame->getMapHeader(), multiplayerGame->getGameHeader(), true, true);
	multiplayer->setNetEngine(net);

	for (int p=0; p<multiplayerGame->getGameHeader().getNumberOfPlayers(); p++)
	{
		if (multiplayerGame->getGameHeader().getBasePlayer(p).type==BasePlayer::P_IP)
		{
			net->prepareForLatency(p, multiplayerGame->getGameHeader().getGameLatency());
		}
	}

	net->setNetworkInfo(multiplayerGame->getGameHeader().getOrderRate(), client->getGameConnection());

	return Engine::EE_NO_ERROR;
}



void Engine::createRandomGame()
{
	bool validMapChosen = false;
	MapHeader map;

	while (!validMapChosen)
	{
		try
		{
			map = chooseRandomMap();
			validMapChosen = true;
		}
		catch (std::ios_base::failure &e)
		{
			validMapChosen = false;
		}
	}
	
	std::cout<<"Randomly Chosen Map: "<<map.getMapName()<<std::endl;
	
	GameHeader game = createRandomGame(map.getNumberOfTeams());
	std::cout<<"Random Seed gameheader: "<<game.getRandomSeed();
	for (int p=0; p<game.getNumberOfPlayers(); p++)
	{
		std::cout<<"    Player: "<<game.getBasePlayer(p).name<<" for team "<<game.getBasePlayer(p).teamNumber<<std::endl;
	}
	
	gui.localPlayer=0;
	gui.localTeamNo=0;
	
	initGame(map, game);
}



bool Engine::haveMap(const MapHeader& mapHeader)
{
	// FIXME: This is a fairly ugly way to test if the file exists
	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapHeader.getFileName()));
	if (stream->isEndOfStream())
	{
		delete stream;
		return false;
	}
	delete stream;
	MapHeader mh = loadMapHeader(mapHeader.getFileName());
	if(mh != mapHeader)
		return false;
	return true;
}



int Engine::run(void)
{
	bool doRunOnceAgain=true;
	if (globalContainer->runNoX)
	{
		assert(globalContainer->mix==NULL);
		printf("nox::game started\n");
		automaticGameStartTick = SDL_GetTicks64();
	}
	else
	{
		// look for all available musics
		globalContainer->fileManager->initDirectoryListing("data/zik/", "", true);
		std::string filename;
		std::vector<std::string> musicDirs;
		while (!(filename = globalContainer->fileManager->getNextDirectoryEntry()).empty())
		{
			if (globalContainer->fileManager->isDir(FormatableString("%0/%1").arg("data/zik/").arg(filename)))
			{
				std::cerr << "music dir found: " << filename << std::endl;
				musicDirs.push_back(filename);
			}
		}
		
		// select a music randomly
		// FIXME: implement more intelligent music choosing policy
		if (!musicDirs.empty())
		{
			size_t musicIndex(rand() % musicDirs.size());
			const std::string& musicDir(musicDirs[musicIndex]);
			std::cerr << "selecting music dir " << musicDir << std::endl;
			globalContainer->mix->loadTrack(FormatableString("data/zik/%0/a1.ogg").arg(musicDir), 2);
			globalContainer->mix->loadTrack(FormatableString("data/zik/%0/a2.ogg").arg(musicDir), 3);
			globalContainer->mix->loadTrack(FormatableString("data/zik/%0/a3.ogg").arg(musicDir), 4);
		}
		else
		{
			std::cerr << "Warning, no music found!" << std::endl;
		}
		
		// Stop menu music, load game music
		globalContainer->mix->setNextTrack(2, true);
		globalContainer->gfx->cursorManager.setDrawColor(gui.getLocalTeam()->color);
	}
	
	while (doRunOnceAgain)
	{
		int speed=40;
		bool networkReadyToExecute = true;
		
		// If playing in fast-forward, we process the GUI and draw everything only once every 3 game-steps
		// This way, the overall fps stays about the same
		int nextGuiStep = 1;
		
		cpuStats.reset(speed);
		
		Sint64 needToBeTime = 0;
		Sint64 startTime = SDL_GetTicks64();
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
				Sint64 currentTime = SDL_GetTicks64() - startTime;
				//if we are more than 500 milliseconds behind where we should be,
				//then truncate it. This is to avoid playing "catchup" for long
				//periods of time if Glob2 recieved allmost no cpu time
				if(  (currentTime - needToBeTime) > 500)
					needToBeTime = currentTime - 500;

				//Any inconsistancies in the delays will be smoothed throughout the following frames,
				Sint64 delay = std::max<Sint64>(0, needToBeTime - currentTime);
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

		delete net;
		net=NULL;
		multiplayer.reset();
		
		if (gui.exitGlobCompletely)
			return -1; // There is no bypass for the "close window button"

		
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
	
	if(!globalContainer->runNoX)
	{
	}
	
	if (globalContainer->runNoX || globalContainer->automaticEndingGame)
	{
		if(!globalContainer->runNoX)
			globalContainer->gfx->cursorManager.setDefaultColor();
		return -1;
	}
	else
	{
		// Restart menu music
		assert(globalContainer->mix);
		globalContainer->mix->setNextTrack(1, true);
		
		// Display End Game Screen
		EndGameScreen endGameScreen(&gui);
		int result = endGameScreen.execute(globalContainer->gfx, 40);
		
		// Return to default color
		globalContainer->gfx->cursorManager.setDefaultColor();
		
		// Return
		return (result == -1) ? -1 : EE_NO_ERROR;
	}
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

		bool validMapSelected;

		try
		{
			validMapSelected = mapHeader.load(stream);
		}
		catch (std::ios_base::failure &e)
		{
			// Notify what filename couldn't load, because if we're doing -test-games(-nox) and loading the map fails,
			// the map name won't be saved inside mapHeader.
			std::cerr << "Engine::loadMapHeader : can't load map \"" << filename << "\": bad format" << std::endl;

			// We didn't solve the problem though, so we re-throw
			throw;
		}

		if (!validMapSelected)
			std::cerr << "Engine::loadMapHeader : invalid map header for map " << filename << std::endl;
	}
	delete stream;
	
	//Map name is the filename without underscores or .map, it has to be updated in case the map file itself was renamed
	std::string mapName;
	if(mapHeader.getIsSavedGame())
		mapName=filename.substr(filename.find("/")+1, filename.size()-6-filename.find("/"));
	else
		mapName=filename.substr(filename.find("/")+1, filename.size()-5-filename.find("/"));
	size_t pos = mapName.find("_");
	while(pos != std::string::npos)
	{
		mapName.replace(pos, 1, " ");
		pos = mapName.find("_");
	}
	mapHeader.setMapName(glob2FilenameToName(filename));
	
	return mapHeader;
}



GameHeader Engine::loadGameHeader(const std::string &filename)
{
	MapHeader mapHeader;
	GameHeader gameHeader;
	std::unique_ptr<InputStream> stream = std::make_unique<BinaryInputStream>(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "Engine::loadGameHeader : error, can't open file " << filename  << std::endl;
		return GameHeader(); // an empty game header
	}
	else
	{
		if (verbose)
			std::cout << "Engine::loadGameHeader : loading map " << filename << std::endl;
		bool headerValid = mapHeader.load(stream.get());
		bool validMapSelected = gameHeader.load(stream.get(), mapHeader.getVersionMinor());
		if (!headerValid || !validMapSelected)
		{
			std::cerr << "Engine::loadGameHeader : invalid game header for map " << filename << std::endl;
			return GameHeader();
		}
	}
	return gameHeader;

}



int Engine::initGame(MapHeader& mapHeader, GameHeader& gameHeader, bool setGameHeader, bool ignoreGUIData, bool saveAI)
{
	bool error = false;
	try
	{
		error = !gui.loadFromHeaders(mapHeader, gameHeader, setGameHeader, ignoreGUIData, saveAI);
	}
	catch (std::exception &e)
	{
		std::cerr << "Failed to load the map: exception received." << std::endl;
		error = true;
	}
	if (error) {
		if (!globalContainer->runNoX)
		{
			// Display an error message
			GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[ERROR_CANT_LOAD_MAP]"), Toolkit::getStringTable()->getString("[ok]"));
		}
		return EE_CANT_LOAD_MAP;
	}

	// We remove uncontrolled stuff from map
	gui.game.clearingUncontrolledTeams();

	// We do some cosmetic fix
	finalAdjustements();

	// we create the net game
	net=new NetEngine(gui.game.gameHeader.getNumberOfPlayers(), gui.localPlayer);

	// Initialise the replay writer, unless we're showing a replay
	if (!globalContainer->replaying)
	{
		assert(globalContainer->replayWriter == NULL);
		globalContainer->replayWriter = new ReplayWriter();
		globalContainer->replayWriter->init("replays/last_game.replay", gui);
	}

	return EE_NO_ERROR;
}



GameHeader Engine::prepareCampaign(MapHeader& mapHeader, int& localPlayer, int& localTeam)
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
			localPlayer = playerNumber;
			localTeam = i;
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
	if(!wasHuman)
	{
		localPlayer = 0;
		localTeam = gameHeader.getBasePlayer(0).teamNumber;
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



MapHeader Engine::chooseRandomMap()
{
	std::vector<std::string> maps;

	std::string fullDir = "maps";

	// we add the other files
	if (Toolkit::getFileManager()->initDirectoryListing(fullDir.c_str(), "map", false))
	{
		std::string fileName;
		while (!(fileName = (Toolkit::getFileManager()->getNextDirectoryEntry())).empty())
		{
			std::string fullFileName = fullDir + DIR_SEPARATOR + fileName;
			maps.push_back(fullFileName);
		}
	}
	
	int number = syncRand() % maps.size();
	
	return loadMapHeader(maps[number]);
}



GameHeader Engine::createRandomGame(int numberOfTeams)
{
	GameHeader gameHeader;
	int count = 0;
	for (int i=0; i<numberOfTeams+1; i++)
	{
		int teamColor=(i % numberOfTeams);
		if (i==0)
		{
			gameHeader.getBasePlayer(count) = BasePlayer(0, globalContainer->settings.getUsername(), teamColor, BasePlayer::P_LOCAL);
		}
		else
		{
			AI::ImplementitionID iid=static_cast<AI::ImplementitionID>(syncRand() % 5 + 1);
			FormatableString name("%0 %1");
			name.arg(AINames::getAIText(iid)).arg(i-1);
			gameHeader.getBasePlayer(count) = BasePlayer(i, name.c_str(), teamColor, Player::playerTypeFromImplementitionID(iid));
		}
		gameHeader.setAllyTeamNumber(teamColor, teamColor);
		count+=1;
	}
	gameHeader.setNumberOfPlayers(count);
	return gameHeader;
}

int Engine::loadReplay(const std::string &fileName)
{
	// Let globalContainer know what we are doing
	globalContainer->replaying = true;
	globalContainer->replayFileName = fileName;
	
	// Reset the replay's options
	gui.localPlayer = 0;
	gui.localTeamNo = 0;
	globalContainer->replayVisibleTeams = 0xFFFFFFFF;
	globalContainer->replayFastForward = false;

	// Initialize the ReplayReader in GlobalContainer
	globalContainer->replayReader = new ReplayReader();
	bool replayLoaded = globalContainer->replayReader->loadReplay(fileName);

	// If the reader found that the replay isn't valid, show an error message and return
	if (!replayLoaded)
	{
		if (!globalContainer->runNoX)
		{
			// Display an error message
			GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[ERROR_CANT_LOAD_MAP]"), Toolkit::getStringTable()->getString("[ok]"));
		}

		delete globalContainer->replayReader;
		globalContainer->replayReader = NULL;
		return EE_CANT_LOAD_MAP;
	}

	assert(globalContainer->replayReader->isValid());

	// Load the map and settings.
	MapHeader mapHeader = loadMapHeader(fileName);
	GameHeader gameHeader = loadGameHeader(fileName);

	// Set all players to a AINone
	for (int p=0; p<gameHeader.getNumberOfPlayers(); p++)
	{
		gameHeader.getBasePlayer(p).makeItAI(AI::NONE);
	}

	// Finally, initialise the Game
	int ret = initGame(mapHeader, gameHeader, true, false, true);
	if(ret != EE_NO_ERROR)
		return EE_CANT_LOAD_MAP;
	else if(ret == -1)
		return -1;

	return EE_NO_ERROR;
}

void Engine::finalAdjustements(void)
{
	gui.adjustLocalTeam();
	if (!globalContainer->runNoX)
	{
		gui.adjustInitialViewport();
	}
	gui.game.setAlliances();
}
