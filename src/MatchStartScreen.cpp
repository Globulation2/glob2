// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#include "MatchStartScreen.h"
#include "Engine.h"
#include "GUIMapPreview.h"
#include "GameSessionScreen.h"
#include "OnlineMatch.h"
#include <FormatableString.h>
#include <SDL3/SDL.h>
#include <random>

using namespace Glob2UI;
using Online::OnlineMatch;

MatchStartScreen::MatchStartScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<OnlineMatch> match)
	: screens(screens), flow(std::move(match)), preview(std::make_unique<MapPreview>())
{
	tip = int(std::random_device{}() % 3);
}

MatchStartScreen::~MatchStartScreen() = default;

void MatchStartScreen::leave()
{
	if (playing)
		return;
	flow->leave();
	endExecute(LEFT);
}

void MatchStartScreen::onTimer(Uint32 tick)
{
	if (playing)
		return;
	flow->update(SDL_GetTicks());
	const int step = int(flow->step());
	if (step != lastStep || tick - lastRefresh > 250)
	{
		lastStep = step;
		lastRefresh = tick;
		if (!flow->mapFile().empty() && flow->mapFile() != previewFile)
		{
			previewFile = flow->mapFile();
			preview->setMapThumbnail(previewFile);
		}
		invalidate();
	}
	if (flow->step() == OnlineMatch::Step::Playing)
	{
		auto engine = flow->takeEngine();
		if (!engine)
			return;
		playing = true;
		screens.push(std::make_unique<GameSessionScreen>(screens, std::move(engine)),
					 [this](GAGGUI::Screen &, int result) {
						 endExecute(result == QUIT_APPLICATION ? QUIT_APPLICATION : PLAYED);
					 });
	}
}

Element MatchStartScreen::steps(const Presentation &p)
{
	using Step = OnlineMatch::Step;
	const Step now = flow->step();
	const auto palette = theme().palette;
	auto line = [&](const std::string &text, Step step, const std::string &detail, Element extra = nullptr) {
		const bool done = now == Step::Playing || (now != Step::Failed && int(now) > int(step));
		const bool active = now == step;
		IconOptions mark;
		mark.size = 18;
		mark.color = done ? palette.success : palette.muted;
		Element glyph = done ? icon(uiIcon(UIIcon::Check), mark)
					  : active ? icon(uiIcon(UIIcon::Spinner), mark)
							   : sized({p.pt(18), p.pt(18)}, canvas("", {p.pt(18), p.pt(18)}, [palette](Canvas &c, Rect r, const Frame &) {
									 c.strokeRect({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, palette.line);
								 }));
		TextOptions words;
		words.muted = !done && !active;
		std::vector<Element> cells{glyph, expanded(label(text, words))};
		if (!detail.empty())
			cells.push_back(caption(detail));
		auto rowElement = row(std::move(cells), {p.pt(8), CrossAlign::Center});
		return extra ? column({rowElement, padding({p.pt(26), 0, 0, 0}, extra)}, {p.pt(4)}) : rowElement;
	};
	std::vector<Element> lines;
	lines.push_back(line(tr("[match seat confirmed]"), Step::Seat, ""));
	const std::string mapTitle = flow->mapTitle();
	std::string mapText = mapTitle.empty() ? tr("[match map]") : GAGCore::FormattableString(tr("[match downloading map %0]")).arg(mapTitle);
	std::string mapDetail = int(now) > int(Step::Map) && flow->mapWasCached() ? tr("[match map cached]") : "";
	lines.push_back(line(mapText, Step::Map, mapDetail));
	lines.push_back(line(tr("[match loading]"), Step::Load, ""));
	std::string relay = GAGCore::FormattableString(tr("[match relay %0]")).arg(flow->relayName());
	if (flow->relayRttMs() >= 0)
		relay += " · " + std::to_string(flow->relayRttMs()) + " ms";
	lines.push_back(line(relay, Step::Relay, ""));
	lines.push_back(line(tr("[match waiting for players]"), Step::Players, ""));
	return column(std::move(lines), {p.pt(p.touch ? 10 : 8)});
}

Element MatchStartScreen::players(const Presentation &p)
{
	std::vector<Element> rows;
	for (const auto &player : flow->players())
	{
		std::string name = player.local ? GAGCore::FormattableString(tr("[match you %0]")).arg(player.name) : player.name;
		if (player.ai)
			name += " · " + tr("[match ai]");
		std::vector<Element> cells{swatch(GAGCore::Color(player.r, player.g, player.b), 14), expanded(label(name))};
		if (!player.ai)
			cells.push_back(width(p.pt(p.compact() ? 70 : 160), progress(std::max(0, player.progress), 100)));
		cells.push_back(width(p.pt(p.compact() ? 64 : 80), caption(tr("[match player " + player.state + "]"))));
		rows.push_back(row(std::move(cells), {p.pt(8), CrossAlign::Center}));
	}
	return column(std::move(rows), {p.pt(6)});
}

Element MatchStartScreen::build(const Presentation &p)
{
	const bool failed = flow->step() == OnlineMatch::Step::Failed;
	const bool narrow = p.compact();
	const auto &ctx = flow->context();
	std::vector<Element> body;
	auto heading = narrow ? column({Glob2UI::heading(tr("[match starting]")), caption(ctx.label + (flow->mapTitle().empty() ? "" : " · " + flow->mapTitle()))}, {p.pt(2)})
						  : row({expanded(title(tr("[match starting]"))), caption(ctx.label)}, {p.pt(8), CrossAlign::Center});
	body.push_back(heading);
	// The preview appears once the map is on this device.
	Element map = previewFile.empty() ? nullptr : mapPreview("map", *preview, narrow ? 150 : 180);
	if (failed)
	{
		body.push_back(row({icon(uiIcon(UIIcon::Warning), {24, theme().palette.danger}),
							expanded(paragraph(tr("[match failed]") + (flow->failure().empty() ? "" : "\n" + flow->failure())))},
						   {p.pt(8), CrossAlign::Start}));
	}
	else if (narrow || !map)
	{
		if (map)
			body.push_back(center(map));
		body.push_back(steps(p));
	}
	else
		body.push_back(row({map, expanded(steps(p))}, {p.pt(16), CrossAlign::Start}));
	body.push_back(divider());
	body.push_back(players(p));
	if (!narrow && !failed)
		body.push_back(paragraph(tr(tip == 0 ? "[match tip select]" : tip == 1 ? "[match tip flags]" : "[match tip input]"), {FontRole::Support, true}));
	std::vector<MenuAction> buttons;
	buttons.push_back({"leave", tr(failed ? "[Back]" : "[match leave]"), [this] { leave(); }, failed, SDLK_ESCAPE});
	auto content = scroll("match/scroll", column(std::move(body), {p.pt(10)}));
	auto actionRow = actions(std::move(buttons), p, ActionStyle::Compact);
	CardOptions options;
	options.padding = p.pt(narrow ? 12 : 20);
	if (p.touch)
		return center(maxWidth(p.pt(560), card(footer(content, actionRow), options)));
	const int w = std::min(p.safe.w - p.pt(32), p.pt(700));
	const int h = std::min(p.safe.h - p.pt(32), p.pt(560));
	return center(sized({w, h}, card(footer(content, actionRow), options)));
}
