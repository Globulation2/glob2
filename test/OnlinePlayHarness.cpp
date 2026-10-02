// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// Plays an online room end to end through the real screens against a live platform
// instance: the online hub (guest sign-in), the Room screen, the starting screen, the
// game with its connection panel, and the results screen. Run one process as host and
// one as guest (docs/multiplayer/client.md, "End-to-end check"):
//
//   OnlinePlayHarness host  <origin> <dir>   creates a room, writes <dir>/code, starts
//   OnlinePlayHarness guest <origin> <dir>   joins by the code, readies
//
// The host sets a one-minute sudden-death timer so the match ends on its own and both
// clients reach the results screen. Every stage is captured as <dir>/<role>-<stage>.bmp.
// Controls are pressed by their keys, as a player would click them.
#include "CustomGameSetup.h"
#include "EndGameScreen.h"
#include "GlobalContainer.h"
#include "InstanceConfig.h"
#include "MatchStartScreen.h"
#include "OnlineHubScreen.h"
#include "OnlineMatch.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "PlatformRoom.h"
#include "RoomScreen.h"
#include "FileManager.h"
#include "Toolkit.h"
#include <ScreenStack.h>
#include <ui/Screen.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

GlobalContainer *globalContainer = nullptr;
using namespace GAGGUI;

namespace
{
std::string role, dir;
int shots = 0;

void capture(const std::string &stage)
{
	const std::string path = dir + "/" + role + "-" + stage + ".bmp";
	SDL_SaveBMP(globalContainer->gfx->getSDLSurface(), path.c_str());
	std::printf("%s CAPTURE %s\n", role.c_str(), path.c_str());
}

bool press(ScreenStack &screens, const std::string &key, std::vector<SDL_Event> &events)
{
	auto *screen = dynamic_cast<GAGGUI::ui::UIScreen *>(screens.top());
	auto *node = screen ? screen->host().find(key) : nullptr;
	if (!node)
		return false;
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
	std::printf("%s PRESS %s\n", role.c_str(), key.c_str());
	return true;
}

template <class T> T *top(ScreenStack &screens)
{
	return dynamic_cast<T *>(screens.top());
}

int play()
{
	ScreenStack screens(*globalContainer->gfx);
	auto hubScreen = std::make_unique<OnlineHubScreen>(screens);
	OnlineHubScreen *hub = hubScreen.get();
	screens.push(std::move(hubScreen));
	auto &client = Online::services().client;
	enum class Stage
	{
		SigningIn,
		Room,
		Ready,
		Starting,
		Playing,
		Results,
		Done
	} stage = Stage::SigningIn;
	const auto began = std::chrono::steady_clock::now();
	auto seconds = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count(); };
	double stageAt = 0, lastShot = 0, lastKey = 0;
	bool rulesSet = false, pressed = false;
	std::string code;
	int rc = 0;
	while (screens.running() && stage != Stage::Done)
	{
		std::vector<SDL_Event> events;
		SDL_Event event;
		while (SDL_PollEvent(&event))
			events.push_back(event);
		Online::pump();
		const double now = seconds();
		if (now > 900)
		{
			std::printf("%s FAIL: timed out in stage %d\n", role.c_str(), int(stage));
			rc = 1;
			break;
		}
		switch (stage)
		{
		case Stage::SigningIn:
			if (client.connection() == Online::PlatformClient::Connection::Online && client.account() && hub->model().link == OnlineHubScreen::Model::Link::Online && now - stageAt > 3)
			{
				std::printf("%s signed in as %s (%s) on %s\n", role.c_str(), client.account()->displayName.c_str(), client.account()->kind.c_str(), client.origin().c_str());
				capture("hub");
				if (role == "host")
					hub->createRoom();
				else
				{
					std::ifstream in(dir + "/code");
					std::getline(in, code);
					if (code.empty())
						break;
					hub->joinByCode(code);
				}
				stage = Stage::Room;
				stageAt = now;
			}
			break;
		case Stage::Room:
		{
			auto *room = top<RoomScreen>(screens);
			if (!room || !room->backend().lobbyReady())
				break;
			auto &backend = room->backend();
			if (role == "host")
			{
				if (!rulesSet && backend.roomName().size())
				{
					// A one-minute sudden-death timer ends the match on its own.
					CustomGameSetup setup;
					backend.setupDraft(setup);
					setup.suddenDeathMinutes = 1;
					backend.applySetup(setup);
					rulesSet = true;
					std::ofstream(dir + "/code") << backend.inviteCode() << "\n";
					std::printf("host ROOM %s %s\n", backend.inviteCode().c_str(), backend.inviteLink().c_str());
				}
				if (!backend.mapStatus().empty() && now - lastShot > 5)
				{
					std::printf("host map: %s\n", backend.mapStatus().c_str());
					lastShot = now;
				}
				if (backend.canStart() && now - stageAt > 3)
				{
					capture("room");
					room->selectTab(RoomScreen::MapTab);
					stage = Stage::Ready;
					stageAt = now;
				}
			}
			else if (now - stageAt > 3)
			{
				// Take the first open seat, then Ready.
				for (const auto &slot : backend.slots())
					if (slot.local)
						pressed = true;
				if (!pressed)
					for (const auto &slot : backend.slots())
						if (backend.canTakeSeat(slot))
						{
							backend.takeSeat(slot.index);
							pressed = true;
							break;
						}
				if (pressed && press(screens, "ready", events))
				{
					stage = Stage::Ready;
					stageAt = now;
				}
			}
			break;
		}
		case Stage::Ready:
		{
			auto *room = top<RoomScreen>(screens);
			if (role == "host" && room && now - stageAt > 2)
			{
				capture("room-map");
				room->selectTab(RoomScreen::PlayersTab);
				if (press(screens, "start", events))
				{
					stage = Stage::Starting;
					stageAt = now;
				}
			}
			else if (role == "guest" && room && now - stageAt > 2 && room->backend().localReady())
			{
				capture("room");
				stage = Stage::Starting;
				stageAt = now;
			}
			if (top<MatchStartScreen>(screens))
			{
				stage = Stage::Starting;
				stageAt = now;
			}
			break;
		}
		case Stage::Starting:
			if (auto *start = top<MatchStartScreen>(screens))
			{
				if (shots == 0)
				{
					capture("starting");
					++shots;
				}
				if (start->match().step() == Online::OnlineMatch::Step::Failed)
				{
					std::printf("%s FAIL: %s\n", role.c_str(), start->match().failure().c_str());
					rc = 1;
					stage = Stage::Done;
				}
			}
			else if (!top<RoomScreen>(screens) && !top<OnlineHubScreen>(screens))
			{
				std::printf("%s PLAYING after %.1f s\n", role.c_str(), now - stageAt);
				stage = Stage::Playing;
				stageAt = now;
				lastShot = now;
			}
			break;
		case Stage::Playing:
			if (now - lastShot > 15)
			{
				capture("ingame-" + std::to_string(int(now - stageAt)));
				lastShot = now;
			}
			// After a minute the guest closes its window: the relay sequences its
			// PlayerQuitsGameOrder, its colony is out and the host's game ends in victory.
			if (role == "guest" && !rulesSet && now - stageAt > 65)
			{
				std::printf("guest QUIT after %.1f s of play\n", now - stageAt);
				SDL_Event quit{};
				quit.type = SDL_QUIT;
				events.push_back(quit);
				rulesSet = true; // reused: the quit was sent
			}
			// The host's "You have won!" dialog: Ok (Enter) leads to the results screen.
			if (role == "host" && now - stageAt > 70 && int(now) % 5 == 0 && now - lastKey > 2)
			{
				lastKey = now;
				SDL_Event key{};
				key.type = SDL_KEYDOWN;
				key.key.keysym.sym = SDLK_RETURN;
				key.key.keysym.scancode = SDL_SCANCODE_RETURN;
				events.push_back(key);
				key.type = SDL_KEYUP;
				events.push_back(key);
			}
			// Let the quit reach the relay before the process ends.
			if (role == "guest" && rulesSet && now - stageAt > 75)
				stage = Stage::Done;
			if (auto *results = top<EndGameScreen>(screens))
			{
				std::printf("%s RESULTS after %.1f s of play\n", role.c_str(), now - stageAt);
				stage = Stage::Results;
				stageAt = now;
				lastShot = 0;
				(void)results;
			}
			break;
		case Stage::Results:
			if (lastShot == 0 && now - stageAt > 2)
			{
				capture("results");
				lastShot = now;
			}
			if (auto *results = top<EndGameScreen>(screens))
			{
				const auto &online = results->onlineResult();
				if (online && online->verification != Online::OnlineMatchResult::Verification::Pending && now - lastShot > 1)
				{
					capture("results-updated");
					std::printf("%s verification %d outcome %s\n", role.c_str(), int(online->verification), online->outcome.c_str());
					lastShot = now + 1000;
				}
				if (now - stageAt > 90)
				{
					capture("results-final");
					press(screens, "quit", events);
				}
			}
			else if (auto *room = top<RoomScreen>(screens))
			{
				capture("back-in-room");
				std::printf("%s back in the room\n", role.c_str());
				room->backend().leave();
				stage = Stage::Done;
			}
			break;
		case Stage::Done:
			break;
		}
		screens.frame(SDL_GetTicks(), events);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	std::printf("%s %s\n", role.c_str(), rc ? "FAIL" : "PASS");
	return rc;
}
} // namespace

int main(int argc, char **argv)
{
	if (argc != 4)
	{
		std::fprintf(stderr, "Usage: %s host|guest <origin> <directory>\n", argv[0]);
		return 2;
	}
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	role = argv[1];
	dir = argv[3];
	std::filesystem::create_directories(dir);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
	SDL_setenv("GLOB2_USER_DIR", (std::filesystem::absolute(dir) / ("profile-" + role)).string().c_str(), 1);
	GlobalContainer globals;
	globalContainer = &globals;
	GAGCore::Toolkit::close();
	GAGCore::Toolkit::init(("glob2-online-test-" + role).c_str());
	globals.fileManager = GAGCore::Toolkit::getFileManager();
	for (const char *sub : {"maps", "games", "campaigns", "replays", "thumbnails", "logs", "scripts", "videoshots", "online", "online/maps"})
		globals.fileManager->addWriteSubdir(sub);
	globals.settings.screenWidth = 1280;
	globals.settings.screenHeight = 800;
	globals.settings.screenFlags = 0;
	globals.settings.mute = true;
	globals.settings.language = "en";
	globals.load();
	auto &config = Online::services().config;
	if (!config.selectInstance(argv[2]))
	{
		std::fprintf(stderr, "invalid origin %s\n", argv[2]);
		return 2;
	}
	return play();
}
