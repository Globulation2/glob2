// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "LANFindScreen.h"
#include "GlobalContainer.h"
#include "LANSessionScreen.h"
#include "LanRoom.h"
#include "MessageScreen.h"
#include "FormatableString.h"
#include <stdexcept>
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
		field(tr("[lan pairing field]"), textField("server", serverName, [this](const std::string &v) { serverName = v; })),
		field(tr("[player name]"), textField("player", playerName, [this](const std::string &v) { playerName = v; }, {false, 32})),
	});
	auto gameList = column({heading(tr("[available lan games]")),
							hint(tr(listener.isListening() ? "[lan discovery instructions]" : "[lan discovery unavailable]")),
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
				actions({{"connect", tr("[lan pairing connect]"), [this] { connect(); }, true, SDLK_RETURN},
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
	try {
        const auto endpoint = NetEndpoint::parse(serverName);
        if (endpoint.route != "/yog") throw std::invalid_argument(tr("[lan lobby endpoint required]"));
    } catch (const std::exception& error) {
        screens.push(std::make_unique<MessageScreen>(error.what(), std::vector<std::string>{tr("[ok]")}));
        return;
    }
	Lan::LanClient::Options options;
	options.endpoint = serverName;
	options.name = playerName;
	listener.disableListening();
	screens.push(std::make_unique<LANSessionScreen>(screens, Lan::LanRoom::join(std::move(options))),
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
	for (const auto &host : listener.getLANHosts())
        names.push_back(GAGCore::FormattableString(tr("[lan discovered host %0]")).arg(host.identifier.substr(0, 8)));
	if (names != games)
	{
		games = std::move(names);
		selectedGame = std::min(selectedGame, int(games.size()) - 1);
		invalidate();
	}
}
