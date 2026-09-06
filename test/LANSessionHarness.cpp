// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real LAN clients, lobby widgets, sockets, readiness and departures.
#include "GlobalContainer.h"
#include "Engine.h"
#include "LANFindScreen.h"
#include "MultiplayerGameScreen.h"
#include "YOGClientBringup.h"
#include "YOGServer.h"
#include "FileManager.h"
#include "GUITextInput.h"
#include "Toolkit.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>

GlobalContainer* globalContainer = nullptr;
using namespace GAGGUI;
using namespace GAGCore;

namespace
{
void click(int x, int y)
{
	SDL_Event event{};
	event.type = SDL_MOUSEBUTTONDOWN;
	event.button.button = SDL_BUTTON_LEFT;
	event.button.state = SDL_PRESSED;
	event.button.x = x;
	event.button.y = y;
	SDL_PushEvent(&event);
	event.type = SDL_MOUSEBUTTONUP;
	event.button.state = SDL_RELEASED;
	SDL_PushEvent(&event);
}

Uint32 readyTimer(Uint32, void*) { click(610, 450); return 0; }
Uint32 leaveTimer(Uint32, void*) { click(530, 525); return 0; }
Uint32 timeoutTimer(Uint32, void*)
{
	SDL_Event event{};
	event.type = SDL_QUIT;
	SDL_PushEvent(&event);
	return 0;
}

class JoinScreen : public LANFindScreen
{
public:
	JoinScreen(const std::string& address, const std::string& capture) : capture(capture)
	{
		// Use the real form's public widget API; no networking is stubbed.
		for (Widget* widget : widgets)
			if (auto* input = dynamic_cast<TextInput*>(widget))
				if (input->getText() == "localhost") input->setText(address);
	}
	void onTimer(Uint32 tick) override
	{
		LANFindScreen::onTimer(tick);
		const Uint64 start = SDL_GetTicks64();
		// SDL timers only enqueue input. Rendering and network state stay on
		// the real screen's main thread, including the nested lobby loop.
		SDL_TimerID ready = SDL_AddTimer(5000, readyTimer, nullptr);
		SDL_TimerID leave = SDL_AddTimer(25000, leaveTimer, nullptr);
		SDL_TimerID timeout = SDL_AddTimer(40000, timeoutTimer, nullptr);
		LANFindScreen::onAction(nullptr, BUTTON_RELEASED, CONNECT, 0);
		SDL_RemoveTimer(ready);
		SDL_RemoveTimer(leave);
		SDL_RemoveTimer(timeout);
		SDL_SaveBMP(globalContainer->gfx->getSDLSurface(), capture.c_str());
		bool ok = returnCode != QUIT_APPLICATION && SDL_GetTicks64() - start < 39000;
		std::puts(ok ? "JOIN PASS: lobby returned through Leave Game" : "JOIN FAIL: lobby did not complete before the timeout");
		endExecute(ok ? 0 : 1);
	}
private:
	std::string capture;
};

class HostScreen : public Glob2TabScreen
{
public:
	HostScreen(std::shared_ptr<MultiplayerGame> game, int cycles, std::string capture)
		: Glob2TabScreen(true), game(game), cycles(cycles), capture(capture), start(SDL_GetTicks64()) {}
	void onTimer(Uint32 tick) override
	{
		Glob2TabScreen::onTimer(tick);
		if (!pendingCapture.empty())
		{
			SDL_SaveBMP(globalContainer->gfx->getSDLSurface(), pendingCapture.c_str());
			pendingCapture.clear();
		}
		if (SDL_GetTicks64() - start > 180000)
		{
			std::puts("HOST FAIL: timed out waiting for ready/join/leave cycles");
			endExecute(1);
			return;
		}
		auto& header = game->getGameHeader();
		int count = header.getNumberOfPlayers();
		if (count != previousCount)
		{
			std::printf("HOST roster=%d state=%d\n", count, int(game->getGameJoinCreationState()));
			for (int i = 0; i < count; ++i)
			{
				const BasePlayer& p = header.getBasePlayer(i);
				std::printf("  slot=%d id=%u name=%s mask=%u\n", p.number, p.playerID, p.name.c_str(), p.numberMask);
			}
			previousCount = count;
		}
		std::set<Uint32> ids;
		for (int i = 0; i < count; ++i)
		{
			const BasePlayer& p = header.getBasePlayer(i);
			if (!ids.insert(p.playerID).second || p.number != i || p.numberMask != (Uint32(1) << i))
			{
				std::puts("HOST FAIL: duplicate identity or invalid slot mask");
				endExecute(1);
			}
		}
		if (count > 2)
		{
			std::puts("HOST FAIL: expected exactly one host and one guest");
			endExecute(1);
		}
		if (count == 2 && game->isGameReadyToStart() && !readySeen)
		{
			readySeen = true;
			std::printf("HOST guest ready, cycle=%d\n", completed + 1);
			pendingCapture = capture + "-" + std::to_string(completed + 1) + ".bmp";
		}
		if (count == 1 && readySeen)
		{
			readySeen = false;
			++completed;
			std::printf("HOST departure verified, cycle=%d\n", completed);
			if (completed == cycles)
			{
				std::puts("HOST PASS: all ready/join/leave cycles completed");
				game->leaveGame();
				endExecute(0);
			}
		}
	}
private:
	std::shared_ptr<MultiplayerGame> game;
	int cycles, completed = 0, previousCount = -1;
	bool readySeen = false;
	std::string capture, pendingCapture;
	Uint64 start;
};

int host(int cycles, const std::string& capture)
{
	auto client = std::make_shared<YOGClient>();
	auto server = std::make_shared<YOGServer>(YOGAnonymousLogin, YOGSingleGame);
	if (!server->isListening()) { std::puts("HOST FAIL: port in use"); return 1; }
	server->enableLANBroadcasting();
	client->attachGameServer(server);
	client->connect("127.0.0.1");
	if (LANBringup::waitForConnectionState(*client, YOGClient::WaitingForLoginInformation) != LANBringup::Result::Reached) return 1;
	client->attemptLogin("LAN host");
	if (LANBringup::waitForConnectionState(*client, YOGClient::ClientOnStandby) != LANBringup::Result::Reached) return 1;
	auto game = std::make_shared<MultiplayerGame>(client);
	client->setMultiplayerGame(game);
	game->createNewGame("LAN regression");
	// A private map name forces a real transfer without touching user maps.
	MapHeader map = Engine::loadMapHeader("maps/FourSquares1.map");
	map.setMapName("LAN regression transfer");
	std::filesystem::copy_file("maps/FourSquares1.map",
		Toolkit::getFileManager()->getDir(0) + "/" + map.getFileName(),
		std::filesystem::copy_options::overwrite_existing);
	game->setMapHeader(map);
	HostScreen screen(game, cycles, capture);
	MultiplayerGameScreen lobby(&screen, game, client);
	int rc = screen.execute(globalContainer->gfx, 20);
	client->setMultiplayerGame({});
	return rc;
}
}

int main(int argc, char** argv)
{
	if (argc != 5)
	{
		std::fprintf(stderr, "Usage: %s host|join address cycles capture-prefix\n", argv[0]);
		return 2;
	}
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
	GlobalContainer globals;
	globalContainer = &globals;
	// Keep the harness's map downloads and anonymous server data separate
	// from the user's normal game profile.
	Toolkit::close();
	Toolkit::init((std::string("glob2-lan-test-") + argv[1]).c_str());
	globals.fileManager = Toolkit::getFileManager();
	for (const char* dir : {"maps", "games", "campaigns", "replays", "thumbnails", "beta4", "beta4/gamelog", "logs", "scripts", "videoshots"})
		globals.fileManager->addWriteSubdir(dir);
	globals.settings.screenWidth = 640;
	globals.settings.screenHeight = 600;
	globals.settings.screenFlags = 0;
	globals.settings.optionFlags |= GlobalContainer::OPTION_LOW_SPEED_GFX;
	globals.settings.mute = true;
	globals.settings.language = "en";
	globals.settings.setUsername(std::string(argv[1]) == "host" ? "LAN host" : "LAN guest");
	globals.load();
	if (SDLNet_Init() < 0) return 1;
	int rc = 0;
	if (std::string(argv[1]) == "host") rc = host(std::stoi(argv[3]), argv[4]);
	else for (int cycle = 0; cycle < std::stoi(argv[3]) && !rc; ++cycle)
	{
		const auto downloaded = std::filesystem::path(globals.fileManager->getDir(0)) / "maps/LAN_regression_transfer.map";
		// Force a second request too: rejoining must reuse the server's upload
		// rather than append another copy of its chunks to the cached transfer.
		std::filesystem::remove(downloaded);
		JoinScreen screen(argv[2], std::string(argv[4]) + "-" + std::to_string(cycle + 1) + ".bmp");
		rc = screen.execute(globals.gfx, 20);
		std::ifstream original("maps/FourSquares1.map", std::ios::binary);
		std::ifstream received(downloaded, std::ios::binary);
		std::string expected((std::istreambuf_iterator<char>(original)), {});
		std::string actual((std::istreambuf_iterator<char>(received)), {});
		if (!original || !received || actual != expected)
		{
			std::puts("JOIN FAIL: downloaded map differs from source");
			rc = 1;
		}
		else std::printf("JOIN map verified: %zu bytes, cycle=%d\n", actual.size(), cycle + 1);
		SDL_Delay(1000);
	}
	SDLNet_Quit();
	return rc;
}
