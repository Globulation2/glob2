// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "LANMenuScreen.h"
#include "ChooseMapScreen.h"
#include "FormatableString.h"
#include "GlobalContainer.h"
#include "LANFindScreen.h"
#include "LANSessionScreen.h"
#include "LanRoom.h"
#include "MessageScreen.h"
#include <ScreenStack.h>

using namespace Glob2UI;

LANMenuScreen::LANMenuScreen(GAGGUI::ScreenStack &screens) : screens(screens) {}

Element LANMenuScreen::build(const Presentation &p)
{
	return menu(tr("[lan]"),
				{{"host", tr("[host]"), [this] { host(); }, true},
				 {"join", tr("[join a game]"), [this] { join(); }},
				 {"back", tr("[goto main menu]"), [this] { endExecute(QuitMenu); }, false, SDLK_ESCAPE}},
				p);
}

void LANMenuScreen::join()
{
	screens.push(std::make_unique<LANFindScreen>(screens),
				 [this](GAGGUI::Screen &, int) { endExecute(JoinedGame); });
}

void LANMenuScreen::host()
{
	screens.push(
		std::make_unique<ChooseMapScreen>("maps", "map", false, "games", "game", false),
		[this](GAGGUI::Screen &selection, int result)
		{
			if (result != ChooseMapScreen::OK)
				return;
			Lan::LanHost::Options options;
			options.hostName = globalContainer->settings.getUsername();
			options.map = static_cast<ChooseMapScreen &>(selection).getMapHeader();
			std::shared_ptr<Lan::LanRoom> room;
			try
			{
				room = Lan::LanRoom::host(std::move(options));
			}
			catch (const std::exception &error)
			{
				screens.push(std::make_unique<MessageScreen>(
					FormattableString(tr("[lan cannot host %0]")).arg(error.what()),
					std::vector<std::string>{tr("[ok]")}));
				return;
			}
			screens.push(std::make_unique<LANSessionScreen>(screens, room),
						 [this](GAGGUI::Screen &, int) { endExecute(HostedGame); });
		});
}
