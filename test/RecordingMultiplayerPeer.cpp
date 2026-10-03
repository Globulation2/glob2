// SPDX-License-Identifier: GPL-3.0-or-later
// Two real clients and an embedded anonymous LAN service; optional recording on each.
#include "GlobalContainer.h"
#include "Engine.h"
#include "RoomScreen.h"
#include "LanRoom.h"
#include "TurnLockstep.h"
#include <fstream>
#include <algorithm>
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
		globals.networkInitialized = true;
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
			std::shared_ptr<Lan::LanRoom> game;
			if (host)
			{
				Lan::LanHost::Options options;
				options.hostName = "recording-host";
				options.mapFile = "maps/FourSquares1.map.gz";
				options.map = Engine::loadMapHeader(options.mapFile);
				options.broadcast = false;
				options.advertisedAddress = "127.0.0.1";
				game = Lan::LanRoom::host(std::move(options));
				std::cout << "PAIRING " << game->shareText() << std::endl;
			}
			else
			{
				Lan::LanClient::Options options;
				options.endpoint = argv[2];
				options.name = "recording-guest";
				game = Lan::LanRoom::join(std::move(options));
			}
			GAGGUI::ScreenStack screens(*globals.gfx);
			bool ready = false, launch = false;
			auto deadline = SDL_GetTicks() + 30000;
			while (!launch && SDL_GetTicks() < deadline)
			{
				game->update();
				if (game->lobbyReady() && !ready)
				{
					game->setReady(true);
					ready = true;
				}
				const auto slots = game->slots();
				const auto humans = std::count_if(slots.begin(), slots.end(), [](const auto &slot) {
					return !slot.open && !slot.ai;
				});
				if (host && humans == 2 && game->canStart() && !game->starting())
					game->start();
				while (auto event = game->takeEvent())
				{
					if (event->kind == RoomBackend::Event::Launch) launch = true;
					if (event->kind == RoomBackend::Event::Finished)
						throw std::runtime_error(event->text);
				}
				recorder.screen("multiplayer_room");
				globals.gfx->drawFilledRect(0, 0, 640, 480, GAGCore::Color(30, 25, 40));
				globals.gfx->nextFrame();
				SDL_Delay(10);
			}
			if (!launch) throw std::runtime_error("Multiplayer launch timed out");
			// Render the production room once both clients have their match setup.
			screens.push(std::make_unique<RoomScreen>(screens, game));
			screens.frame(SDL_GetTicks(), {});
			// The callback may run until Engine joins its simulation thread, even
			// when the fixture exits with an exception. Its stream must outlive it.
			std::ofstream checksums;
			Engine engine;
			// Engine closes its turn session in its destructor. Join the runner
			// first on every exit path, including timeout or drawing exceptions.
			struct JoinSimulation
			{
				Engine &engine;
				~JoinSimulation() { engine.stopSimulationThread(); }
			} joinSimulation{engine};
			if (!game->initGame(engine).run())
				throw std::runtime_error("Multiplayer initialization failed");
			// Turn multiplayer has its own match record; capture every executed
			// checksum directly rather than depending on the legacy replay writer.
			if (const char *path = SDL_getenv("GLOB2_REPLAY_PATH"))
				checksums.open(std::string(path) + ".checksums");
			engine.turnLockstep()->onChecksum = [&checksums](std::uint32_t tick, Uint32 sum) {
				checksums << tick << ' ' << sum << '\n';
			};
			globals.automaticEndingGame = true;
			globals.automaticEndingSteps = 120;
			globals.automaticGameGlobalEndConditions = true;
			engine.prepareRun();
			game->gameStarted(true);
			engine.beginSession(SDL_GetTicks());
			// Use the production session mode: turn networking is polled on
			// the main thread; other sessions may use the simulation runner.
			const bool threaded = engine.startSimulationThread(SDL_GetTicks());
			auto nextStep = SDL_GetTicks();
			deadline = SDL_GetTicks() + 30000;
			bool running = true;
			while (running && SDL_GetTicks() < deadline)
			{
				const auto now = SDL_GetTicks();
				if (threaded)
					running = engine.threadedClientFrame(now, {});
				else if (now >= nextStep)
				{
					running = engine.stepSession(now, {});
					nextStep = now + engine.sessionDelay(now);
				}
				else
					engine.pollTurnSession(now);
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
			game->gameEnded(false);
			game->leave();
		}
		recorder.stop();
		recorder.shutdown();
		if (encoder && recorder.status().state != GAGCore::Recording::State::Complete)
			throw std::runtime_error(recorder.status().error);
		globalContainer = nullptr;
		std::cout << "MULTIPLAYER RECORDING PASS" << std::endl;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
