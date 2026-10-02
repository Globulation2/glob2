// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#include "ConnectionOverlay.h"
#include "GlobalContainer.h"
#include "InGameTouchTheme.h"
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <cmath>

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

std::string clock(int seconds)
{
	seconds = std::max(0, seconds);
	char buffer[16];
	std::snprintf(buffer, sizeof buffer, "%d:%02d", seconds / 60, seconds % 60);
	return buffer;
}

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

const Color good(110, 205, 110), slow(235, 185, 60), lost(225, 85, 70), gone(140, 140, 150);

Color stateColor(const ConnectionRow &row)
{
	switch (row.state)
	{
	case ConnectionRow::State::Connected:
		return row.unstable ? slow : good;
	case ConnectionRow::State::Slow:
	case ConnectionRow::State::Resyncing:
		return slow;
	case ConnectionRow::State::Reconnecting:
		return lost;
	default:
		return gone;
	}
}

void dot(int x, int y, int radius, const Color &color)
{
	for (int r = 1; r <= radius; ++r)
		globalContainer->gfx->drawCircle(x, y, r, color);
	globalContainer->gfx->drawFilledRect(x - radius / 2, y - radius / 2, radius, radius, color);
}

void textAt(int x, int y, Font *font, const std::string &value, const Color &color)
{
	font->pushStyle(Font::Style(Font::STYLE_NORMAL, color));
	globalContainer->gfx->drawString(x, y, font, value);
	font->popStyle();
}

void textRight(int right, int y, Font *font, const std::string &value, const Color &color)
{
	textAt(right - font->getStringWidth(value), y, font, value, color);
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
			if (!line.empty() && font->getStringWidth(candidate) > width)
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

std::string latencyText(const ConnectionRow &row, bool compact)
{
	switch (row.state)
	{
	case ConnectionRow::State::AI:
		return "–";
	case ConnectionRow::State::Reconnecting:
		return row.graceSeconds >= 0 ? clock(row.graceSeconds) : text("[conn lost]");
	case ConnectionRow::State::Left:
		return text("[conn left]");
	case ConnectionRow::State::Waiting:
		return "…";
	default:
		if (row.latencyMs < 0)
			return "–";
		return compact ? std::to_string(row.latencyMs) : std::to_string(row.latencyMs) + " ms";
	}
}

std::string stateWord(const ConnectionRow &row)
{
	switch (row.state)
	{
	case ConnectionRow::State::Slow:
		return text("[conn slow]");
	case ConnectionRow::State::Reconnecting:
		return text("[conn reconnecting]");
	case ConnectionRow::State::Resyncing:
		return text("[conn resyncing]");
	case ConnectionRow::State::Waiting:
		return text("[conn waiting]");
	case ConnectionRow::State::Left:
		return text("[conn left]");
	case ConnectionRow::State::AI:
		return text("[conn ai everywhere]");
	default:
		return row.unstable ? text("[conn unstable]") : text("[conn connected]");
	}
}
} // namespace

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
}

void ConnectionOverlay::drawPanel(bool touch, SDL_Rect area, double unit)
{
	auto *gfx = globalContainer->gfx;
	const Look colors = look(touch);
	Font *font = globalContainer->standardFont;
	Font *small = globalContainer->littleFont;
	const int u = std::max(1, int(std::lround(unit)));
	int humans = 0;
	for (const auto &row : snapshot.rows)
		humans += row.state != ConnectionRow::State::AI;
	const int pad = 6 * u;
	const int lineH = std::max(font->getStringHeight("Ag"), int(18 * unit));
	const int x = area.x + (touch ? 8 * u : 12);
	const int y = area.y + (touch ? 6 * u : 8);
	// Phones beyond four people: a grid of dots and numbers that never grows into the map.
	if (touch && humans > 4)
	{
		const int cellW = int(64 * unit), cellH = int(20 * unit);
		const int columns = 2;
		const int rows = int((snapshot.rows.size() + columns - 1) / columns);
		panelRect = {x, y, columns * cellW + 2 * pad, rows * cellH + 2 * pad};
		gfx->drawFilledRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, colors.paper);
		gfx->drawRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, colors.edge);
		for (std::size_t i = 0; i < snapshot.rows.size(); ++i)
		{
			const auto &row = snapshot.rows[i];
			const int cx = x + pad + int(i % columns) * cellW, cy = y + pad + int(i / columns) * cellH;
			gfx->drawFilledRect(cx, cy + cellH / 2 - 5 * u, 10 * u, 10 * u, row.color);
			dot(cx + 18 * u, cy + cellH / 2, 3 * u, stateColor(row));
			textAt(cx + 26 * u, cy + (cellH - small->getStringHeight("0")) / 2, small, latencyText(row, true), row.local ? colors.ink : colors.muted);
		}
		return;
	}
	const int width = touch ? int(176 * unit) : 262;
	const int header = touch ? 0 : small->getStringHeight("Ag") + 4;
	panelRect = {x, y, width, header + int(snapshot.rows.size()) * lineH + 2 * pad};
	gfx->drawFilledRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, colors.paper);
	gfx->drawRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, colors.edge);
	int cy = y + pad;
	if (header)
	{
		textAt(x + pad, cy, small, text("[conn players]"), colors.muted);
		textRight(x + width - pad, cy, small, text("[conn latency]"), colors.muted);
		cy += header;
	}
	for (const auto &row : snapshot.rows)
	{
		if (row.local)
			gfx->drawFilledRect(x + 1, cy, width - 2, lineH, colors.own);
		const int textY = cy + (lineH - font->getStringHeight("Ag")) / 2;
		const int swatch = touch ? 10 * u : 10;
		gfx->drawFilledRect(x + pad, cy + (lineH - swatch) / 2, swatch, swatch, row.color);
		std::string name = row.local && touch ? text("[conn you]") : row.local ? text("[conn you %0]", row.name) : row.name;
		if (row.state == ConnectionRow::State::AI)
			name += " · " + text("[conn ai]");
		if (row.local && row.unstable)
			name += " · " + text("[conn unstable]");
		const int nameX = x + pad + swatch + 6;
		const std::string value = latencyText(row, touch);
		const int valueW = font->getStringWidth(value);
		// Ellipsize names that would run into the state column.
		const int nameMax = width - (nameX - x) - pad - valueW - (touch ? 14 * u : 70);
		while (name.size() > 1 && font->getStringWidth(name) > nameMax)
			name = name.substr(0, name.size() - 2) + "…";
		textAt(nameX, textY, font, name, row.state == ConnectionRow::State::Left ? colors.muted : colors.ink);
		const Color state = stateColor(row);
		textRight(x + width - pad, textY, font, value, row.state == ConnectionRow::State::Reconnecting ? lost : colors.ink);
		if (row.state != ConnectionRow::State::AI)
		{
			const int dotX = x + width - pad - valueW - 10 * u;
			dot(dotX, cy + lineH / 2, 3 * u, state);
			if (!touch && (row.state == ConnectionRow::State::Slow || row.state == ConnectionRow::State::Reconnecting))
				textRight(dotX - 8, textY, small, row.state == ConnectionRow::State::Slow ? text("[conn slow]") : "↻", state);
		}
		cy += lineH;
	}
}

void ConnectionOverlay::drawDetails(bool touch, SDL_Rect area, double unit)
{
	auto *gfx = globalContainer->gfx;
	const Look colors = look(touch);
	Font *font = globalContainer->standardFont;
	Font *small = globalContainer->littleFont;
	const int u = std::max(1, int(std::lround(unit)));
	const int width = std::min(area.w - 24 * u, touch ? int(360 * unit) : 440);
	const int pad = 12 * u;
	const int lineH = std::max(font->getStringHeight("Ag") + 6, int(22 * unit));
	std::string explanation = text("[conn delay explanation]");
	if (!snapshot.relay.empty())
		explanation += " " + GAGCore::FormattableString(text("[conn relay %0 %1]")).arg(snapshot.relay).arg(snapshot.rttMs >= 0 ? std::to_string(snapshot.rttMs) : "–");
	const auto lines = wrap(small, explanation, width - 2 * pad);
	const int textH = small->getStringHeight("Ag") + 2;
	const int buttonH = int((touch ? 44 : 28) * (touch ? unit : 1));
	const int height = pad + font->getStringHeight("Ag") + 8 + int(snapshot.rows.size()) * lineH + 8 + int(lines.size()) * textH + 10 + buttonH + pad;
	const int x = area.x + (area.w - width) / 2;
	const int y = touch ? area.y + area.h - height - 8 * u : area.y + (area.h - height) / 2;
	gfx->drawFilledRect(x, y, width, height, colors.paper.applyAlpha(245));
	gfx->drawRect(x, y, width, height, touch ? colors.edge : Color(200, 170, 80));
	int cy = y + pad;
	textAt(x + pad, cy, font, text("[conn connections]"), colors.ink);
	cy += font->getStringHeight("Ag") + 8;
	for (const auto &row : snapshot.rows)
	{
		gfx->drawFilledRect(x + pad, cy + lineH / 2 - 5, 10, 10, row.color);
		std::string name = row.local ? text("[conn you %0]", row.name) : row.name;
		textAt(x + pad + 16, cy + 3, font, name, colors.ink);
		std::string state = stateWord(row);
		if (row.state == ConnectionRow::State::Reconnecting && row.graceSeconds >= 0)
			state += " · " + clock(row.graceSeconds);
		textAt(x + width / 2, cy + 5, small, state, stateColor(row));
		if (row.state != ConnectionRow::State::AI)
			textRight(x + width - pad, cy + 3, font, row.latencyMs >= 0 ? std::to_string(row.latencyMs) + " ms" : "–", colors.ink);
		cy += lineH;
	}
	cy += 8;
	for (const auto &line : lines)
	{
		textAt(x + pad, cy, small, line, colors.muted);
		cy += textH;
	}
	cy += 10;
	const std::string close = text("[Close]");
	const int buttonW = std::max(96, font->getStringWidth(close) + 32);
	closeRect = {x + width - pad - buttonW, cy, buttonW, buttonH};
	gfx->drawFilledRect(closeRect.x, closeRect.y, closeRect.w, closeRect.h, touch ? InGameTouchTheme::field : Color(60, 50, 20, 230));
	gfx->drawRect(closeRect.x, closeRect.y, closeRect.w, closeRect.h, touch ? colors.edge : Color(220, 190, 90));
	textAt(closeRect.x + (buttonW - font->getStringWidth(close)) / 2, closeRect.y + (buttonH - font->getStringHeight(close)) / 2, font, close, colors.ink);
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
		heading = text("[conn card catching up]");
		line = text("[conn card replaying %0]", clock(snapshot.missedSeconds));
		body = text("[conn card input paused]");
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
	const int textH = small->getStringHeight("Ag") + 2, lineH = font->getStringHeight("Ag") + 2;
	const int buttonH = touch ? int(44 * unit) : 28;
	const int barH = 10 * u;
	int height = pad + title->getStringHeight("Ag") + 6 + int(lineLines.size()) * lineH + 6 + int(bodyLines.size()) * textH + pad;
	if (showProgress)
		height += barH + textH + 12;
	if (showLeave)
		height += buttonH + 10;
	const int x = area.x + (area.w - width) / 2, y = area.y + (area.h - height) / 2;
	gfx->drawFilledRect(x, y, width, height, colors.paper.applyAlpha(245));
	gfx->drawRect(x, y, width, height, touch ? colors.edge : Color(200, 170, 80));
	int cy = y + pad;
	textAt(x + pad, cy, title, heading, touch ? colors.ink : Color(240, 210, 120));
	cy += title->getStringHeight("Ag") + 6;
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
		const int buttonW = std::max(120, font->getStringWidth(label) + 32);
		leaveRect = {x + width - pad - buttonW, cy, buttonW, buttonH};
		gfx->drawFilledRect(leaveRect.x, leaveRect.y, leaveRect.w, leaveRect.h, touch ? InGameTouchTheme::field : Color(60, 50, 20, 230));
		gfx->drawRect(leaveRect.x, leaveRect.y, leaveRect.w, leaveRect.h, touch ? colors.edge : Color(220, 190, 90));
		textAt(leaveRect.x + (buttonW - font->getStringWidth(label)) / 2, leaveRect.y + (buttonH - font->getStringHeight(label)) / 2, font, label, colors.ink);
	}
}

bool ConnectionOverlay::handle(const SDL_Event &event)
{
	int x = 0, y = 0;
	if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT && event.button.which != SDL_TOUCH_MOUSEID)
	{
		x = event.button.x;
		y = event.button.y;
	}
	else if (event.type == SDL_FINGERDOWN && globalContainer && globalContainer->gfx)
	{
		x = int(event.tfinger.x * globalContainer->gfx->getW());
		y = int(event.tfinger.y * globalContainer->gfx->getH());
	}
	else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && details)
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
