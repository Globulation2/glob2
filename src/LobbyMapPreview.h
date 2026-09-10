// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GUIMapPreview.h"
#include "Utilities.h"
#include <Toolkit.h>
#include <algorithm>
#include <vector>

class LobbyMapPreview : public MapPreview
{
  public:
	struct Start
	{
		int x, y;
		Color color;
	};
	std::vector<Start> starts;
	LobbyMapPreview() : MapPreview(430, 115, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED) { w = h = 180; }
	void paint() override
	{
		if (!surface)
			return;
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		auto target = parent->getSurface();
		// Thumbnails letterbox rectangular maps inside a 128x128 image. Crop
		// that padding before scaling, so terrain and colony markers share
		// the same coordinate system.
		int extent, cropW, cropH, cropX, cropY;
		Utilities::computeMinimapData(PreviewSize, getLastWidth(), getLastHeight(),
			&extent, &cropW, &cropH, &cropX, &cropY);
		target->drawSurface(x, y, w, h, surface, cropX, cropY, cropW, cropH);
		auto font = Toolkit::getFont("standard");
		for (size_t i = 0; i < starts.size(); ++i)
		{
			const int mapW = getLastWidth(), mapH = getLastHeight();
			const int startX = (starts[i].x % mapW + mapW) % mapW;
			const int startY = (starts[i].y % mapH + mapH) % mapH;
			int px = x + std::clamp(startX * w / mapW, 2, std::max(2, w - 18));
			int py = y + std::clamp(startY * h / mapH, 2, std::max(2, h - 18));
			target->drawFilledRect(px - 2, py - 2, 20, 20, 20, 30, 20);
			target->drawFilledRect(px, py, 16, 16, starts[i].color);
			font->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(0, 0, 0)));
			target->drawString(px + 3, py, font, std::to_string(i + 1));
			font->popStyle();
		}
	}
};
