// SPDX-License-Identifier: GPL-3.0-or-later
// Two real clients and an embedded anonymous LAN service; optional recording on each.
#include "GlobalContainer.h"
#include "Engine.h"
#include "MultiplayerGame.h"
#include "MultiplayerGameScreen.h"
#include "SessionTabsScreen.h"
#include "EndGameScreen.h"
#include "YOGClient.h"
#include "YOGClientGameListManager.h"
#include "YOGServer.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <GameplayRecording.h>
#include <ScreenStack.h>
#include <SDL3_net/SDL_net.h>
#include <filesystem>
#include <iostream>

GlobalContainer *globalContainer = nullptr;
int main(int argc, char **argv)
{
	if (argc != 4)
	{
		std::cerr << "Usage: RecordingMultiplayerPeer host|join endpoint output.mp4\n";
		return 2;
	}
	try
	{
		const bool host = std::string(argv[1]) == "host";
		SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 0);
		SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 0);
		SDL_setenv_unsafe("GLOB2_CHECKSUM_SIDECAR", "1", 1);
		GlobalContainer globals;
		globalContainer = &globals;
		globals.settings.screenWidth = 640;
		globals.settings.screenHeight = 480;
		globals.settings.screenFlags = 0;
		globals.settings.mute = true;
		globals.settings.setUsername(host ? "recording-host" : "recording-guest");
		globals.settings.autosaveGames = false;
		globals.load();
		if (!NET_Init())
			throw std::runtime_error(SDL_GetError());
		auto &recorder = GAGCore::Recording::recorder();
		const char *encoder = SDL_getenv("GLOB2_TEST_FFMPEG");
		if (encoder)
		{
			recorder.options.ffmpeg = encoder;
			recorder.options.fps = 30;
			recorder.options.chapterTicks = 40;
			if (!recorder.start(argv[3]))
				throw std::runtime_error(recorder.status().error);
		}
		{
			auto client = std::make_shared<YOGClient>();
			if (host)
			{
				auto server = std::make_shared<YOGServer>(YOGAnonymousLogin, YOGSingleGame);
				if (!server->isListening())
					throw std::runtime_error("LAN service did not start");
				client->attachGameServer(server);
				client->connect(server->networkConfig().lobbyEndpoint);
				std::cout << "PAIRING " << server->networkConfig().lobbyEndpoint << std::endl;
			}
			else
				client->connect(argv[2]);
			std::shared_ptr<MultiplayerGame> game;
			GAGGUI::ScreenStack screens(*globals.gfx);
			bool ready = false, launch = false;
			auto deadline = SDL_GetTicks() + 30000;
			while (SDL_GetTicks() < deadline)
			{
				client->update();
				if (client->getConnectionState() == YOGClient::WaitingForLoginInformation)
					client->attemptLogin(globals.settings.getUsername());
				if (!game && client->getConnectionState() == YOGClient::ClientOnStandby &&
					(host || !client->getGameListManager()->getGameList().empty()))
				{
					game = std::make_shared<MultiplayerGame>(client);
					client->setMultiplayerGame(game);
					if (host)
					{
						game->createNewGame("Recording fixture");
						MapHeader map = Engine::loadMapHeader("maps/FourSquares1.map");
						game->setMapHeader(map);
					}
					else
						game->joinGame(
							client->getGameListManager()->getGameList().front().getGameID());
					auto lobby = std::make_unique<SessionTabsScreen>();
					// Tab lifetime extends until after its owning stack is stopped below.
					screens.push(std::move(lobby));
				}
				if (game)
				{
					game->update();
					if (game->isFullyInGame() && !ready)
					{
						game->setHumanReady(true);
						ready = true;
					}
					if (host && game->getGameHeader().getNumberOfPlayers() == 2 &&
						game->isGameReadyToStart() && !launch)
					{
						launch = true;
						game->startGame();
					}
					if (game->takeStartRequest())
						break;
				}
				GAGCore::Recording::recorder().screen("multiplayer_game");
				globals.gfx->drawFilledRect(0, 0, 640, 480, GAGCore::Color(30, 25, 40));
				globals.gfx->nextFrame();
				SDL_Delay(10);
			}
			if (!game || !game->isWaitingForEngine())
				throw std::runtime_error("Multiplayer launch timed out");
			// Render the real multiplayer room once initialization is complete.
			MultiplayerGameScreen room(screens, game, client);
			auto lobby = std::make_unique<SessionTabsScreen>();
			lobby->addTab(&room, true);
			screens.push(std::move(lobby));
			screens.frame(SDL_GetTicks(), {});
			Engine engine;
			if (engine.initMultiplayer(game, client, game->getLocalPlayer()) != Engine::EE_NO_ERROR)
				throw std::runtime_error("Multiplayer initialization failed");
			globals.automaticEndingGame = true;
			globals.automaticEndingSteps = 120;
			globals.automaticGameGlobalEndConditions = true;
			engine.prepareRun();
			game->sessionStarted();
			engine.beginSession(SDL_GetTicks());
			if (!engine.startSimulationThread(SDL_GetTicks()))
				throw std::runtime_error("Simulation thread did not start");
			deadline = SDL_GetTicks() + 30000;
			bool running = true;
			while (running && SDL_GetTicks() < deadline)
			{
				// The simulation owns network updates once the engine starts.
				running = engine.threadedClientFrame(SDL_GetTicks(), {});
				engine.drawSession();
				SDL_Delay(10);
			}
			if (running)
				throw std::runtime_error("Multiplayer match timed out");
			engine.finishSession();
			globals.automaticEndingGame = false;
			auto results = engine.endRunScreen();
			if (results)
			{
				results->beginExecution(globals.gfx);
				for (int i = 0; i < 20; ++i)
				{
					results->updateExecution(SDL_GetTicks());
					results->drawExecution();
					SDL_Delay(10);
				}
				results->endExecute(0);
				results->finishExecution();
			}
			screens.stop();
			screens.frame(SDL_GetTicks(), {});
			game->sessionEnded(false);
			game->leaveGame();
			client->setMultiplayerGame({});
			client->disconnect();
		}
		recorder.stop();
		recorder.shutdown();
		if (encoder && recorder.status().state != GAGCore::Recording::State::Complete)
			throw std::runtime_error(recorder.status().error);
		NET_Quit();
		globalContainer = nullptr;
		std::cout << "MULTIPLAYER RECORDING PASS" << std::endl;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
