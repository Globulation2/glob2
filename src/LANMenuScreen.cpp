// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "LANMenuScreen.h"
#include "ChooseMapScreen.h"
#include "FormatableString.h"
#include "GlobalContainer.h"
#include "LANFindScreen.h"
#include "LANSessionScreen.h"
#include "MessageScreen.h"
#include "YOGClient.h"
#include "YOGServer.h"
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
			auto client = std::make_shared<YOGClient>();
			auto server = std::make_shared<YOGServer>(YOGAnonymousLogin, YOGSingleGame);
			if (!server->isListening())
			{
				screens.push(std::make_unique<MessageScreen>(
					FormattableString(tr("[Can't host game, port %0 in use]")).arg(YOG_SERVER_PORT),
					std::vector<std::string>{tr("[ok]")}));
				return;
			}
			server->enableLANBroadcasting();
			client->attachGameServer(server);
			client->connect("127.0.0.1");
			screens.push(std::make_unique<LANSessionScreen>(
							 screens, client, globalContainer->settings.getUsername(),
							 static_cast<ChooseMapScreen &>(selection).getMapHeader()),
						 [this](GAGGUI::Screen &, int) { endExecute(HostedGame); });
		});
}
