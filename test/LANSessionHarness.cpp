// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real LAN clients, lobby widgets, sockets, readiness and departures.
#include "GlobalContainer.h"
#include "Engine.h"
#include "LANFindScreen.h"
#include "LANSessionScreen.h"
#include <optional>
#include "MultiplayerGameScreen.h"
#include "YOGServer.h"
#include "FileManager.h"
#include "GUITextInput.h"
#include "Toolkit.h"
#include <ScreenStack.h>

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
	JoinScreen(ScreenStack& screens, const std::string& address, const std::string& capture) : LANFindScreen(screens), capture(capture)
	{
		// Use the real form's public widget API; no networking is stubbed.
		for (Widget* widget : widgets)
			if (auto* input = dynamic_cast<TextInput*>(widget))
				if (input->getText() == "localhost") input->setText(address);
	}
    ~JoinScreen() override {
        for (auto timer : timers) if (timer) SDL_RemoveTimer(timer);
    }
    void onTimer(Uint32 tick) override {
        LANFindScreen::onTimer(tick);
        if (!started) {
            started = true;
            start = SDL_GetTicks64();
            timers[0] = SDL_AddTimer(5000, readyTimer, nullptr);
            timers[1] = SDL_AddTimer(25000, leaveTimer, nullptr);
            timers[2] = SDL_AddTimer(40000, timeoutTimer, nullptr);
            LANFindScreen::onAction(nullptr, BUTTON_RELEASED, CONNECT, 0);
            return;
        }
        // Parent updates resume only after the scheduled LAN session returns.
        SDL_SaveBMP(globalContainer->gfx->getSDLSurface(), capture.c_str());
        const bool ok = SDL_GetTicks64() - start < 39000;
        std::puts(ok ? "JOIN PASS: lobby returned through Leave Game" : "JOIN FAIL: lobby timed out");
        endExecute(ok ? 0 : 1);
    }
private:
    std::string capture;
    bool started = false;
    Uint64 start = 0;
    SDL_TimerID timers[3]{};
};

class HostObserver
{
public:
	HostObserver(std::shared_ptr<YOGClient> client, int cycles, std::string capture)
        : client(client), cycles(cycles), capture(capture), start(SDL_GetTicks64()) {}
    std::optional<int> result;
	void onTimer(Uint32 tick)
	{

		if (!pendingCapture.empty())
		{
			SDL_SaveBMP(globalContainer->gfx->getSDLSurface(), pendingCapture.c_str());
			pendingCapture.clear();
		}
		if (SDL_GetTicks64() - start > 180000)
		{
			std::puts("HOST FAIL: timed out waiting for ready/join/leave cycles");
			result = 1;
			return;
		}
        auto game = client->getMultiplayerGame();
        if (!game) return;
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
				result = 1;
			}
		}
		if (count > 2)
		{
			std::puts("HOST FAIL: expected exactly one host and one guest");
			result = 1;
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
				result = 0;
			}
		}
	}
private:
	std::shared_ptr<YOGClient> client;
	int cycles, completed = 0, previousCount = -1;
	bool readySeen = false;
	std::string capture, pendingCapture;
	Uint64 start;
};

bool connectionFailureChecks()
{
    // Accept the TCP handshake at the OS level but never pump YOG, so the
    // client really remains connected without receiving a protocol greeting.
    YOGServer stalled(YOGAnonymousLogin, YOGSingleGame);
    if (!stalled.isListening()) { std::puts("LAN progress FAIL: probe port in use"); return false; }
    for (bool cancel : {true, false}) {
        auto client = std::make_shared<YOGClient>();
        client->connect("127.0.0.1");
        const auto deadline = SDL_GetTicks64() + 2000;
        while (!client->isConnected() && SDL_GetTicks64() < deadline) {
            client->update(); SDL_Delay(1);
        }
        if (!client->isConnected()) { std::puts("LAN progress FAIL: probe TCP connection timed out"); return false; }
        ScreenStack screens(*globalContainer->gfx);
        screens.push(std::make_unique<LANSessionScreen>(screens, client, "timeout probe"));
        screens.frame(0, {});
        SDL_Event escape{};
        escape.type = SDL_KEYDOWN;
        escape.key.keysym.sym = SDLK_ESCAPE;
        if (cancel) {
            screens.frame(1, {escape}); screens.frame(2, {});
        } else {
            // Advance the host clock, not wall time, through the real deadline.
            screens.frame(10000, {}); screens.frame(10001, {});
            screens.frame(10002, {escape}); screens.frame(10003, {}); screens.frame(10004, {});
        }
        if (screens.running() || screens.result() != (cancel ? 0 : 1) || client->isConnected()) {
            std::printf("LAN progress FAIL: cancel=%d running=%d result=%d connected=%d\n",
                int(cancel), int(screens.running()), screens.result(), int(client->isConnected()));
            return false;
        }
    }
    std::puts("LAN progress PASS: cancellation and greeting timeout release the connection");
    return true;
}

int host(int cycles, const std::string& capture)
{
	auto client = std::make_shared<YOGClient>();
	auto server = std::make_shared<YOGServer>(YOGAnonymousLogin, YOGSingleGame);
	if (!server->isListening()) { std::puts("HOST FAIL: port in use"); return 1; }
	// The fixture joins by explicit loopback address; do not advertise its
	// temporary diagnostic ports to other machines on the user's network.
	client->attachGameServer(server);
	client->connect("127.0.0.1");
	// A private map name forces a real transfer without touching user maps.
	MapHeader map = Engine::loadMapHeader("maps/FourSquares1.map");
	map.setMapName("LAN regression transfer");
	std::filesystem::copy_file("maps/FourSquares1.map",
		Toolkit::getFileManager()->getDir(0) + "/" + map.getFileName(),
		std::filesystem::copy_options::overwrite_existing);
    ScreenStack screens(*globalContainer->gfx);
    screens.push(std::make_unique<LANSessionScreen>(screens, client, "LAN host", map));
    HostObserver observer(client, cycles, capture);
    while (screens.running() && !observer.result) {
        std::vector<SDL_Event> events;
        SDL_Event event;
        while (SDL_PollEvent(&event)) events.push_back(event);
        screens.frame(SDL_GetTicks(), events);
        observer.onTimer(SDL_GetTicks());
        SDL_Delay(20);
    }
    int rc = observer.result.value_or(1);
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
	if (std::string(argv[1]) == "host") rc = connectionFailureChecks() ? host(std::stoi(argv[3]), argv[4]) : 1;
	else for (int cycle = 0; cycle < std::stoi(argv[3]) && !rc; ++cycle)
	{
		const auto downloaded = std::filesystem::path(globals.fileManager->getDir(0)) / "maps/LAN_regression_transfer.map";
		// Force a second request too: rejoining must reuse the server's upload
		// rather than append another copy of its chunks to the cached transfer.
		std::filesystem::remove(downloaded);
        ScreenStack screens(*globals.gfx);
        screens.push(std::make_unique<JoinScreen>(screens, argv[2], std::string(argv[4]) + "-" + std::to_string(cycle + 1) + ".bmp"));
        rc = screens.execute(20);
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
