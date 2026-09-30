// SPDX-License-Identifier: GPL-3.0-or-later
#include "SessionTabsScreen.h"
#include <algorithm>

namespace fe = Glob2UI;

void SessionTab::refresh()
{
	if (session)
		session->invalidate();
}

SessionTabsScreen::SessionTabsScreen() = default;
SessionTabsScreen::~SessionTabsScreen() = default;

void SessionTabsScreen::addTab(SessionTab *tab, bool primary)
{
	tab->session = this;
	tabs.push_back({tab, primary});
	if (!active)
		activate(tab);
	invalidate();
}

void SessionTabsScreen::removeTab(SessionTab *tab)
{
	tabs.erase(std::remove_if(tabs.begin(), tabs.end(), [&](const Entry &e) { return e.tab == tab; }), tabs.end());
	if (active == tab)
	{
		active = nullptr;
		if (!tabs.empty())
			activate(tabs.front().tab);
	}
	tab->session = nullptr;
	invalidate();
}

void SessionTabsScreen::activate(SessionTab *tab)
{
	for (auto &entry : tabs)
		entry.tab->activated = entry.tab == tab;
	active = tab;
	host().closePopup();
	host().endEditing();
	invalidate();
	if (tab)
		tab->onActivated();
}

void SessionTabsScreen::onEscape()
{
	if (active && active->onEscape())
		return;
	endExecute(-1);
}

void SessionTabsScreen::onTimer(Uint32 tick)
{
	// A tab's timer can remove and destroy another tab (the lobby owns the
	// game room), so only tick tabs that are still registered.
	for (auto &entry : std::vector<Entry>(tabs))
	{
		const bool present = std::any_of(tabs.begin(), tabs.end(), [&](const Entry &e) { return e.tab == entry.tab; });
		if (present && entry.tab)
			entry.tab->onTimer(tick);
	}
	for (auto &entry : tabs)
		if (entry.primary && entry.tab->finished())
		{
			endExecute(entry.tab->returnCode());
			return;
		}
}

fe::Element SessionTabsScreen::build(const fe::Presentation &p)
{
	std::vector<fe::Element> parts;
	if (tabs.size() > 1)
	{
		std::vector<std::string> titles;
		int selected = 0;
		for (std::size_t i = 0; i < tabs.size(); ++i)
		{
			titles.push_back(tabs[i].tab->title());
			if (tabs[i].tab == active)
				selected = int(i);
		}
		parts.push_back(fe::segments("session/tab", titles, selected, [this](int i)
									 {
										 if (i >= 0 && i < int(tabs.size()))
											 activate(tabs[std::size_t(i)].tab);
									 }));
	}
	parts.push_back(fe::expanded(active ? active->build(p) : fe::empty()));
	fe::CardOptions cardOptions;
	cardOptions.padding = p.pt(p.compact() ? 10 : 16);
	return fe::center(fe::maxWidth(p.pt(1120), fe::card(fe::column(std::move(parts), {p.pt(8)}), cardOptions)));
}
