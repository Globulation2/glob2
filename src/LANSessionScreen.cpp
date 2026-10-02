// SPDX-License-Identifier: GPL-3.0-or-later
#include "LANSessionScreen.h"
#include "LanRoom.h"
#include "MessageScreen.h"
#include "MultiplayerGameScreen.h"
#include "SessionTabsScreen.h"
#include <ScreenStack.h>

using namespace GAGGUI;
namespace
{
class LANGameScreen final : public SessionTabsScreen
{
	std::shared_ptr<Lan::LanRoom> lanRoom;
	MultiplayerGameScreen room;

  public:
	LANGameScreen(ScreenStack &screens, std::shared_ptr<Lan::LanRoom> lanRoom)
		: lanRoom(lanRoom), room(screens, lanRoom)
	{
		addTab(&room, true);
	}
	~LANGameScreen() override { removeTab(&room); }
};
} // namespace
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
	screens.push(std::make_unique<LANGameScreen>(screens, lanRoom),
				 [this](GAGGUI::Screen &, int result)
				 {
					 if (!lanRoom->endMessage().empty() && result != GAGGUI::Screen::QUIT_APPLICATION)
					 {
						 stage = Stage::Failed;
						 screens.push(std::make_unique<MessageScreen>(lanRoom->endMessage(),
																	  std::vector<std::string>{Glob2UI::tr("[ok]")}),
									  [this, result](GAGGUI::Screen &, int) { endExecute(result); });
						 return;
					 }
					 endExecute(result);
				 });
}
