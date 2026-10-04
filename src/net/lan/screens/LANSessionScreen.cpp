// SPDX-License-Identifier: GPL-3.0-or-later
#include "LANSessionScreen.h"
#include "LanRoom.h"
#include "MessageScreen.h"
#include "RoomScreen.h"
#include <ScreenStack.h>

using namespace GAGGUI;
LANSessionScreen::LANSessionScreen(ScreenStack &screens, std::shared_ptr<Lan::LanRoom> room)
	: screens(screens), lanRoom(std::move(room))
{
}
Glob2UI::Element LANSessionScreen::build(const Glob2UI::Presentation &p)
{
	using namespace Glob2UI;
	return page("", center(paragraph(tr("[connecting to game]"), {FontRole::Body, false, TextAlign::Center})),
				actions({{"cancel", tr("[Cancel]"), [this] { endExecute(0); }, false, SDLK_ESCAPE}}, p), p, 480);
}
LANSessionScreen::~LANSessionScreen()
{
	lanRoom->leave();
}
void LANSessionScreen::fail(const std::string &message)
{
	stage = Stage::Failed;
	screens.push(std::make_unique<MessageScreen>(message, std::vector<std::string>{Glob2UI::tr("[ok]")}),
				 [this](GAGGUI::Screen &, int) { endExecute(1); });
}
void LANSessionScreen::onTimer(Uint32 tick)
{
	if (stage != Stage::Connecting)
		return;
	if (!stageStarted)
		stageStarted = tick;
	lanRoom->update();
	if (lanRoom->lobbyReady())
	{
		enterLobby();
		return;
	}
	while (auto event = lanRoom->takeEvent())
		if (event->kind == RoomBackend::Event::Finished)
		{
			fail(event->text.empty() ? Glob2UI::tr("[lan connection unavailable]") : event->text);
			return;
		}
	if (Uint32(tick - *stageStarted) >= 10000)
	{
		lanRoom->leave();
		fail(Glob2UI::tr("[lan connection unavailable]"));
	}
}
void LANSessionScreen::enterLobby()
{
	stage = Stage::Lobby;
	// The shared Room screen (multiplayer mock-up 2, state C) on the LanRoom backend.
	// It shows why the room ended (host left, refusal, lost connection) itself.
	screens.push(std::make_unique<RoomScreen>(screens, lanRoom),
				 [this](GAGGUI::Screen &, int result) { endExecute(result); });
}
