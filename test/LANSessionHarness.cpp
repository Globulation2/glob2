// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real LAN rooms (LanRoom), lobby widgets, sockets, map transfer, readiness and departures.
#include "GlobalContainer.h"
#include <vector>
#include "Engine.h"
#include "LANFindScreen.h"
#include "LANSessionScreen.h"
#include <optional>
#include "MultiplayerGameScreen.h"
#include "LanRoom.h"
#include "MapCache.h"
#include "OnlineServices.h"
#include "MatchSetup.h"
#include "FileManager.h"
#include "Toolkit.h"
#include <ScreenStack.h>
#include <ui/Screen.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
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
	PressLeave = 2,
	PressStart = 3
};
void request(int code)
{
	SDL_Event event{};
	event.type = SDL_USEREVENT;
	event.user.code = code;
	SDL_PushEvent(&event);
}
Uint32 readyTimer(Uint32, void*) { request(PressReady); return 0; }
Uint32 leaveTimer(Uint32, void*) { request(PressLeave); return 0; }
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
	JoinScreen(ScreenStack& screens, const std::string& address, const std::string& capture, bool play = false)
		: LANFindScreen(screens), capture(capture), play(play)
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
            start = SDL_GetTicks64();
            timers[0] = SDL_AddTimer(5000, readyTimer, nullptr);
            // Playing, the guest stays until the host leaves (the runner stops it).
            if (!play) {
                timers[1] = SDL_AddTimer(25000, leaveTimer, nullptr);
                timers[2] = SDL_AddTimer(40000, timeoutTimer, nullptr);
            }
            connect();
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
    bool play = false;
    bool started = false;
    Uint64 start = 0;
    SDL_TimerID timers[3]{};
};

// Turn a queued press request into a click on the named control of the
// screen currently receiving input. The lobby lays itself out for the window,
// so the harness asks the layout rather than assuming pixel positions.
bool press(ScreenStack& screens, int code, std::vector<SDL_Event>& events)
{
	const char* key = code == PressReady ? "ready" : code == PressStart ? "start" : "cancel";
	auto* screen = dynamic_cast<GAGGUI::ui::UIScreen*>(screens.top());
	auto* node = screen ? screen->host().find(key) : nullptr;
	if (!node)
	{
		std::printf("JOIN FAIL: no '%s' control on the current screen\n", key);
		return false;
	}
	const auto r = node->bounds;
	SDL_Event event{};
	event.type = SDL_MOUSEBUTTONDOWN;
	event.button.button = SDL_BUTTON_LEFT;
	event.button.state = SDL_PRESSED;
	event.button.x = r.x + r.w / 2;
	event.button.y = r.y + r.h / 2;
	events.push_back(event);
	event.type = SDL_MOUSEBUTTONUP;
	event.button.state = SDL_RELEASED;
	events.push_back(event);
	return true;
}

class HostObserver
{
public:
	HostObserver(Lan::LanHost& host, int cycles, std::string capture)
        : host(host), cycles(cycles), capture(capture), start(SDL_GetTicks64()) {}
    std::optional<int> result;
	void onTimer(Uint32)
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
		const auto& room = host.state();
		const int count = static_cast<int>(room.members.size());
		if (count != previousCount)
		{
			std::printf("HOST roster=%d seats=%zu\n", count, room.setup.seats.size());
			for (const auto& m : room.members)
				std::printf("  member=%u seat=%d name=%s ready=%d map=%d\n", m.id, m.seat, m.name.c_str(), int(m.ready), int(m.hasMap));
			previousCount = count;
		}
		std::set<int> seats;
		std::set<std::string> names;
		for (const auto& m : room.members)
			if (!seats.insert(m.seat).second || !names.insert(m.name).second || m.seat < 0 ||
			    m.seat >= int(room.setup.seats.size()) || room.setup.seats[m.seat].name != m.name)
			{
				std::puts("HOST FAIL: duplicate identity or invalid seat");
				result = 1;
			}
		for (std::size_t i = 0; i < room.setup.seats.size(); ++i)
			if (room.setup.seats[i].seat != int(i))
			{
				std::puts("HOST FAIL: seats are not numbered in order");
				result = 1;
			}
		if (count > 2)
		{
			std::puts("HOST FAIL: expected exactly one host and one guest");
			result = 1;
		}
		if (count == 2 && host.canStart() && !readySeen)
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
				result = 0;
			}
		}
	}
private:
	Lan::LanHost& host;
	int cycles, completed = 0, previousCount = -1;
	bool readySeen = false;
	std::string capture, pendingCapture;
	Uint64 start;
};

Lan::LanHost::Options hostOptions(std::uint16_t port)
{
	Lan::LanHost::Options options;
	options.hostName = "LAN host";
	options.map = Engine::loadMapHeader("maps/FourSquares1.map");
	options.port = port;
	options.broadcast = port == 0;
	return options;
}

bool connectionFailureChecks()
{
    // A host that is never updated: its listener never completes a handshake, so
    // cancellation and the connection deadline must stay responsive meanwhile.
    auto stalled = Lan::LanRoom::host(hostOptions(17489));
    for (bool cancel : {true, false}) {
        Lan::LanClient::Options options;
        options.endpoint = stalled->shareText();
        options.name = "timeout probe";
        auto room = Lan::LanRoom::join(options);
        ScreenStack screens(*globalContainer->gfx);
        screens.push(std::make_unique<LANSessionScreen>(screens, room));
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
        if (screens.running() || screens.result() != (cancel ? 0 : 1) ||
            room->guestSide()->phase() != Lan::LanClient::Phase::Closed) return false;
    }
    std::puts("LAN progress PASS: cancellation and connection timeout release the connection");
    return true;
}

// Plays a real game through the room screen: the host presses Start once the guest
// is ready, both games run in their GameSessionScreens, and after `seconds` of play
// the host quits the application, which ends the game for the guest. The runner
// compares both processes' per-tick checksum sidecars.
int hostPlay(int seconds, const std::string& capture)
{
	int captures = 0;
	Uint64 nextCapture = 0;
	std::shared_ptr<Lan::LanRoom> room;
	try { room = Lan::LanRoom::host(hostOptions(0)); }
	catch (const std::exception& error) { std::printf("HOST FAIL: %s\n", error.what()); return 1; }
	std::cout << "PAIRING " << room->shareText() << std::endl;
	std::puts("HOST roster=1");
	ScreenStack screens(*globalContainer->gfx);
	screens.push(std::make_unique<LANSessionScreen>(screens, room));
	auto& host = *room->hostSide();
	bool pressed = false, quit = false;
	const Uint64 start = SDL_GetTicks64();
	while (screens.running()) {
		std::vector<SDL_Event> events;
		SDL_Event event;
		while (SDL_PollEvent(&event)) events.push_back(event);
		if (!pressed && host.guestCount() == 1 && host.canStart()) {
			// Press Start once the room screen has rebuilt with the ready guest.
			auto* screen = dynamic_cast<GAGGUI::ui::UIScreen*>(screens.top());
			if (screen && screen->host().find("start")) {
				press(screens, PressStart, events);
				pressed = true;
				std::puts("HOST PLAY pressed Start");
			}
		}
		if (pressed && !quit && host.horizon() >= Uint32(seconds * 25)) {
			std::printf("HOST PLAY horizon=%u, leaving\n", host.horizon());
			SDL_Event exit{};
			exit.type = SDL_QUIT;
			events.push_back(exit);
			quit = true;
		}
		screens.frame(SDL_GetTicks(), events);
		// Captures of the first seconds of play show the in-game connection notice
		// (waiting for every player to load).
		if (pressed && captures < 10 && SDL_GetTicks64() >= nextCapture) {
			SDL_SaveBMP(globalContainer->gfx->getSDLSurface(), (capture + "-" + std::to_string(captures++) + ".bmp").c_str());
			nextCapture = SDL_GetTicks64() + 300;
		}
		if (SDL_GetTicks64() - start > Uint64(seconds + 120) * 1000) {
			std::puts("HOST PLAY FAIL: timed out");
			return 1;
		}
	}
	std::puts(quit ? "HOST PLAY PASS" : "HOST PLAY FAIL: the session ended early");
	return quit ? 0 : 1;
}

int host(int cycles, const std::string& capture)
{
	std::shared_ptr<Lan::LanRoom> room;
	try { room = Lan::LanRoom::host(hostOptions(0)); }
	catch (const std::exception& error) { std::printf("HOST FAIL: %s\n", error.what()); return 1; }
    std::cout << "PAIRING " << room->shareText() << std::endl;
    ScreenStack screens(*globalContainer->gfx);
    screens.push(std::make_unique<LANSessionScreen>(screens, room));
    HostObserver observer(*room->hostSide(), cycles, capture);
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
	globals.settings.setGraphicsDetail(false);
	globals.settings.mute = true;
	globals.settings.language = "en";
	globals.settings.setUsername(std::string(argv[1]).rfind("host", 0) == 0 ? "LAN host" : "LAN guest");
	globals.load();
	if (SDLNet_Init() < 0) return 1;
	int rc = 0;
	if (std::string(argv[1]) == "host") rc = connectionFailureChecks() ? host(std::stoi(argv[3]), argv[4]) : 1;
	else if (std::string(argv[1]) == "host-play") rc = hostPlay(std::stoi(argv[3]), argv[4]);
	else if (std::string(argv[1]) == "join-play")
	{
		ScreenStack screens(*globals.gfx);
		screens.push(std::make_unique<JoinScreen>(screens, argv[2], std::string(argv[4]) + ".bmp", true));
		while (screens.running())
		{
			std::vector<SDL_Event> events;
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
				if (event.type == SDL_USEREVENT) { if (!press(screens, event.user.code, events)) rc = 1; }
				else events.push_back(event);
			}
			screens.frame(SDL_GetTicks(), events);
		}
	}
	else
	{
		const std::string source = glob2PreferGzipReadPath(*globals.fileManager, "maps/FourSquares1.map");
		const std::string hash = Online::mapContentHash(source);
		for (int cycle = 0; cycle < std::stoi(argv[3]) && !rc; ++cycle)
		{
		// Force a download every cycle: the guest's map cache is keyed by content hash.
		Online::services().maps.remove(hash);
        ScreenStack screens(*globals.gfx);
        screens.push(std::make_unique<JoinScreen>(screens, argv[2], std::string(argv[4]) + "-" + std::to_string(cycle + 1) + ".bmp"));
        while (screens.running())
        {
            std::vector<SDL_Event> events;
            SDL_Event event;
            bool failed = false;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_USEREVENT) failed = !press(screens, event.user.code, events) || failed;
                else events.push_back(event);
            }
            if (failed) { rc = 1; break; }
            screens.frame(SDL_GetTicks(), events);
            SDL_Delay(20);
        }
        if (!rc) rc = screens.result();
		std::string expected, actual;
		if (!Online::readMapBytes(source, expected) || !Online::readMapBytes(Online::services().maps.path(hash).value_or(std::string()), actual) || actual != expected)
		{
			std::puts("JOIN FAIL: downloaded map differs from source");
			rc = 1;
		}
		else std::printf("JOIN map verified: %zu bytes, cycle=%d\n", actual.size(), cycle + 1);
		SDL_Delay(1000);
		}
	}
	SDLNet_Quit();
	return rc;
}
