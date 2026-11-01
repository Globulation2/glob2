// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#include "ConnectionOverlay.h"
#include "ConnectionQuality.h"
#include "GlobalContainer.h"
#include "InGameTouchTheme.h"
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <cmath>

using ConnectionQuality::Metric;
using ConnectionQuality::Rating;
using GAGCore::Color;
using GAGCore::Font;

namespace
{
std::string text(const char *key)
{
	return GAGCore::Toolkit::getStringTable()->getString(key);
}
std::string text(const char *key, const std::string &value)
{
	return GAGCore::FormattableString(text(key)).arg(value);
}
std::string text(const char *key, const std::string &a, const std::string &b)
{
	return GAGCore::FormattableString(text(key)).arg(a).arg(b);
}

std::string clock(int seconds)
{
	seconds = std::max(0, seconds);
	char buffer[16];
	std::snprintf(buffer, sizeof buffer, "%d:%02d", seconds / 60, seconds % 60);
	return buffer;
}

const char *const DOT = " \xC2\xB7 ";

// The classic in-match look (navy and gold) and the touch HUD's aubergine.
struct Look
{
	Color paper, edge, ink, muted, own;
};
Look look(bool touch)
{
	if (touch)
		return {InGameTouchTheme::paper(), InGameTouchTheme::border(), InGameTouchTheme::ink(),
				Color(190, 170, 140), InGameTouchTheme::selected()};
	return {Color(8, 10, 40, 215), Color(200, 200, 230, 120), Color(240, 240, 240),
			Color(160, 165, 200), Color(52, 56, 112, 230)};
}

// Quality colours. Never the only signal: every rating also has a word and a marker
// shape (good: disc, fair: ring, poor: triangle).
const Color goodInk(110, 205, 110), fairInk(235, 185, 60), poorInk(235, 95, 80),
	gone(140, 140, 150);

Color ratingColor(Rating r)
{
	return r == Rating::Good ? goodInk : r == Rating::Fair ? fairInk : poorInk;
}

std::string ratingWord(Rating r)
{
	return text(r == Rating::Good   ? "[conn good]"
				: r == Rating::Fair ? "[conn fair]"
									: "[conn poor]");
}

// What a player's row shows: Ping normally; Behind once their game runs a second or
// more behind (that is what the other players notice); Behind alone where no round
// trip was measured (LAN, older relays).
struct Reading
{
	bool known = false;
	Metric metric = Metric::Ping;
	int ms = 0;
	Rating rating = Rating::Good;
};

Reading reading(const ConnectionRow &row)
{
	Reading r;
	const bool behindKnown = row.behindMs >= 0;
	const Rating behind =
		behindKnown ? ConnectionQuality::rate(Metric::Behind, row.behindMs) : Rating::Good;
	if (behindKnown && (behind != Rating::Good || row.pingMs < 0))
	{
		r = {true, Metric::Behind, row.behindMs, behind};
	}
	else if (row.pingMs >= 0)
	{
		r = {true, Metric::Ping, row.pingMs, ConnectionQuality::rate(Metric::Ping, row.pingMs)};
	}
	// A relay Slow state overrides the numeric metric with a Poor rating.
	if (row.state == ConnectionRow::State::Slow)
		r.rating = Rating::Poor;
	return r;
}

// The marker colour and shape of a row (shape 0 disc, 1 ring, 2 triangle, -1 none).
struct Mark
{
	Color color;
	int shape;
};

Mark mark(const ConnectionRow &row)
{
	switch (row.state)
	{
	case ConnectionRow::State::Connected:
	case ConnectionRow::State::Slow:
	{
		const Reading r = reading(row);
		if (!r.known)
			return {gone, 1};
		return {ratingColor(r.rating), int(r.rating)};
	}
	case ConnectionRow::State::Resyncing:
		return {fairInk, 1};
	case ConnectionRow::State::Reconnecting:
		return {poorInk, 2};
	case ConnectionRow::State::AI:
		return {gone, -1};
	default:
		return {gone, 1};
	}
}

void marker(int x, int y, int radius, const Mark &m)
{
	auto *gfx = globalContainer->gfx;
	switch (m.shape)
	{
	case 0: // disc
		for (int r = 1; r <= radius; ++r)
			gfx->drawCircle(x, y, r, m.color);
		gfx->drawFilledRect(x - radius / 2, y - radius / 2, radius, radius, m.color);
		break;
	case 1: // ring
		gfx->drawCircle(x, y, radius, m.color);
		gfx->drawCircle(x, y, std::max(1, radius - 1), m.color);
		break;
	case 2: // triangle, point up
		for (int i = 0; i <= 2 * radius; ++i)
		{
			const int half = (i + 1) / 2;
			gfx->drawHorzLine(x - half, y - radius + i, 2 * half + 1, m.color);
		}
		break;
	default:
		break;
	}
}

// Text is drawn at the touch HUD's scale on phones (as TouchReadout does).
double textScale = 1;
int tw(Font *font, const std::string &value)
{
	return int(font->getStringWidth(value) * textScale);
}
int th(Font *font, const std::string &value)
{
	return int(font->getStringHeight(value) * textScale);
}

void textAt(int x, int y, Font *font, const std::string &value, const Color &color)
{
	font->pushStyle(Font::Style(Font::STYLE_NORMAL, color));
	if (textScale != 1)
	{
		globalContainer->gfx->setUITransform(textScale, x, y);
		globalContainer->gfx->drawString(0, 0, font, value);
		globalContainer->gfx->setUITransform();
	}
	else
		globalContainer->gfx->drawString(x, y, font, value);
	font->popStyle();
}

void textRight(int right, int y, Font *font, const std::string &value, const Color &color)
{
	textAt(right - tw(font, value), y, font, value, color);
}

// Cuts whole UTF-8 characters until the text and an ellipsis fit.
std::string ellipsize(Font *font, std::string value, int width)
{
	if (width <= 0 || tw(font, value) <= width)
		return value;
	std::string base = value;
	do
	{
		base.pop_back();
		while (!base.empty() && (static_cast<unsigned char>(base.back()) & 0xC0) == 0x80)
			base.pop_back();
		value = base + "…";
	} while (!base.empty() && tw(font, value) > width);
	return value;
}

// Word wrap to a pixel width.
std::vector<std::string> wrap(Font *font, const std::string &value, int width)
{
	std::vector<std::string> lines;
	std::string line, word;
	auto flush = [&]
	{
		if (!word.empty())
		{
			const std::string candidate = line.empty() ? word : line + " " + word;
			if (!line.empty() && tw(font, candidate) > width)
			{
				lines.push_back(line);
				line = word;
			}
			else
				line = candidate;
			word.clear();
		}
	};
	for (char c : value)
	{
		if (c == ' ')
			flush();
		else if (c == '\n')
		{
			flush();
			lines.push_back(line);
			line.clear();
		}
		else
			word += c;
	}
	flush();
	if (!line.empty())
		lines.push_back(line);
	return lines;
}

// A state other than playing, as a word ("Reconnecting 2:31"); empty while playing.
std::string stateText(const ConnectionRow &row)
{
	switch (row.state)
	{
	case ConnectionRow::State::Reconnecting:
		return row.graceSeconds >= 0 ? text("[conn reconnecting]") + " " + clock(row.graceSeconds)
									 : text("[conn reconnecting]");
	case ConnectionRow::State::Resyncing:
		return text("[conn resyncing]");
	case ConnectionRow::State::Waiting:
		return text("[conn waiting]");
	case ConnectionRow::State::Left:
		return text("[conn left]");
	case ConnectionRow::State::AI:
		return text("[conn ai]");
	default:
		return {};
	}
}

// The panel's value for a row: "42 ms · Good", "Behind 2.4 s · Poor", or the state.
std::string panelValue(const ConnectionRow &row)
{
	std::string state = stateText(row);
	if (!state.empty())
		return state;
	const Reading r = reading(row);
	if (!r.known)
		return "–";
	const std::string value = ConnectionQuality::format(r.metric, r.ms);
	return (r.metric == Metric::Behind ? text("[conn behind %0]", value) : value) + DOT +
		   ratingWord(r.rating);
}

// The compact grid's value: the number alone (the legend in the details names it).
std::string gridValue(const ConnectionRow &row)
{
	switch (row.state)
	{
	case ConnectionRow::State::Reconnecting:
		return row.graceSeconds >= 0 ? clock(row.graceSeconds) : "↻";
	case ConnectionRow::State::Left:
		return text("[conn left]");
	case ConnectionRow::State::AI:
		return text("[conn ai]");
	case ConnectionRow::State::Waiting:
	case ConnectionRow::State::Resyncing:
		return "…";
	default:
		break;
	}
	const Reading r = reading(row);
	if (!r.known)
		return "–";
	return r.metric == Metric::Behind ? ConnectionQuality::format(Metric::Behind, r.ms)
									  : std::to_string(r.ms);
}

std::string valueOrDash(Metric metric, int ms)
{
	return ms >= 0 ? ConnectionQuality::format(metric, ms) : "–";
}

/// "Good under 150 ms, poor from 300 ms" values for the explanations.
std::string limitText(Metric metric, bool poor)
{
	const auto l = ConnectionQuality::limits(metric);
	std::string value = ConnectionQuality::format(metric, poor ? l.poorMs : l.fairMs);
	const std::string tenth = ".0 s";
	if (value.size() > tenth.size() &&
		value.compare(value.size() - tenth.size(), tenth.size(), tenth) == 0)
		value = value.substr(0, value.size() - tenth.size()) + " s";
	return value;
}
} // namespace

std::string ConnectionOverlay::delayLine(const ConnectionSnapshot &snapshot)
{
	if (snapshot.inputDelayMs < 0)
		return {};
	const Rating r = ConnectionQuality::rate(Metric::Delay, snapshot.inputDelayMs);
	std::string line = text("[conn your delay %0]",
							ConnectionQuality::format(Metric::Delay, snapshot.inputDelayMs)) +
					   DOT + ratingWord(r);
	if (snapshot.ownUnstable)
		line += DOT + text("[conn unstable]");
	return line;
}

std::string ConnectionOverlay::rowText(const ConnectionRow &row)
{
	return panelValue(row);
}

bool ConnectionOverlay::inside(const SDL_Rect &r, int x, int y) const
{
	return r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

void ConnectionOverlay::observe(const ConnectionSnapshot &next)
{
	// One-line notices when someone else's connection changes.
	for (const auto &row : next.rows)
	{
		if (row.local || row.state == ConnectionRow::State::AI)
			continue;
		auto previous = seen.find(row.seat);
		if (primed && notice && previous != seen.end() && previous->second != row.state)
		{
			if (row.state == ConnectionRow::State::Reconnecting)
				notice(GAGCore::FormattableString(text("[conn notice lost %0 %1]"))
						   .arg(row.name)
						   .arg(clock(row.graceSeconds)));
			else if (row.state == ConnectionRow::State::Connected &&
					 previous->second == ConnectionRow::State::Reconnecting)
				notice(text("[conn notice back %0]", row.name));
			else if (row.state == ConnectionRow::State::Left)
				notice(text("[conn notice left %0]", row.name));
		}
		seen[row.seat] = row.state;
	}
	primed = true;
}

struct ConnectionOverlay::PanelLayout
{
	int u = 1, pad = 0, lineH = 0, x = 0, y = 0, footerH = 0, header = 0, cellW = 0, cellH = 0,
		rows = 0, width = 0;
	bool grid = false;
	std::string footer;
	SDL_Rect bounds{};
};
ConnectionOverlay::PanelLayout ConnectionOverlay::panelLayout(bool touch, SDL_Rect area,
															  double unit) const
{
	auto *font = globalContainer->standardFont, *small = globalContainer->littleFont;
	PanelLayout l;
	l.u = std::max(1, int(std::lround(unit)));
	l.pad = 6 * l.u;
	l.lineH = std::max(th(font, "Ag"), int(18 * unit));
	l.x = area.x + (touch ? 8 * l.u : 12);
	l.y = area.y + (touch ? 6 * l.u : 8);
	l.footer = delayLine(snapshot);
	l.footerH = l.footer.empty() ? 0 : th(small, "Ag") + 4 * l.u;
	l.grid = compactGrid(touch);
	if (l.grid)
	{
		l.cellW = int(64 * unit);
		l.cellH = int(20 * unit);
		l.rows = int((snapshot.rows.size() + 1) / 2);
		l.width = std::max(2 * l.cellW, l.footer.empty() ? 0 : tw(small, l.footer)) + 2 * l.pad;
		l.bounds = {l.x, l.y, l.width, l.rows * l.cellH + l.footerH + 2 * l.pad};
	}
	else
	{
		l.header = th(small, "Ag") + 4;
		int valueMax = 0;
		for (const auto &row : snapshot.rows)
			valueMax = std::max(valueMax, tw(font, panelValue(row)));
		const int swatch = touch ? 10 * l.u : 10;
		l.width = std::max({touch ? int(200 * unit) : 280,
							l.footer.empty() ? 0 : tw(small, l.footer) + 2 * l.pad,
							l.pad + swatch + 6 + int(70 * unit) + 14 * l.u + valueMax + l.pad});
		l.bounds = {l.x, l.y, l.width,
					l.header + int(snapshot.rows.size()) * l.lineH + l.footerH + 2 * l.pad};
	}
	return l;
}
struct ConnectionOverlay::DetailsLayout
{
	int u = 1, width = 0, pad = 0, lineH = 0, textH = 0, legendH = 0, buttonH = 0, headerH = 0,
		height = 0, x = 0, y = 0;
	std::vector<std::string> lines;
	SDL_Rect close{};
};
ConnectionOverlay::DetailsLayout ConnectionOverlay::detailsLayout(bool touch, SDL_Rect area,
																  double unit) const
{
	auto *font = globalContainer->standardFont, *small = globalContainer->littleFont;
	DetailsLayout l;
	l.u = std::max(1, int(std::lround(unit)));
	l.width = std::min(area.w - 24 * l.u, touch ? int(380 * unit) : 520);
	l.pad = 12 * l.u;
	l.lineH = std::max(th(font, "Ag") + 6, int(22 * unit));
	std::vector<std::string> paragraphs;
	const auto delay = delayLine(snapshot);
	if (!delay.empty())
		paragraphs.push_back(
			delay + (snapshot.jitterMs > 0
						 ? DOT + text("[conn jitter %0]", std::to_string(snapshot.jitterMs) + " ms")
						 : ""));
	paragraphs.push_back(text("[conn delay explanation %0 %1]", limitText(Metric::Delay, false),
							  limitText(Metric::Delay, true)));
	paragraphs.push_back(text("[conn ping explanation %0 %1]", limitText(Metric::Ping, false),
							  limitText(Metric::Ping, true)));
	paragraphs.push_back(text("[conn behind explanation %0 %1]", limitText(Metric::Behind, false),
							  limitText(Metric::Behind, true)));
	if (compactGrid(touch))
		paragraphs.push_back(text("[conn grid legend]"));
	if (!snapshot.relay.empty())
		paragraphs.push_back(text("[conn relay %0]", snapshot.relay));
	for (const auto &p : paragraphs)
		for (auto &line : wrap(small, p, l.width - 2 * l.pad))
			l.lines.push_back(std::move(line));
	l.textH = th(small, "Ag") + 2;
	l.legendH = l.textH + 6;
	l.buttonH = int((touch ? 44 : 28) * (touch ? unit : 1));
	l.headerH = l.textH + 4;
	l.height = l.pad + th(font, "Ag") + 8 + l.headerH + int(snapshot.rows.size()) * l.lineH + 8 +
			   l.legendH + int(l.lines.size()) * l.textH + 10 + l.buttonH + l.pad;
	l.x = area.x + (area.w - l.width) / 2;
	l.y = touch ? area.y + area.h - l.height - 8 * l.u : area.y + (area.h - l.height) / 2;
	const int buttonW = std::max(96, tw(font, text("[Close]")) + 32);
	l.close = {l.x + l.width - l.pad - buttonW, l.y + l.height - l.pad - l.buttonH, buttonW,
			   l.buttonH};
	return l;
}
struct ConnectionOverlay::CardLayout
{
	int u = 1, width = 0, pad = 0, textH = 0, lineH = 0, buttonH = 0, barH = 0, height = 0, x = 0,
		y = 0;
	bool visible = false, showProgress = false, showLeave = false;
	std::string heading;
	std::vector<std::string> bodyLines, lineLines;
	SDL_Rect leave{};
};
ConnectionOverlay::CardLayout ConnectionOverlay::cardLayout(bool touch, SDL_Rect area,
															double unit) const
{
	auto *title = globalContainer->menuFont, *font = globalContainer->standardFont,
		 *small = globalContainer->littleFont;
	CardLayout l;
	l.u = std::max(1, int(std::lround(unit)));
	std::string line, body;
	switch (snapshot.card)
	{
	case ConnectionSnapshot::Card::Reconnecting:
		l.heading = text("[conn card lost]");
		line = text("[conn card reconnecting %0]", std::to_string(std::max(1, snapshot.attempt)));
		body = snapshot.graceSeconds >= 0
				   ? text("[conn card grace %0]", clock(snapshot.graceSeconds))
				   : text("[conn card colony works]");
		l.showLeave = bool(leave);
		break;
	case ConnectionSnapshot::Card::CatchingUp:
		l.showLeave = bool(leave);
		if (snapshot.cannotKeepUp)
		{
			l.heading = text("[conn card cannot keep up]");
			line = text("[conn card replaying %0]", clock(snapshot.missedSeconds));
			body = text("[conn card cannot keep up body]");
		}
		else
		{
			l.heading = text("[conn card catching up]");
			line = text("[conn card replaying %0]", clock(snapshot.missedSeconds));
			body = text("[conn card input paused]");
		}
		l.showProgress = true;
		break;
	case ConnectionSnapshot::Card::Desync:
		l.heading = text("[conn card desync]");
		line = text("[conn card desync detail]");
		l.showProgress = snapshot.catchupTotal > 0;
		break;
	default:
		return l;
	}
	l.visible = true;
	l.width = std::min(area.w - 24 * l.u, touch ? int(340 * unit) : 460);
	l.pad = 16 * l.u;
	l.bodyLines = wrap(small, body, l.width - 2 * l.pad);
	l.lineLines = wrap(font, line, l.width - 2 * l.pad);
	l.textH = th(small, "Ag") + 2;
	l.lineH = th(font, "Ag") + 2;
	l.buttonH = touch ? int(44 * unit) : 28;
	l.barH = 10 * l.u;
	l.height = l.pad + th(title, "Ag") + 6 + int(l.lineLines.size()) * l.lineH + 6 +
			   int(l.bodyLines.size()) * l.textH + l.pad;
	if (l.showProgress)
		l.height += l.barH + l.textH + 12;
	if (l.showLeave)
		l.height += l.buttonH + 10;
	l.x = area.x + (area.w - l.width) / 2;
	l.y = area.y + (area.h - l.height) / 2;
	if (l.showLeave)
	{
		const int buttonW = std::max(120, tw(font, text("[conn leave match]")) + 32);
		l.leave = {l.x + l.width - l.pad - buttonW, l.y + l.height - l.pad - l.buttonH, buttonW,
				   l.buttonH};
	}
	return l;
}

void ConnectionOverlay::refreshSnapshot()
{
	if (source && !*inputSnapshotFrozen)
	{
		snapshot = source();
		if (snapshot.card != ConnectionSnapshot::Card::None)
			details = false;
	}
}
ConnectionOverlay::InputSnapshot::InputSnapshot(ConnectionOverlay *owner)
{
	if (!owner)
		return;
	frozen = owner->inputSnapshotFrozen;
	previous = *frozen;
	owner->refreshSnapshot();
	*frozen = true;
}
ConnectionOverlay::InputSnapshot::~InputSnapshot()
{
	if (frozen)
		*frozen = previous;
}

void ConnectionOverlay::draw(bool touch, SDL_Rect area, double unit)
{
	lastTouch = touch;
	lastArea = area;
	lastUnit = unit;
	if (!source)
		return;
	refreshSnapshot();
	observe(snapshot);
	textScale = touch ? std::max(1.0, unit) : 1.0;
	globalContainer->gfx->setClipRect();
	drawPanel(touch, area, unit);
	if (snapshot.card != ConnectionSnapshot::Card::None)
	{
		details = false;
		drawCard(touch, area, unit);
	}
	else if (details)
		drawDetails(touch, area, unit);
	else
		closeRect = leaveRect = {};
	textScale = 1;
}

bool ConnectionOverlay::compactGrid(bool touch) const
{
	int humans = 0;
	for (const auto &row : snapshot.rows)
		humans += row.state != ConnectionRow::State::AI;
	return touch && humans > 4;
}

void ConnectionOverlay::drawPanel(bool touch, SDL_Rect area, double unit)
{
	auto *gfx = globalContainer->gfx;
	const Look colors = look(touch);
	Font *font = globalContainer->standardFont;
	Font *small = globalContainer->littleFont;
	const auto layout = panelLayout(touch, area, unit);
	const int u = layout.u, pad = layout.pad, lineH = layout.lineH, x = layout.x, y = layout.y,
			  footerH = layout.footerH;
	const auto &footer = layout.footer;
	panelRect = layout.bounds;
	if (layout.grid)
	{
		const int cellW = layout.cellW, cellH = layout.cellH, columns = 2, rows = layout.rows;
		gfx->drawFilledRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, colors.paper);
		gfx->drawRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, colors.edge);
		for (std::size_t i = 0; i < snapshot.rows.size(); ++i)
		{
			const auto &row = snapshot.rows[i];
			const int cx = x + pad + int(i % columns) * cellW,
					  cy = y + pad + int(i / columns) * cellH;
			gfx->drawFilledRect(cx, cy + cellH / 2 - 5 * u, 10 * u, 10 * u, row.color);
			marker(cx + 18 * u, cy + cellH / 2, 3 * u, mark(row));
			textAt(cx + 26 * u, cy + (cellH - th(small, "0")) / 2, small, gridValue(row),
				   row.local ? colors.ink : colors.muted);
		}
		if (!footer.empty())
			textAt(x + pad, y + pad + rows * cellH + 2 * u, small, footer, colors.muted);
		return;
	}
	const int header = layout.header, swatch = touch ? 10 * u : 10, width = layout.width;
	gfx->drawFilledRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, colors.paper);
	gfx->drawRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, colors.edge);
	int cy = y + pad;
	textAt(x + pad, cy, small, text("[conn players]"), colors.muted);
	// The column shows each player's worst signal (ping, or how far behind), so it
	// is headed "Connection", not "Ping" above "Behind 1.2 s".
	textRight(x + width - pad, cy, small, text("[conn connection]"), colors.muted);
	cy += header;
	for (const auto &row : snapshot.rows)
	{
		if (row.local)
			gfx->drawFilledRect(x + 1, cy, width - 2, lineH, colors.own);
		const int textY = cy + (lineH - th(font, "Ag")) / 2;
		gfx->drawFilledRect(x + pad, cy + (lineH - swatch) / 2, swatch, swatch, row.color);
		const std::string value = panelValue(row);
		const int valueW = tw(font, value);
		const int nameX = x + pad + swatch + 6;
		const int nameMax = width - (nameX - x) - pad - valueW - 14 * u - 8;
		const std::string name =
			ellipsize(font, row.local ? text("[conn you]") : row.name, nameMax);
		textAt(nameX, textY, font, name,
			   row.state == ConnectionRow::State::Left ? colors.muted : colors.ink);
		const Mark m = mark(row);
		textRight(x + width - pad, textY, font, value,
				  row.state == ConnectionRow::State::Left || row.state == ConnectionRow::State::AI
					  ? colors.muted
					  : colors.ink);
		if (m.shape >= 0)
			marker(x + width - pad - valueW - 8 * u, cy + lineH / 2, 3 * u, m);
		cy += lineH;
	}
	if (!footer.empty())
	{
		const Rating r = ConnectionQuality::rate(Metric::Delay, snapshot.inputDelayMs);
		marker(x + pad + 3 * u, cy + footerH / 2, 3 * u, {ratingColor(r), int(r)});
		textAt(x + pad + 10 * u, cy + 2 * u, small, footer, colors.muted);
	}
}

void ConnectionOverlay::drawDetails(bool touch, SDL_Rect area, double unit)
{
	auto *gfx = globalContainer->gfx;
	const Look colors = look(touch);
	Font *font = globalContainer->standardFont;
	Font *small = globalContainer->littleFont;
	const auto layout = detailsLayout(touch, area, unit);
	const int u = layout.u, width = layout.width, pad = layout.pad, lineH = layout.lineH,
			  textH = layout.textH, legendH = layout.legendH, buttonH = layout.buttonH,
			  headerH = layout.headerH, height = layout.height, x = layout.x, y = layout.y;
	const auto &lines = layout.lines;
	gfx->drawFilledRect(x, y, width, height, colors.paper.applyAlpha(245));
	gfx->drawRect(x, y, width, height, touch ? colors.edge : Color(200, 170, 80));
	int cy = y + pad;
	textAt(x + pad, cy, font, text("[conn connections]"), colors.ink);
	cy += th(font, "Ag") + 8;
	// Columns: player | Ping | Behind | state. Values right-aligned in their column.
	const int statusX =
		x + width - pad -
		std::max(int(width * 0.24), tw(small, text("[conn reconnecting]") + " 0:00"));
	const int behindRight = statusX - 10 * u;
	const int pingRight = behindRight - std::max(int(width * 0.16), tw(font, "0000 ms"));
	const int nameX = x + pad + 16 * u;
	textAt(nameX, cy, small, text("[conn players]"), colors.muted);
	textRight(pingRight, cy, small, text("[conn ping]"), colors.muted);
	textRight(behindRight, cy, small, text("[conn behind]"), colors.muted);
	cy += headerH;
	for (const auto &row : snapshot.rows)
	{
		gfx->drawFilledRect(x + pad, cy + lineH / 2 - 5 * u, 10 * u, 10 * u, row.color);
		const int nameMax =
			pingRight - std::max(int(width * 0.16), tw(font, "0000 ms")) - nameX - 6 * u;
		textAt(nameX, cy + 3, font,
			   ellipsize(font, row.local ? text("[conn you %0]", row.name) : row.name, nameMax),
			   colors.ink);
		if (row.state != ConnectionRow::State::AI)
		{
			textRight(pingRight, cy + 3, font, valueOrDash(Metric::Ping, row.pingMs), colors.ink);
			textRight(behindRight, cy + 3, font, valueOrDash(Metric::Behind, row.behindMs),
					  colors.ink);
		}
		const Mark m = mark(row);
		std::string status = stateText(row);
		if (status.empty())
		{
			const Reading r = reading(row);
			status = r.known ? ratingWord(r.rating) : "–";
			if (row.state == ConnectionRow::State::Slow)
				status = text("[conn slow]");
		}
		if (m.shape >= 0)
			marker(statusX + 3 * u, cy + lineH / 2, 3 * u, m);
		textAt(statusX + 10 * u, cy + 5, small, status,
			   row.state == ConnectionRow::State::AI ? colors.muted : colors.ink);
		cy += lineH;
	}
	cy += 8;
	// Legend: shape, colour and word together.
	int lx = x + pad;
	for (Rating r : {Rating::Good, Rating::Fair, Rating::Poor})
	{
		marker(lx + 3 * u, cy + textH / 2, 3 * u, {ratingColor(r), int(r)});
		const std::string word = ratingWord(r);
		textAt(lx + 10 * u, cy, small, word, colors.ink);
		lx += 10 * u + tw(small, word) + 16 * u;
	}
	cy += legendH;
	for (const auto &line : lines)
	{
		textAt(x + pad, cy, small, line, colors.muted);
		cy += textH;
	}
	cy += 10;
	const std::string close = text("[Close]");
	const int buttonW = layout.close.w;
	closeRect = layout.close;
	gfx->drawFilledRect(closeRect.x, closeRect.y, closeRect.w, closeRect.h,
						touch ? InGameTouchTheme::field() : Color(60, 50, 20, 230));
	gfx->drawRect(closeRect.x, closeRect.y, closeRect.w, closeRect.h,
				  touch ? colors.edge : Color(220, 190, 90));
	textAt(closeRect.x + (buttonW - tw(font, close)) / 2,
		   closeRect.y + (buttonH - th(font, close)) / 2, font, close, colors.ink);
}

void ConnectionOverlay::drawCard(bool touch, SDL_Rect area, double unit)
{
	auto *gfx = globalContainer->gfx;
	const Look colors = look(touch);
	Font *title = globalContainer->menuFont;
	Font *font = globalContainer->standardFont;
	Font *small = globalContainer->littleFont;
	const auto layout = cardLayout(touch, area, unit);
	if (!layout.visible)
		return;
	const int width = layout.width, pad = layout.pad, textH = layout.textH, lineH = layout.lineH,
			  buttonH = layout.buttonH, barH = layout.barH, height = layout.height, x = layout.x,
			  y = layout.y;
	const auto &heading = layout.heading;
	const auto &bodyLines = layout.bodyLines, &lineLines = layout.lineLines;
	const bool showProgress = layout.showProgress, showLeave = layout.showLeave;
	gfx->drawFilledRect(area.x, area.y, area.w, area.h, Color(0, 0, 0, 90));
	gfx->drawFilledRect(x, y, width, height, colors.paper.applyAlpha(245));
	gfx->drawRect(x, y, width, height, touch ? colors.edge : Color(200, 170, 80));
	int cy = y + pad;
	textAt(x + pad, cy, title, heading, touch ? colors.ink : Color(240, 210, 120));
	cy += th(title, "Ag") + 6;
	for (const auto &l : lineLines)
	{
		textAt(x + pad, cy, font, l, colors.ink);
		cy += lineH;
	}
	cy += 6;
	if (showProgress)
	{
		const double fraction =
			snapshot.catchupTotal
				? std::clamp(double(snapshot.catchupDone) / snapshot.catchupTotal, 0.0, 1.0)
				: 0.0;
		gfx->drawFilledRect(x + pad, cy, width - 2 * pad, barH, Color(255, 255, 255, 40));
		gfx->drawFilledRect(x + pad, cy, int((width - 2 * pad) * fraction), barH,
							Color(225, 190, 110));
		cy += barH + 4;
		std::string progress = GAGCore::FormattableString(text("[conn card progress %0 %1 %2]"))
								   .arg(int(fraction * 100))
								   .arg(snapshot.catchupDone)
								   .arg(snapshot.catchupTotal);
		textAt(x + pad, cy, small, progress, colors.muted);
		if (snapshot.secondsLeft >= 0)
			textRight(x + width - pad, cy, small,
					  text("[conn card seconds left %0]", std::to_string(snapshot.secondsLeft)),
					  colors.muted);
		else if (snapshot.card == ConnectionSnapshot::Card::CatchingUp && snapshot.cannotKeepUp)
			textRight(x + width - pad, cy, small, text("[conn card not gaining]"), colors.muted);
		cy += textH + 8;
	}
	for (const auto &l : bodyLines)
	{
		textAt(x + pad, cy, small, l, colors.muted);
		cy += textH;
	}
	leaveRect = {};
	if (showLeave)
	{
		cy += 10;
		const std::string label = text("[conn leave match]");
		const int buttonW = layout.leave.w;
		leaveRect = layout.leave;
		gfx->drawFilledRect(leaveRect.x, leaveRect.y, leaveRect.w, leaveRect.h,
							touch ? InGameTouchTheme::field() : Color(60, 50, 20, 230));
		gfx->drawRect(leaveRect.x, leaveRect.y, leaveRect.w, leaveRect.h,
					  touch ? colors.edge : Color(220, 190, 90));
		textAt(leaveRect.x + (buttonW - tw(font, label)) / 2,
			   leaveRect.y + (buttonH - th(font, label)) / 2, font, label, colors.ink);
	}
}

void CatchUpPace::sample(std::uint64_t nowMicros, std::uint32_t executed, std::uint32_t horizon)
{
	const std::uint32_t gap = horizon > executed ? horizon - executed : 0;
	if (samples == 0)
	{
		lastAt = nowMicros;
		lastGap = gap;
		samples = 1;
		return;
	}
	if (nowMicros - lastAt < SAMPLE_MICROS)
		return;
	const double seconds = double(nowMicros - lastAt) / 1e6;
	const double closed = (double(lastGap) - double(gap)) / seconds;
	// Smooth over a few samples; the first one sets the pace.
	rate = samples < 2 ? closed : rate * 0.5 + closed * 0.5;
	if (rate > 0.5)
		notClosingSince = 0;
	else if (!notClosingSince)
		notClosingSince = lastAt;
	lastAt = nowMicros;
	lastGap = gap;
	++samples;
}

int CatchUpPace::secondsLeft(std::uint32_t gapTicks, double) const
{
	if (!known() || rate <= 0.5)
		return -1;
	return int(double(gapTicks) / rate + 0.5);
}

bool ConnectionOverlay::handle(const SDL_Event &event)
{
	int x = 0, y = 0;
	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
		event.button.which != SDL_TOUCH_MOUSEID)
	{
		x = int(event.button.x);
		y = int(event.button.y);
	}
	else if (event.type == SDL_EVENT_FINGER_DOWN && globalContainer && globalContainer->gfx)
	{
		x = int(event.tfinger.x * globalContainer->gfx->getW());
		y = int(event.tfinger.y * globalContainer->gfx->getH());
	}
	else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && details)
	{
		details = false;
		return true;
	}
	else
	{
		const bool input =
			event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
			event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING ||
			event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_MOTION ||
			event.type == SDL_EVENT_MOUSE_WHEEL || event.type == SDL_EVENT_FINGER_UP ||
			event.type == SDL_EVENT_FINGER_MOTION;
		return input && blocksGameplay();
	}
	const auto controls = describeControls(lastTouch, lastArea, lastUnit);
	for (const auto &control : controls)
		if (control.tap && control.contains({x, y}))
		{
			control.tap({x, y});
			return true;
		}
	return blocksGameplay();
}

std::vector<GAGGUI::ui::PositionedControl>
ConnectionOverlay::describeControls(bool touch, SDL_Rect area, double unit)
{
	using namespace GAGGUI::ui;
	std::vector<PositionedControl> controls;
	lastTouch = touch;
	lastArea = area;
	lastUnit = unit;
	if (!source)
		return controls;
	refreshSnapshot();
	struct Scale
	{
		double old;
		~Scale() { textScale = old; }
	} scale{textScale};
	textScale = touch ? std::max(1., unit) : 1.;
	const auto panel = panelLayout(touch, area, unit);
	panelRect = panel.bounds;
	auto button =
		[&](const char *key, const std::string &label, SDL_Rect rect, std::function<void()> action)
	{
		PositionedControl c;
		c.key = key;
		c.label = label;
		c.bounds = {rect.x, rect.y, rect.w, rect.h};
		c.tap = [action](Point) { action(); };
		controls.push_back(std::move(c));
	};
	if (snapshot.card != ConnectionSnapshot::Card::None)
	{
		const auto card = cardLayout(touch, area, unit);
		PositionedControl body;
		body.key = "connection/card";
		body.role = "dialog";
		body.label = card.heading;
		body.bounds = {card.x, card.y, card.width, card.height};
		body.actions.clear();
		controls.push_back(std::move(body));
		if (card.showLeave)
			button("connection/leave", text("[conn leave match]"), card.leave,
				   [this]
				   {
					   if (leave)
						   leave();
				   });
		return controls;
	}
	if (details)
	{
		const auto layout = detailsLayout(touch, area, unit);
		button("connection/details", text("[conn connections]"), panel.bounds,
			   [this] { details = false; });
		controls.back().selected = true;
		controls.back().value = true;
		button("connection/close", text("[Close]"), layout.close, [this] { details = false; });
		return controls;
	}
	button("connection/details", text("[conn connections]"), panel.bounds,
		   [this] { details = true; });
	return controls;
}
