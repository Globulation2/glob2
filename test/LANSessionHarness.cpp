// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real LAN clients, lobby widgets, sockets, readiness and departures.
#include "GlobalContainer.h"
#include <vector>
#include "Engine.h"
#include "LANFindScreen.h"
#include "LANSessionScreen.h"
#include <optional>
#include "MultiplayerGameScreen.h"
#include "YOGServer.h"
#include "FileManager.h"
#include "Toolkit.h"
#include <ScreenStack.h>
#include <ui/Screen.h>

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
// Timer callbacks run on SDL's timer thread, so they only queue a request;
// the join loop presses the control wherever the live lobby layout put it.
enum PressRequest
{
	PressReady = 1,
	PressLeave = 2
};
void request(int code)
{
	SDL_Event event{};
	event.type = SDL_EVENT_USER;
	event.user.code = code;
	SDL_PushEvent(&event);
}
Uint32 SDLCALL readyTimer(void*, SDL_TimerID, Uint32) { request(PressReady); return 0; }
Uint32 SDLCALL leaveTimer(void*, SDL_TimerID, Uint32) { request(PressLeave); return 0; }
Uint32 SDLCALL timeoutTimer(void*, SDL_TimerID, Uint32)
{
	SDL_Event event{};
	event.type = SDL_EVENT_QUIT;
	SDL_PushEvent(&event);
	return 0;
}

class JoinScreen : public LANFindScreen
{
public:
	JoinScreen(ScreenStack& screens, const std::string& address, const std::string& capture) : LANFindScreen(screens), capture(capture)
	{
		// Use the real form's entry points; no networking is stubbed.
		setServer(address);
	}
    ~JoinScreen() override {
        for (auto timer : timers) if (timer) SDL_RemoveTimer(timer);
    }
    void onTimer(Uint32 tick) override {
        LANFindScreen::onTimer(tick);
        if (!started) {
            started = true;
            start = SDL_GetTicks();
            timers[0] = SDL_AddTimer(5000, readyTimer, nullptr);
            timers[1] = SDL_AddTimer(25000, leaveTimer, nullptr);
            timers[2] = SDL_AddTimer(40000, timeoutTimer, nullptr);
            connect();
            return;
        }
        // Parent updates resume only after the scheduled LAN session returns.
        SDL_SaveBMP(globalContainer->gfx->getSDLSurface(), capture.c_str());
        const bool ok = SDL_GetTicks() - start < 39000;
        std::puts(ok ? "JOIN PASS: lobby returned through Leave Game" : "JOIN FAIL: lobby timed out");
        endExecute(ok ? 0 : 1);
    }
private:
    std::string capture;
    bool started = false;
    Uint64 start = 0;
    SDL_TimerID timers[3]{};
};

// Turn a queued press request into a click on the named control of the
// screen currently receiving input. The lobby lays itself out for the window,
// so the harness asks the layout rather than assuming pixel positions.
bool press(ScreenStack& screens, int code, std::vector<SDL_Event>& events)
{
	const char* key = code == PressReady ? "ready" : "cancel";
	auto* screen = dynamic_cast<GAGGUI::ui::UIScreen*>(screens.top());
	auto* node = screen ? screen->host().find(key) : nullptr;
	if (!node)
	{
		std::printf("JOIN FAIL: no '%s' control on the current screen\n", key);
		return false;
	}
	const auto r = node->bounds;
	SDL_Event event{};
	event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	event.button.button = SDL_BUTTON_LEFT;
	event.button.down = true;
	event.button.x = r.x + r.w / 2;
	event.button.y = r.y + r.h / 2;
	events.push_back(event);
	event.type = SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.down = false;
	events.push_back(event);
	return true;
}

class HostObserver
{
public:
	HostObserver(std::shared_ptr<YOGClient> client, int cycles, std::string capture)
        : client(client), cycles(cycles), capture(capture), start(SDL_GetTicks()) {}
    std::optional<int> result;
	void onTimer(Uint32 tick)
	{

		if (!pendingCapture.empty())
		{
			SDL_SaveBMP(globalContainer->gfx->getSDLSurface(), pendingCapture.c_str());
			pendingCapture.clear();
		}
		if (SDL_GetTicks() - start > 180000)
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
    if (!stalled.isListening()) return false;
    for (bool cancel : {true, false}) {
        auto client = std::make_shared<YOGClient>();
        client->connect("127.0.0.1");
        const auto deadline = SDL_GetTicks() + 2000;
        while (!client->isConnected() && SDL_GetTicks() < deadline) {
            client->update(); SDL_Delay(1);
        }
        if (!client->isConnected()) return false;
        ScreenStack screens(*globalContainer->gfx);
        screens.push(std::make_unique<LANSessionScreen>(screens, client, "timeout probe"));
        screens.frame(0, {});
        SDL_Event escape{};
        escape.type = SDL_EVENT_KEY_DOWN;
        escape.key.key = SDLK_ESCAPE;
        if (cancel) {
            screens.frame(1, {escape}); screens.frame(2, {});
        } else {
            // Advance the host clock, not wall time, through the real deadline.
            screens.frame(10000, {}); screens.frame(10001, {});
            screens.frame(10002, {escape}); screens.frame(10003, {}); screens.frame(10004, {});
        }
        if (screens.running() || screens.result() != (cancel ? 0 : 1) || client->isConnected()) return false;
    }
    std::puts("LAN progress PASS: cancellation and greeting timeout release the connection");
    return true;
}

int host(int cycles, const std::string& capture)
{
	auto client = std::make_shared<YOGClient>();
	auto server = std::make_shared<YOGServer>(YOGAnonymousLogin, YOGSingleGame);
	if (!server->isListening()) { std::puts("HOST FAIL: port in use"); return 1; }
	server->enableLANBroadcasting();
	client->attachGameServer(server);
	client->connect("127.0.0.1");
	// A private map name forces a real transfer without touching user maps.
	MapHeader map = Engine::loadMapHeader("maps/FourSquares1.map");
	map.setMapName("LAN regression transfer");
	const std::string sourcePath = glob2PreferGzipReadPath(*Toolkit::getFileManager(), "maps/FourSquares1.map");
	std::filesystem::copy_file(sourcePath,
		Toolkit::getFileManager()->getDir(0) + "/" + glob2GzipWritePath(map.getFileName()),
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
	SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 0);
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
	if (!NET_Init()) return 1;
	int rc = 0;
	if (std::string(argv[1]) == "host") rc = connectionFailureChecks() ? host(std::stoi(argv[3]), argv[4]) : 1;
	else for (int cycle = 0; cycle < std::stoi(argv[3]) && !rc; ++cycle)
	{
		// A new receiver stores the download as ".gz" without unzipping it (see
		// YOGClientFileAssembler::handleMessage), so this is the file to expect.
		const auto downloaded = std::filesystem::path(globals.fileManager->getDir(0)) / "maps/LAN_regression_transfer.map.gz";
		// Force a second request too: rejoining must reuse the server's upload
		// rather than append another copy of its chunks to the cached transfer.
		std::filesystem::remove(downloaded);
        ScreenStack screens(*globals.gfx);
        screens.push(std::make_unique<JoinScreen>(screens, argv[2], std::string(argv[4]) + "-" + std::to_string(cycle + 1) + ".bmp"));
        while (screens.running())
        {
            std::vector<SDL_Event> events;
            SDL_Event event;
            bool failed = false;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_USER) failed = !press(screens, event.user.code, events) || failed;
                else events.push_back(event);
            }
            if (failed) { rc = 1; break; }
            screens.frame(SDL_GetTicks(), events);
            SDL_Delay(20);
        }
        if (!rc) rc = screens.result();
		std::ifstream original(glob2PreferGzipReadPath(*globals.fileManager, "maps/FourSquares1.map"), std::ios::binary);
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
	NET_Quit();
	return rc;
}
