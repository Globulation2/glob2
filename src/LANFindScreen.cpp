// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "LANFindScreen.h"
#include "GlobalContainer.h"
#include "LANSessionScreen.h"
#include "YOGClient.h"
#include <ScreenStack.h>

using namespace Glob2UI;

LANFindScreen::LANFindScreen(GAGGUI::ScreenStack &screens)
	: screens(screens), playerName(globalContainer->settings.getUsername())
{
}

LANFindScreen::~LANFindScreen() = default;

Element LANFindScreen::build(const Presentation &p)
{
	auto formPart = form({
		field(tr("[svr hostname]"), textField("server", serverName, [this](const std::string &v) { serverName = v; })),
		field(tr("[player name]"), textField("player", playerName, [this](const std::string &v) { playerName = v; }, {false, 32})),
	});
	auto gameList = column({label(tr("[available lan games]")),
							listView("games", games, selectedGame,
									 [this](int i)
									 {
										 selectedGame = i;
										 serverName = listener.getIPAddress(i);
									 },
									 {{}, {}, {}, [this](int) { connect(); }, {}, 6, tr("[No items]")})});
	Element body = adaptive(
		[formPart, gameList](const LayoutContext &ctx, Size available)
		{
			if (available.w < ctx.presentation.pt(640))
				return scroll("find/scroll", column({formPart, gameList}));
			return row({expanded(formPart), expanded(gameList)}, {-1, CrossAlign::Start});
		});
	return page(tr("[join a game]"), body,
				actions({{"connect", tr("[connect]"), [this] { connect(); }, true, SDLK_RETURN},
						 {"back", tr("[goto main menu]"), [this] { endExecute(QUIT); }, false, SDLK_ESCAPE}},
						p),
				p, 800);
}

void LANFindScreen::setServer(const std::string &address)
{
	serverName = address;
	invalidate();
}

void LANFindScreen::connect()
{
	auto client = std::make_shared<YOGClient>();
	client->connect(serverName);
	listener.disableListening();
	screens.push(std::make_unique<LANSessionScreen>(screens, client, playerName),
				 [this](GAGGUI::Screen &, int result)
				 {
					 listener.enableListening();
					 if (result == GAGGUI::Screen::QUIT_APPLICATION)
						 endExecute(result);
				 });
}

void LANFindScreen::onTimer(Uint32)
{
	listener.update();
	std::vector<std::string> names;
	for (const auto &game : listener.getLANGames())
		names.push_back(game.getGameInformation().getGameName());
	if (names != games)
	{
		games = std::move(names);
		selectedGame = std::min(selectedGame, int(games.size()) - 1);
		invalidate();
	}
}
