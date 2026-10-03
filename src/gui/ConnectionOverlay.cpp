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

using GAGCore::Color;
using GAGCore::Font;
using ConnectionQuality::Metric;
using ConnectionQuality::Rating;

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
		return {InGameTouchTheme::paper, InGameTouchTheme::border, InGameTouchTheme::ink, Color(190, 170, 140), InGameTouchTheme::selected};
	return {Color(8, 10, 40, 215), Color(200, 200, 230, 120), Color(240, 240, 240), Color(160, 165, 200), Color(52, 56, 112, 230)};
}

// Quality colours. Never the only signal: every rating also has a word and a marker
// shape (good: disc, fair: ring, poor: triangle).
const Color goodInk(110, 205, 110), fairInk(235, 185, 60), poorInk(235, 95, 80), gone(140, 140, 150);

Color ratingColor(Rating r)
{
	return r == Rating::Good ? goodInk : r == Rating::Fair ? fairInk : poorInk;
}

std::string ratingWord(Rating r)
{
	return text(r == Rating::Good ? "[conn good]" : r == Rating::Fair ? "[conn fair]" : "[conn poor]");
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
	const Rating behind = behindKnown ? ConnectionQuality::rate(Metric::Behind, row.behindMs) : Rating::Good;
	if (behindKnown && (behind != Rating::Good || row.pingMs < 0))
	{
		r = {true, Metric::Behind, row.behindMs, behind};
	}
	else if (row.pingMs >= 0)
	{
		r = {true, Metric::Ping, row.pingMs, ConnectionQuality::rate(Metric::Ping, row.pingMs)};
	}
	// The relay marks a player slow from 2 s behind: poor on the shared scale too.
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
	auto flush = [&] {
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
		return row.graceSeconds >= 0 ? text("[conn reconnecting]") + " " + clock(row.graceSeconds) : text("[conn reconnecting]");
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
	return (r.metric == Metric::Behind ? text("[conn behind %0]", value) : value) + DOT + ratingWord(r.rating);
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
	return r.metric == Metric::Behind ? ConnectionQuality::format(Metric::Behind, r.ms) : std::to_string(r.ms);
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
	if (value.size() > tenth.size() && value.compare(value.size() - tenth.size(), tenth.size(), tenth) == 0)
		value = value.substr(0, value.size() - tenth.size()) + " s";
	return value;
}
} // namespace

std::string ConnectionOverlay::delayLine(const ConnectionSnapshot &snapshot)
{
	if (snapshot.inputDelayMs < 0)
		return {};
	const Rating r = ConnectionQuality::rate(Metric::Delay, snapshot.inputDelayMs);
	std::string line = text("[conn your delay %0]", ConnectionQuality::format(Metric::Delay, snapshot.inputDelayMs)) + DOT + ratingWord(r);
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
				notice(GAGCore::FormattableString(text("[conn notice lost %0 %1]")).arg(row.name).arg(clock(row.graceSeconds)));
			else if (row.state == ConnectionRow::State::Connected && previous->second == ConnectionRow::State::Reconnecting)
				notice(text("[conn notice back %0]", row.name));
			else if (row.state == ConnectionRow::State::Left)
				notice(text("[conn notice left %0]", row.name));
		}
		seen[row.seat] = row.state;
	}
	primed = true;
}

void ConnectionOverlay::draw(bool touch, SDL_Rect area, double unit)
{
	if (!source)
		return;
	snapshot = source();
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
	const int u = std::max(1, int(std::lround(unit)));
	const int pad = 6 * u;
	const int lineH = std::max(th(font, "Ag"), int(18 * unit));
	const int x = area.x + (touch ? 8 * u : 12);
	const int y = area.y + (touch ? 6 * u : 8);
	const std::string footer = delayLine(snapshot);
	const int footerH = footer.empty() ? 0 : th(small, "Ag") + 4 * u;
	// Phones beyond four people: a grid of markers and numbers that never grows into
	// the map. The details sheet explains the numbers.
	if (compactGrid(touch))
	{
		const int cellW = int(64 * unit), cellH = int(20 * unit);
		const int columns = 2;
		const int rows = int((snapshot.rows.size() + columns - 1) / columns);
		const int width = std::max(columns * cellW, footer.empty() ? 0 : tw(small, footer)) + 2 * pad;
		panelRect = {x, y, width, rows * cellH + footerH + 2 * pad};
		gfx->drawFilledRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, colors.paper);
		gfx->drawRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, colors.edge);
		for (std::size_t i = 0; i < snapshot.rows.size(); ++i)
		{
			const auto &row = snapshot.rows[i];
			const int cx = x + pad + int(i % columns) * cellW, cy = y + pad + int(i / columns) * cellH;
			gfx->drawFilledRect(cx, cy + cellH / 2 - 5 * u, 10 * u, 10 * u, row.color);
			marker(cx + 18 * u, cy + cellH / 2, 3 * u, mark(row));
			textAt(cx + 26 * u, cy + (cellH - th(small, "0")) / 2, small, gridValue(row), row.local ? colors.ink : colors.muted);
		}
		if (!footer.empty())
			textAt(x + pad, y + pad + rows * cellH + 2 * u, small, footer, colors.muted);
		return;
	}
	const int header = th(small, "Ag") + 4;
	// Wide enough for the longest value, so names give way first.
	int valueMax = 0;
	for (const auto &row : snapshot.rows)
		valueMax = std::max(valueMax, tw(font, panelValue(row)));
	const int swatch = touch ? 10 * u : 10;
	const int minimum = touch ? int(200 * unit) : 280;
	const int width = std::max({minimum, footer.empty() ? 0 : tw(small, footer) + 2 * pad,
	                            pad + swatch + 6 + int(70 * unit) + 14 * u + valueMax + pad});
	panelRect = {x, y, width, header + int(snapshot.rows.size()) * lineH + footerH + 2 * pad};
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
		const std::string name = ellipsize(font, row.local ? text("[conn you]") : row.name, nameMax);
		textAt(nameX, textY, font, name, row.state == ConnectionRow::State::Left ? colors.muted : colors.ink);
		const Mark m = mark(row);
		textRight(x + width - pad, textY, font, value, row.state == ConnectionRow::State::Left || row.state == ConnectionRow::State::AI ? colors.muted : colors.ink);
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
	const int u = std::max(1, int(std::lround(unit)));
	const int width = std::min(area.w - 24 * u, touch ? int(380 * unit) : 520);
	const int pad = 12 * u;
	const int lineH = std::max(th(font, "Ag") + 6, int(22 * unit));
	// Explanations, one paragraph each, from the shared table's limits.
	std::vector<std::string> paragraphs;
	const std::string delay = delayLine(snapshot);
	if (!delay.empty())
		paragraphs.push_back(delay + (snapshot.jitterMs > 0 ? DOT + text("[conn jitter %0]", std::to_string(snapshot.jitterMs) + " ms") : ""));
	paragraphs.push_back(text("[conn delay explanation %0 %1]", limitText(Metric::Delay, false), limitText(Metric::Delay, true)));
	paragraphs.push_back(text("[conn ping explanation %0 %1]", limitText(Metric::Ping, false), limitText(Metric::Ping, true)));
	paragraphs.push_back(text("[conn behind explanation %0 %1]", limitText(Metric::Behind, false), limitText(Metric::Behind, true)));
	if (compactGrid(touch))
		paragraphs.push_back(text("[conn grid legend]"));
	if (!snapshot.relay.empty())
		paragraphs.push_back(text("[conn relay %0]", snapshot.relay));
	std::vector<std::string> lines;
	for (const auto &p : paragraphs)
		for (auto &l : wrap(small, p, width - 2 * pad))
			lines.push_back(std::move(l));
	const int textH = th(small, "Ag") + 2;
	const int legendH = textH + 6;
	const int buttonH = int((touch ? 44 : 28) * (touch ? unit : 1));
	const int headerH = textH + 4;
	const int height = pad + th(font, "Ag") + 8 + headerH + int(snapshot.rows.size()) * lineH + 8 + legendH + int(lines.size()) * textH + 10 + buttonH + pad;
	const int x = area.x + (area.w - width) / 2;
	const int y = touch ? area.y + area.h - height - 8 * u : area.y + (area.h - height) / 2;
	gfx->drawFilledRect(x, y, width, height, colors.paper.applyAlpha(245));
	gfx->drawRect(x, y, width, height, touch ? colors.edge : Color(200, 170, 80));
	int cy = y + pad;
	textAt(x + pad, cy, font, text("[conn connections]"), colors.ink);
	cy += th(font, "Ag") + 8;
	// Columns: player | Ping | Behind | state. Values right-aligned in their column.
	const int statusX = x + width - pad - std::max(int(width * 0.24), tw(small, text("[conn reconnecting]") + " 0:00"));
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
		const int nameMax = pingRight - std::max(int(width * 0.16), tw(font, "0000 ms")) - nameX - 6 * u;
		textAt(nameX, cy + 3, font, ellipsize(font, row.local ? text("[conn you %0]", row.name) : row.name, nameMax), colors.ink);
		if (row.state != ConnectionRow::State::AI)
		{
			textRight(pingRight, cy + 3, font, valueOrDash(Metric::Ping, row.pingMs), colors.ink);
			textRight(behindRight, cy + 3, font, valueOrDash(Metric::Behind, row.behindMs), colors.ink);
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
		textAt(statusX + 10 * u, cy + 5, small, status, row.state == ConnectionRow::State::AI ? colors.muted : colors.ink);
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
	const int buttonW = std::max(96, tw(font, close) + 32);
	closeRect = {x + width - pad - buttonW, cy, buttonW, buttonH};
	gfx->drawFilledRect(closeRect.x, closeRect.y, closeRect.w, closeRect.h, touch ? InGameTouchTheme::field : Color(60, 50, 20, 230));
	gfx->drawRect(closeRect.x, closeRect.y, closeRect.w, closeRect.h, touch ? colors.edge : Color(220, 190, 90));
	textAt(closeRect.x + (buttonW - tw(font, close)) / 2, closeRect.y + (buttonH - th(font, close)) / 2, font, close, colors.ink);
}

void ConnectionOverlay::drawCard(bool touch, SDL_Rect area, double unit)
{
	auto *gfx = globalContainer->gfx;
	const Look colors = look(touch);
	Font *title = globalContainer->menuFont;
	Font *font = globalContainer->standardFont;
	Font *small = globalContainer->littleFont;
	const int u = std::max(1, int(std::lround(unit)));
	// The map dims behind the card: this client is the one waiting.
	gfx->drawFilledRect(area.x, area.y, area.w, area.h, Color(0, 0, 0, 90));
	std::string heading, line, body;
	bool showProgress = false, showLeave = false;
	switch (snapshot.card)
	{
	case ConnectionSnapshot::Card::Reconnecting:
		heading = text("[conn card lost]");
		line = text("[conn card reconnecting %0]", std::to_string(std::max(1, snapshot.attempt)));
		body = snapshot.graceSeconds >= 0 ? text("[conn card grace %0]", clock(snapshot.graceSeconds)) : text("[conn card colony works]");
		showLeave = static_cast<bool>(leave);
		break;
	case ConnectionSnapshot::Card::CatchingUp:
		// Always a way out; a device that replays slower than the match runs is told
		// so instead of watching an estimate grow.
		showLeave = static_cast<bool>(leave);
		if (snapshot.cannotKeepUp)
		{
			heading = text("[conn card cannot keep up]");
			line = text("[conn card replaying %0]", clock(snapshot.missedSeconds));
			body = text("[conn card cannot keep up body]");
		}
		else
		{
			heading = text("[conn card catching up]");
			line = text("[conn card replaying %0]", clock(snapshot.missedSeconds));
			body = text("[conn card input paused]");
		}
		showProgress = true;
		break;
	case ConnectionSnapshot::Card::Desync:
		heading = text("[conn card desync]");
		line = text("[conn card desync detail]");
		showProgress = snapshot.catchupTotal > 0;
		break;
	default:
		return;
	}
	const int width = std::min(area.w - 24 * u, touch ? int(340 * unit) : 460);
	const int pad = 16 * u;
	const auto bodyLines = wrap(small, body, width - 2 * pad);
	const auto lineLines = wrap(font, line, width - 2 * pad);
	const int textH = th(small, "Ag") + 2, lineH = th(font, "Ag") + 2;
	const int buttonH = touch ? int(44 * unit) : 28;
	const int barH = 10 * u;
	int height = pad + th(title, "Ag") + 6 + int(lineLines.size()) * lineH + 6 + int(bodyLines.size()) * textH + pad;
	if (showProgress)
		height += barH + textH + 12;
	if (showLeave)
		height += buttonH + 10;
	const int x = area.x + (area.w - width) / 2, y = area.y + (area.h - height) / 2;
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
		const double fraction = snapshot.catchupTotal ? std::clamp(double(snapshot.catchupDone) / snapshot.catchupTotal, 0.0, 1.0) : 0.0;
		gfx->drawFilledRect(x + pad, cy, width - 2 * pad, barH, Color(255, 255, 255, 40));
		gfx->drawFilledRect(x + pad, cy, int((width - 2 * pad) * fraction), barH, Color(225, 190, 110));
		cy += barH + 4;
		std::string progress = GAGCore::FormattableString(text("[conn card progress %0 %1 %2]")).arg(int(fraction * 100)).arg(snapshot.catchupDone).arg(snapshot.catchupTotal);
		textAt(x + pad, cy, small, progress, colors.muted);
		if (snapshot.secondsLeft >= 0)
			textRight(x + width - pad, cy, small, text("[conn card seconds left %0]", std::to_string(snapshot.secondsLeft)), colors.muted);
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
		const int buttonW = std::max(120, tw(font, label) + 32);
		leaveRect = {x + width - pad - buttonW, cy, buttonW, buttonH};
		gfx->drawFilledRect(leaveRect.x, leaveRect.y, leaveRect.w, leaveRect.h, touch ? InGameTouchTheme::field : Color(60, 50, 20, 230));
		gfx->drawRect(leaveRect.x, leaveRect.y, leaveRect.w, leaveRect.h, touch ? colors.edge : Color(220, 190, 90));
		textAt(leaveRect.x + (buttonW - tw(font, label)) / 2, leaveRect.y + (buttonH - th(font, label)) / 2, font, label, colors.ink);
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
	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && event.button.which != SDL_TOUCH_MOUSEID)
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
		return false;
	if (inside(leaveRect, x, y))
	{
		leaveRect = {};
		if (leave)
			leave();
		return true;
	}
	if (details && inside(closeRect, x, y))
	{
		details = false;
		return true;
	}
	if (inside(panelRect, x, y))
	{
		details = !details;
		return true;
	}
	return false;
}
