// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// Plays online matches end to end through the real screens against a live platform
// instance: the online hub (guest sign-in), the Room screen or quick match, the
// starting screen, the game with its connection panel, and the results screen.
// docs/multiplayer/client.md, "End-to-end check":
//
//   OnlinePlayHarness host  <origin> <dir>   creates a room, writes <dir>/code, starts
//   OnlinePlayHarness guest <origin> <dir>   joins by the code, readies
//   OnlinePlayHarness quick <origin> <dir>   casual quick match (AI backfill)
//
// Environment (all optional):
//   GLOB2_E2E_SUDDEN_DEATH   room sudden-death timer in minutes (default 1)
//   GLOB2_E2E_GUEST_LEAVE    when the guest leaves, in seconds of play (default 40;
//                            0 = it stays until the game ends)
//   GLOB2_E2E_LEAVE_BY       window (default: close the window) | menu (the in-game
//                            Quit: a sequenced PlayerQuitsGameOrder, then results)
//   GLOB2_E2E_QUICK_QUEUE    quick-match queue id (default: the first unrated queue)
//   GLOB2_E2E_QUICK_LEAVE    seconds of quick-match play before leaving (default 45)
//
// Every stage is captured as <dir>/<role>-<stage>.bmp, and every line of the log is
// prefixed with the role and the seconds since start. Controls are pressed by their
// keys, as a player would click them. The host's results stage waits until the
// platform has settled the result (Phase::Done) and reports how long that took.
#include "CustomGameSetup.h"
#include "EndGameScreen.h"
#include "Engine.h"
#include "GameSessionScreen.h"
#include "GlobalContainer.h"
#include "InstanceConfig.h"
#include "MatchStartScreen.h"
#include "OnlineHubScreen.h"
#include "OnlineMatch.h"
#include "OnlineServices.h"
#include "Order.h"
#include "PlatformClient.h"
#include "PlatformRoom.h"
#include "QuickMatch.h"
#include "QuickMatchScreen.h"
#include "RelayTransport.h"
#include "RoomScreen.h"
#include "Team.h"
#include "FileManager.h"
#include "Toolkit.h"
#include "TurnSession.h"
#include <ScreenStack.h>
#include <ui/Screen.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
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
std::chrono::steady_clock::time_point began;

double seconds()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
}

void say(const char *format, ...)
{
	char line[1024];
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof line, format, args);
	va_end(args);
	std::printf("%7.1f %s %s\n", seconds(), role.c_str(), line);
}

int envInt(const char *name, int fallback)
{
	const char *value = std::getenv(name);
	return value && *value ? std::atoi(value) : fallback;
}

std::string envText(const char *name, const std::string &fallback)
{
	const char *value = std::getenv(name);
	return value && *value ? value : fallback;
}

void capture(const std::string &stage)
{
	const std::string path = dir + "/" + role + "-" + stage + ".bmp";
	SDL_SaveBMP(globalContainer->gfx->getSDLSurface(), path.c_str());
	say("CAPTURE %s", path.c_str());
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
	say("PRESS %s", key.c_str());
	return true;
}

template <class T> T *top(ScreenStack &screens)
{
	return dynamic_cast<T *>(screens.top());
}

const char *presenceName(Turn::PresenceState state)
{
	switch (state)
	{
	case Turn::PresenceState::NotConnected:
		return "not-connected";
	case Turn::PresenceState::Connected:
		return "connected";
	case Turn::PresenceState::Lagging:
		return "lagging";
	case Turn::PresenceState::Reconnecting:
		return "reconnecting";
	case Turn::PresenceState::Resyncing:
		return "resyncing";
	case Turn::PresenceState::Left:
		return "left";
	}
	return "?";
}

const char *phaseName(Online::OnlineMatchResult::Phase phase)
{
	switch (phase)
	{
	case Online::OnlineMatchResult::Phase::Waiting:
		return "waiting-for-players";
	case Online::OnlineMatchResult::Phase::Verifying:
		return "verifying";
	case Online::OnlineMatchResult::Phase::Done:
		return "done";
	}
	return "?";
}

const char *verificationName(Online::OnlineMatchResult::Verification v)
{
	using V = Online::OnlineMatchResult::Verification;
	switch (v)
	{
	case V::Pending:
		return "pending";
	case V::Verified:
		return "verified";
	case V::Diverged:
		return "diverged";
	case V::Unverifiable:
		return "unverifiable";
	case V::NotApplicable:
		return "not_applicable";
	}
	return "?";
}

// One line about the game: tick, end state and every other human seat's presence as
// this client's turn session reports it (what the connection panel shows).
std::string gameLine(Engine &engine)
{
	auto &game = engine.gui.game;
	std::string line = "tick " + std::to_string(game.stepCounter);
	if (game.isGameEnded)
		line += " game-ended";
	if (auto *team = engine.gui.getLocalTeam())
		line += team->hasWon ? " local-won" : team->hasLost ? " local-lost" : "";
	if (auto *session = engine.turnSession())
		for (int seat = 0; seat < int(Turn::MAX_SEATS); ++seat)
			if ((session->humanSeatMask() & (1u << seat)) && seat != session->localSeat())
			{
				const auto info = session->seatPresenceInfo(seat);
				line += " seat" + std::to_string(seat) + "=" + presenceName(info.state);
				if (info.state == Turn::PresenceState::Reconnecting || info.state == Turn::PresenceState::NotConnected)
					line += "(grace " + std::to_string(info.graceRemainingTicks / 25) + "s)";
			}
	return line;
}

// After the window closed the stack is gone; the relay connection still has its Quit
// to write, as the shutdown screen would let it.
void drainRelayConnections()
{
	const double until = seconds() + 4;
	while (Online::lingeringRelayConnections() > 0 && seconds() < until)
	{
		Online::pump();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	say("relay connections drained (%zu left)", Online::lingeringRelayConnections());
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
		Queue,
		Starting,
		Playing,
		Results,
		Done
	} stage = Stage::SigningIn;
	const int suddenDeath = envInt("GLOB2_E2E_SUDDEN_DEATH", 1);
	const int guestLeave = envInt("GLOB2_E2E_GUEST_LEAVE", 40);
	const bool leaveByMenu = envText("GLOB2_E2E_LEAVE_BY", "window") == "menu";
	const int quickLeave = envInt("GLOB2_E2E_QUICK_LEAVE", 45);
	double stageAt = 0, lastShot = 0, lastKey = 0, lastLine = 0, endedAt = -1;
	bool rulesSet = false, pressed = false, left = false;
	std::string code, lastPresence, lastPhase, matchId;
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
			say("FAIL: timed out in stage %d", int(stage));
			rc = 1;
			break;
		}
		switch (stage)
		{
		case Stage::SigningIn:
			if (client.connection() == Online::PlatformClient::Connection::Online && client.account() && hub->model().link == OnlineHubScreen::Model::Link::Online && now - stageAt > 3)
			{
				say("signed in as %s (%s) on %s", client.account()->displayName.c_str(), client.account()->kind.c_str(), client.origin().c_str());
				capture("hub");
				if (role == "host")
					hub->createRoom();
				else if (role == "guest")
				{
					std::ifstream in(dir + "/code");
					std::getline(in, code);
					if (code.empty())
						break;
					hub->joinByCode(code);
				}
				else
				{
					// The quick-match card of the queue to play (the first unrated one).
					const auto &queues = hub->model().queues;
					if (!queues.is_array() || queues.empty())
						break;
					const std::string wanted = envText("GLOB2_E2E_QUICK_QUEUE", "");
					int index = -1;
					for (int i = 0; i < int(queues.size()) && index < 0; ++i)
						if (wanted.empty() ? !queues[std::size_t(i)].value("rated", true) : queues[std::size_t(i)].value("id", "") == wanted)
							index = i;
					if (index < 0)
					{
						say("FAIL: no queue %s", wanted.empty() ? "(unrated)" : wanted.c_str());
						rc = 1;
						stage = Stage::Done;
						break;
					}
					say("QUEUE %s", queues[std::size_t(index)].value("id", "").c_str());
					hub->findMatch(index);
					stage = Stage::Queue;
					stageAt = now;
					break;
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
					// A short sudden-death timer ends the match on its own.
					CustomGameSetup setup;
					backend.setupDraft(setup);
					setup.suddenDeathMinutes = suddenDeath;
					backend.applySetup(setup);
					rulesSet = true;
					std::ofstream(dir + "/code") << backend.inviteCode() << "\n";
					say("ROOM %s %s sudden death %d min", backend.inviteCode().c_str(), backend.inviteLink().c_str(), suddenDeath);
				}
				if (!backend.mapStatus().empty() && now - lastShot > 5)
				{
					say("map: %s", backend.mapStatus().c_str());
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
		case Stage::Queue:
		{
			auto &search = Online::quickMatch();
			const std::string phase = Online::phaseName(search.phase());
			if (phase != lastPhase)
			{
				say("quick match %s", phase.c_str());
				lastPhase = phase;
				if (search.phase() == Online::QuickMatch::Phase::Searching)
					capture("queue-searching");
				if (search.phase() == Online::QuickMatch::Phase::Failed)
				{
					say("FAIL: quick match failed: %s", search.error().message.c_str());
					rc = 1;
					stage = Stage::Done;
				}
			}
			if (top<MatchStartScreen>(screens))
			{
				lastPhase.clear();
				stage = Stage::Starting;
				stageAt = now;
			}
			break;
		}
		case Stage::Starting:
			if (auto *start = top<MatchStartScreen>(screens))
			{
				if (lastShot < stageAt)
				{
					capture("starting");
					lastShot = now;
				}
				if (start->match().step() == Online::OnlineMatch::Step::Failed)
				{
					say("FAIL: %s", start->match().failure().c_str());
					rc = 1;
					stage = Stage::Done;
				}
				matchId = start->match().matchId();
			}
			else if (top<GameSessionScreen>(screens))
			{
				say("PLAYING match %s after %.1f s", matchId.c_str(), now - stageAt);
				stage = Stage::Playing;
				stageAt = now;
				lastShot = now;
			}
			break;
		case Stage::Playing:
		{
			auto *session = top<GameSessionScreen>(screens);
			Engine *engine = session ? session->runningEngine() : nullptr;
			if (engine)
			{
				const std::string line = gameLine(*engine);
				// The other seats' states without the grace countdown.
				std::string presence;
				if (auto *turn = engine->turnSession())
					for (int seat = 0; seat < int(Turn::MAX_SEATS); ++seat)
						if ((turn->humanSeatMask() & (1u << seat)) && seat != turn->localSeat())
							presence += std::string(" ") + presenceName(turn->seatPresenceInfo(seat).state);
				if (now - lastLine > 5 || presence != lastPresence)
				{
					say("%s", line.c_str());
					lastLine = now;
					if (presence != lastPresence && !lastPresence.empty())
						capture("ingame-presence-" + std::to_string(int(now - stageAt)));
					lastPresence = presence;
				}
				if (endedAt < 0 && (engine->gui.game.isGameEnded || engine->gui.getLocalTeam()->hasWon || engine->gui.getLocalTeam()->hasLost))
				{
					endedAt = now;
					say("GAME END at tick %u: %s", engine->gui.game.stepCounter, line.c_str());
					capture("game-end");
				}
			}
			if (now - lastShot > 15)
			{
				capture("ingame-" + std::to_string(int(now - stageAt)));
				lastShot = now;
			}
			const int leaveAt = role == "guest" ? guestLeave : role == "quick" ? quickLeave : 0;
			if (!left && leaveAt > 0 && now - stageAt > leaveAt && engine)
			{
				left = true;
				if (leaveByMenu || role == "quick")
				{
					// The in-game Quit: a sequenced PlayerQuitsGameOrder, then the results.
					say("LEAVE by the in-game Quit after %.1f s of play", now - stageAt);
					engine->gui.orderQueue.push_back(std::make_shared<PlayerQuitsGameOrder>(engine->gui.localPlayer));
					engine->gui.flushOutgoingAndExit = true;
				}
				else
				{
					say("LEAVE by closing the window after %.1f s of play", now - stageAt);
					SDL_Event quit{};
					quit.type = SDL_QUIT;
					events.push_back(quit);
				}
			}
			// The end dialog ("You have won!", the sudden-death timer): Ok (Enter) leads
			// to the results screen a few seconds after it appears.
			if (endedAt >= 0 && now - endedAt > 4 && now - lastKey > 2)
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
			if (top<EndGameScreen>(screens))
			{
				say("RESULTS after %.1f s of play", now - stageAt);
				stage = Stage::Results;
				stageAt = now;
				lastShot = 0;
				lastPhase.clear();
			}
			break;
		}
		case Stage::Results:
			if (lastShot == 0 && now - stageAt > 2)
			{
				capture("results");
				lastShot = now;
			}
			if (auto *results = top<EndGameScreen>(screens))
			{
				const auto &online = results->onlineResult();
				if (!online)
				{
					say("FAIL: no online result");
					rc = 1;
					stage = Stage::Done;
					break;
				}
				const std::string phase = std::string(phaseName(online->phase())) + (online->slow ? " (slow)" : "") +
										  " status=" + (online->status.empty() ? "?" : online->status) +
										  " verification=" + verificationName(online->verification) +
										  " outcome=" + (online->outcome.empty() ? "?" : online->outcome);
				if (phase != lastPhase)
				{
					say("RESULT %s after %.1f s on the results screen", phase.c_str(), now - stageAt);
					lastPhase = phase;
					capture("results-" + std::string(phaseName(online->phase())) + (online->slow ? "-slow" : ""));
				}
				const bool done = online->phase() == Online::OnlineMatchResult::Phase::Done;
				if ((done && now - stageAt > 3) || now - stageAt > 240)
				{
					if (!done)
					{
						say("FAIL: the result did not settle within 240 s");
						rc = 1;
					}
					capture("results-final");
					if (now - lastKey > 2)
					{
						lastKey = now;
						press(screens, "quit", events);
					}
				}
			}
			else if (auto *room = top<RoomScreen>(screens))
			{
				capture("back-in-room");
				say("back in the room");
				room->backend().leave();
				stage = Stage::Done;
			}
			else if (top<OnlineHubScreen>(screens) || top<QuickMatchScreen>(screens))
			{
				capture("back-online");
				say("back online");
				stage = Stage::Done;
			}
			break;
		case Stage::Done:
			break;
		}
		screens.frame(SDL_GetTicks(), events);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	drainRelayConnections();
	say("%s", rc ? "FAIL" : "PASS");
	return rc;
}
} // namespace

int main(int argc, char **argv)
{
	if (argc != 4)
	{
		std::fprintf(stderr, "Usage: %s host|guest|quick <origin> <directory>\n", argv[0]);
		return 2;
	}
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	began = std::chrono::steady_clock::now();
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
