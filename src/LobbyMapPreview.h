// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GUIMapPreview.h"
#include "Utilities.h"
#include <Toolkit.h>
#include <algorithm>
#include <vector>
#include <filesystem>
#include "MapHeader.h"

/// A colony's starting position and colour, drawn as a numbered marker over a map preview.
struct MapStart
{
	int x, y;
	Color color;
};

/// Draws tightly packed terrain fitted into r, then a
/// numbered marker per colony start. Shared by the lobby's preview and the landscape picker.
inline void drawMapThumbnail(DrawableSurface *target, SDL_Rect r, DrawableSurface *thumbnail,
							 int mapW, int mapH, const std::vector<MapStart> &starts,
							 std::unique_ptr<DrawableSurface> &raster, int marker = 16)
{
	if (!thumbnail || mapW <= 0 || mapH <= 0)
		return;
// Cache the viewport raster, including in software mode where the graphics
// wrapper's scaled blit is not implemented. The picker resets it on new pixels.
if (!raster || raster->getW() != r.w || raster->getH() != r.h)
{
	raster = std::make_unique<DrawableSurface>(r.w, r.h);
	SDL_Rect destination{0, 0, r.w, r.h};
	SDL_BlitScaled(thumbnail->getSDLSurface(), nullptr, raster->getSDLSurface(), &destination);
}
target->drawSurface(r.x, r.y, raster.get());
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
	struct CachedMap
	{
		std::string path;
		std::filesystem::file_time_type time;
		uintmax_t bytes;
		MapHeader header;
		MapThumbnail terrain;
		std::vector<Start> starts;
	};
	std::vector<CachedMap> cache;
	LobbyMapPreview() : MapPreview(430, 115, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED) { w = h = 180; }
	void paintOverlay(DrawableSurface *target, MapPreviewGeometry::Rect area) override
	{
		auto font = Toolkit::getFont("standard");
		for (size_t i = 0; i < starts.size(); ++i)
		{
			const int anchorX = view.x(starts[i].x, getLastWidth(), area);
			const int anchorY = view.y(starts[i].y, getLastHeight(), area);
			// Repeat clipped markers at seams, never move a colony away from
			// its terrain just to keep its label inside the viewport.
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					int px = anchorX + dx * area.w, py = anchorY + dy * area.h;
					target->drawFilledRect(px - 10, py - 10, 20, 20, Color(20, 30, 20));
					target->drawFilledRect(px - 8, py - 8, 16, 16, starts[i].color);
					font->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(0, 0, 0)));
					const auto label = std::to_string(i + 1);
					target->drawString(px - font->getStringWidth(label) / 2, py - 8, font, label);
					font->popStyle();
				}
		}
	}
};
