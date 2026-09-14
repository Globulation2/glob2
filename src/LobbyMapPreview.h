// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GUIMapPreview.h"
#include "Utilities.h"
#include <Toolkit.h>
#include <algorithm>
#include <vector>

/// A colony's starting position and colour, drawn as a numbered marker over a map preview.
struct MapStart
{
	int x, y;
	Color color;
};

/// Draws a 128x128 letterboxed thumbnail cropped to the map's own aspect ratio into r, then a
/// numbered marker per colony start. Shared by the lobby's preview and the landscape picker.
inline void drawMapThumbnail(DrawableSurface *target, SDL_Rect r, DrawableSurface *thumbnail,
							 int mapW, int mapH, const std::vector<MapStart> &starts,
							 int marker = 16)
{
	if (!thumbnail || mapW <= 0 || mapH <= 0)
		return;
	// Thumbnails letterbox rectangular maps inside a 128x128 image. Crop that padding before
	// scaling, so terrain and colony markers share the same coordinate system.
	int extent, cropW, cropH, cropX, cropY;
	Utilities::computeMinimapData(MapPreview::PreviewSize, mapW, mapH, &extent, &cropW, &cropH,
								  &cropX, &cropY);
	target->drawSurface(r.x, r.y, r.w, r.h, thumbnail, cropX, cropY, cropW, cropH);
	auto font = Toolkit::getFont(marker >= 16 ? "standard" : "little");
	for (size_t i = 0; i < starts.size(); ++i)
	{
		const int startX = (starts[i].x % mapW + mapW) % mapW;
		const int startY = (starts[i].y % mapH + mapH) % mapH;
		int px = r.x + std::clamp(startX * r.w / mapW, 2, std::max(2, r.w - marker - 2));
		int py = r.y + std::clamp(startY * r.h / mapH, 2, std::max(2, r.h - marker - 2));
		target->drawFilledRect(px - 2, py - 2, marker + 4, marker + 4, 20, 30, 20);
		target->drawFilledRect(px, py, marker, marker, starts[i].color);
		font->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(0, 0, 0)));
		const std::string label = std::to_string(i + 1);
		target->drawString(px + (marker - font->getStringWidth(label)) / 2, py, font, label);
		font->popStyle();
	}
}

class LobbyMapPreview : public MapPreview
{
  public:
	using Start = MapStart;
	std::vector<Start> starts;
	LobbyMapPreview() : MapPreview(430, 115, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED) { w = h = 180; }
	void paint() override
	{
		if (!surface)
			return;
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		drawMapThumbnail(parent->getSurface(), {x, y, w, h}, surface, getLastWidth(),
						 getLastHeight(), starts);
	}
};
