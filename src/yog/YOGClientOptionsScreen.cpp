// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#include "YOGClientOptionsScreen.h"
#include "YOGClient.h"
#include "YOGClientBlockedList.h"
#include <algorithm>

namespace fe = Glob2UI;

YOGClientOptionsScreen::YOGClientOptionsScreen(std::shared_ptr<YOGClient> client) : client(client) { updateBlockedPlayerList(); }

std::string YOGClientOptionsScreen::title() const { return fe::tr("[Options]"); }

void YOGClientOptionsScreen::onActivated() { updateBlockedPlayerList(); }

void YOGClientOptionsScreen::updateBlockedPlayerList()
{
	// The list exists once the client is logged in; before that there is nothing to show.
	blocked.clear();
	if (auto list = client->getBlockedList())
		blocked.assign(list->getBlockedPlayers().begin(), list->getBlockedPlayers().end());
	selected = std::min(selected, int(blocked.size()) - 1);
	refresh();
}

void YOGClientOptionsScreen::addBlocked()
{
	auto list = client->getBlockedList();
	if (list && !draft.empty() && !list->isPlayerBlocked(draft))
	{
		list->addBlockedPlayer(draft);
		list->save();
	}
	draft.clear();
	updateBlockedPlayerList();
}

void YOGClientOptionsScreen::removeBlocked()
{
	auto list = client->getBlockedList();
	if (!list || selected < 0 || selected >= int(blocked.size()))
		return;
	list->removeBlockedPlayer(blocked[std::size_t(selected)]);
	list->save();
	updateBlockedPlayerList();
}

fe::Element YOGClientOptionsScreen::build(const fe::Presentation &p)
{
	fe::TextFieldOptions options;
	options.submit = [this](const std::string &) { addBlocked(); };
	auto body = fe::scroll("options/scroll",
						   fe::column({fe::label(fe::tr("[Blocked Players]")),
									   fe::listView("options/blocked", blocked, selected, [this](int i) { selected = i; }, {{}, {}, {}, {}, {}, 8, fe::tr("[No items]")}),
									   fe::wrap({fe::button("options/remove", fe::tr("[Remove]"), [this] { removeBlocked(); }, {false, false, selected >= 0})}, {-1, p.pt(140)}),
									   fe::field(fe::tr("[Add]"), fe::textField("options/name", draft, [this](const std::string &v) { draft = v; }, options)),
									   fe::wrap({fe::button("options/add", fe::tr("[Add]"), [this] { addBlocked(); }, {false, false, !draft.empty()})}, {-1, p.pt(140)})},
									  {p.pt(8)}));
	return fe::column({fe::expanded(body), fe::divider(), fe::actions({{"options/quit", fe::tr("[quit]"), [this] { finish(QUIT); }, false, SDLK_ESCAPE}}, p)}, {p.pt(8)});
}
