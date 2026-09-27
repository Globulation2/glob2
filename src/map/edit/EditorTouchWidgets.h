// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "InGameTouchTheme.h"
#include <GUITextArea.h>
#include <ResponsiveDialog.h>
#include <TouchText.h>
#include <Toolkit.h>
#include <algorithm>

// Small drawing helpers shared by the editor's table and text workspace. These
// contain no editor state, actions, or layout decisions.
namespace EditorTouch
{
using GAGCore::ViewRect;
inline void panel(GAGCore::GraphicContext *gfx, ViewRect r)
{
	gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::paper);
	gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::border);
}
inline void label(GAGCore::GraphicContext *gfx, ViewRect r, const std::string &text)
{
	auto *font = GAGCore::Toolkit::getFont("standard");
	const double scale = gfx->logicalUnitsPerPoint();
	InGameTouchTheme::TextStyle ink(font);
	SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
	gfx->setUITransform(scale, r.x + 6 * scale,
						r.y + (r.h - font->getStringHeight(text) * scale) / 2, &clip);
	gfx->drawString(0, 0, font, text, std::max(1, int(r.w / scale - 12)));
	gfx->setUITransform();
	gfx->setClipRect();
}
inline void button(GAGCore::GraphicContext *gfx, ViewRect r, const std::string &text,
				   bool selected = false)
{
	gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h),
						selected ? InGameTouchTheme::selected : InGameTouchTheme::field);
	gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::border);
	label(gfx, r, text);
}

// Reuses TextArea's UTF-8 editing, file I/O, and cursor model. Only presentation
// and touch cursor placement are specialized; desktop paint remains untouched.
class TextCanvas : public GAGGUI::TextArea
{
	ViewRect canvasRect;
	double canvasScale = 1;

  public:
	using TextArea::TextArea;
	ViewRect canvasRectangle() const { return canvasRect; }
	void canvasBounds(ViewRect r)
	{
		canvasRect = r;
		auto *context = static_cast<GAGCore::GraphicContext *>(parent->getSurface());
		canvasScale = context->logicalUnitsPerPoint();
		const int width = std::max(16, int(r.w / canvasScale)),
				  height = std::max(24, int(r.h / canvasScale));
		if (w != width || h != height)
		{
			setScreenRectangle(0, 0, width, height);
			areaHeight = std::max(1, int((h - 8) / charHeight));
			layout();
			compute();
		}
		else
			setScreenPosition(0, 0);
	}
	void paintCanvas(bool numbered, const std::string &preedit,
					 const std::string &emptyLabel = "Tap to write the mission briefing")
	{
		auto *surface = static_cast<GAGCore::GraphicContext *>(parent->getSurface());
		const double gutter = numbered ? 36 * canvasScale : 0;
		SDL_Rect clip{int(canvasRect.x - gutter), int(canvasRect.y), int(canvasRect.w + gutter),
					  int(canvasRect.h)};
		surface->setUITransform(canvasScale, canvasRect.x, canvasRect.y, &clip);
		surface->drawFilledRect(x, y, w, h, GAGCore::Color(25, 22, 35));

		InGameTouchTheme::TextStyle ink(font);
		areaHeight = std::max(1, int((h - 8) / charHeight));
		for (size_t i = 0; i < areaHeight && areaPos + i < lines.size(); ++i)
		{
			const size_t row = areaPos + i,
						 end = row + 1 < lines.size() ? lines[row + 1] : text.size();
			const std::string line = text.substr(lines[row], end - lines[row]);
			surface->drawString(x + 4, y + 4 + i * charHeight, font, line.c_str(), w - 8);
		}
		if (activated && cursorPosY >= areaPos && cursorPosY < areaPos + areaHeight)
		{
			int cx = x + 4 + cursorScreenPosY, cy = y + 4 + (cursorPosY - areaPos) * charHeight;
			surface->drawLine(cx, cy, cx, cy + charHeight, InGameTouchTheme::ink);
			if (!preedit.empty())
				surface->drawString(cx, cy, font, preedit.c_str(), w - (cx - x) - 4);
		}
		if (text.empty() && !activated)
			surface->drawString(x + 8, y + 8, font,
								numbered ? "Tap to write the map script" : emptyLabel.c_str(),
								w - 16);

		if (numbered)
		{
			// Font drawing clips negative local coordinates before applying the
			// UI transform, so the gutter owns a positive local origin.
			SDL_Rect gutterClip{int(canvasRect.x - gutter), int(canvasRect.y), int(gutter),
								int(canvasRect.h)};
			surface->setUITransform(canvasScale, canvasRect.x - gutter, canvasRect.y, &gutterClip);
			surface->drawFilledRect(0, 0, 36, h, GAGCore::Color(31, 27, 43));
			for (size_t i = 0; i < areaHeight && areaPos + i < lines.size(); ++i)
			{
				const size_t pos = lines[areaPos + i];
				const unsigned number = 1 + std::count(text.begin(), text.begin() + pos, '\n');
				surface->drawString(2, 4 + i * charHeight, font, std::to_string(number).c_str());
			}
		}
		surface->setUITransform();
		surface->setClipRect();
	}
	void placeCursor(int px, int py)
	{
		px = int((px - canvasRect.x) / canvasScale);
		py = int((py - canvasRect.y) / canvasScale);
		const size_t row = std::min(lines.size() - 1,
									areaPos + size_t(std::max(0, (py - y - 4) / int(charHeight))));
		size_t at = lines[row], end = row + 1 < lines.size() ? lines[row + 1] : text.size();
		while (at < end && text[at] != '\n')
		{
			size_t next = GAGGUI::getNextUTF8Char(text, at);
			if (font->getStringWidth(text.substr(lines[row], next - lines[row]).c_str()) >
				px - x - 4)
				break;
			at = next;
		}
		setCursorPos(at);
		activate();
		notifyCursorMoved();
	}
	void scrollLines(int delta)
	{
		const int maximum = std::max(0, int(lines.size()) - int(areaHeight));
		areaPos = std::clamp(int(areaPos) + delta, 0, maximum);
	}
};
} // namespace EditorTouch
