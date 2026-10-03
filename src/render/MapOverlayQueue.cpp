// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapOverlayQueue.h"

#include <GraphicContext.h>
#include <OpaqueRectangleBatch.h>
#include <algorithm>
#include <cmath>

using GAGCore::GraphicContext;

void MapOverlayQueue::anchor(GraphicContext &gfx, int mapX, int mapY, float opacity)
{
	anchorMapX = mapX;
	anchorMapY = mapY;
	anchorOpacity = std::clamp(opacity, 0.f, 1.f);
	gfx.mapToScreen(mapX, mapY, anchorScreenX, anchorScreenY);
}

void MapOverlayQueue::bar(int mapX, int mapY, bool vertical, bool reversed, int maxLength,
						  int actLength, int secondActLength, Uint8 r, Uint8 g, Uint8 b, Uint8 r2,
						  Uint8 g2, Uint8 b2, int barWidth)
{
	const Uint8 alpha = Uint8(std::lround(anchorOpacity * 255));
	if (!alpha)
		return;
	bars.push_back({anchorScreenX, anchorScreenY, Sint16(mapX - anchorMapX),
					Sint16(mapY - anchorMapY), Sint16(maxLength), Sint16(actLength),
					Sint16(secondActLength), Uint8(vertical), Uint8(reversed), Uint8(barWidth),
					alpha, r, g, b, r2, g2, b2});
}

void MapOverlayQueue::pip(GraphicContext &gfx, int mapX, int mapY, Uint8 r, Uint8 g, Uint8 b,
						  float opacity)
{
	const Uint8 alpha = Uint8(std::lround(std::clamp(opacity, 0.f, 1.f) * 255));
	if (!alpha)
		return;
	Pip pip{0, 0, r, g, b, alpha};
	gfx.mapToScreen(mapX, mapY, pip.screenX, pip.screenY);
	pips.push_back(pip);
}

void MapOverlayQueue::marker(GraphicContext &gfx, int mapX, int mapY, MarkerShape shape,
							 float sizePoints, Uint8 r, Uint8 g, Uint8 b, float opacity)
{
	const Uint8 alpha = Uint8(std::lround(std::clamp(opacity, 0.f, 1.f) * 255));
	if (!alpha)
		return;
	Marker marker{0, 0, sizePoints, Uint8(shape), r, g, b, alpha};
	gfx.mapToScreen(mapX, mapY, marker.screenX, marker.screenY);
	markers.push_back(marker);
}

void MapOverlayQueue::glyph(GraphicContext &gfx, int mapLeft, int mapTop, int mapRight,
							int mapBottom, int icon, GlyphShape shape, int level, bool site,
							int priority, Uint8 r, Uint8 g, Uint8 b, float opacity)
{
	const Uint8 alpha = Uint8(std::lround(std::clamp(opacity, 0.f, 1.f) * 255));
	if (!alpha)
		return;
	Glyph glyph{0, 0, 0, 0, Uint8(icon), Uint8(shape), Uint8(std::clamp(level, 0, 3)), Uint8(site),
				Uint8(std::clamp(priority, 0, 255)), r, g, b, alpha};
	gfx.mapToScreen(mapLeft, mapTop, glyph.left, glyph.top);
	gfx.mapToScreen(mapRight, mapBottom, glyph.right, glyph.bottom);
	glyphs.push_back(glyph);
}

namespace
{
// Half-width of row `row` of a marker `size` rows tall, in pixels.
int markerHalfWidth(Uint8 shape, int row, int size)
{
	switch (shape)
	{
		case MapOverlayQueue::Triangle: return (row + 2) / 2;
		case MapOverlayQueue::Diamond: return std::min(row, size - 1 - row) + 1;
		default: return (row == 0 || row == size - 1) && size > 2 ? size / 2 - 1 : size / 2;
	}
}

void fillMarker(GraphicContext &gfx, Uint8 shape, int centerX, int centerY, int size, int grow,
				Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	const int top = centerY - size / 2;
	for (int row = -grow; row < size + grow; row++)
	{
		// The rim repeats the nearest row of the shape, one pixel wider.
		const int half = markerHalfWidth(shape, std::clamp(row, 0, size - 1), size) + grow;
		gfx.drawFilledRect(centerX - half, top + row, std::max(1, 2 * half), 1, r, g, b, a);
	}
}
}

void MapOverlayQueue::flush(GraphicContext &gfx, GAGCore::Sprite *icons, double scale, double unitsPerPoint)
{
	if (bars.empty() && pips.empty() && markers.empty() && glyphs.empty())
		return;
	int x = 0, y = 0, sx, sy, sw, sh;
	gfx.beginScreenOverlay(x, y, sx, sy, sw, sh);
	{
		// The most important glyphs claim their place first; one that an already
		// placed glyph would cover much of is left out rather than piled on top.
		constexpr double SmallestChipPoints = 15, LargestChipPoints = 26, FlagDiscPoints = 17;
		constexpr float MostGlyphOverlap = 0.15f;
		// Icon frames per icon, by edge length in target pixels
		// (tools/icons/export_map_icons.py).
		static const int iconSizes[] = {10, 12, 16, 20, 24, 32, 48};
		constexpr int IconSizeCount = int(sizeof(iconSizes) / sizeof(iconSizes[0]));
		const float raster = gfx.getRasterScale();
		std::stable_sort(glyphs.begin(), glyphs.end(),
			[](const Glyph &a, const Glyph &b) { return a.priority > b.priority; });
		std::vector<SDL_FRect> placed;
		placed.reserve(glyphs.size());
		for (const Glyph &glyph : glyphs)
		{
			const float centerX = (glyph.left + glyph.right) / 2, centerY = (glyph.top + glyph.bottom) / 2;
			const float footprint = std::max(1.f, std::min(glyph.right - glyph.left, glyph.bottom - glyph.top));
			if (glyph.shape == Tile)
			{
				const int size = std::max(1, int(std::lround(footprint)));
				gfx.drawFilledRect(int(std::lround(glyph.left)), int(std::lround(glyph.top)), size, size, glyph.r, glyph.g, glyph.b, glyph.alpha);
				continue;
			}
			// A bigger building gets a bigger chip, within a narrow range.
			const float side = glyph.shape == Disc ? float(FlagDiscPoints * unitsPerPoint)
				: std::clamp(footprint, float(SmallestChipPoints * unitsPerPoint), float(LargestChipPoints * unitsPerPoint));
			const int size = std::max(6, int(std::lround(side)));
			const SDL_FRect box{centerX - size / 2.f, centerY - size / 2.f, float(size), float(size)};
			bool covered = false;
			for (const SDL_FRect &other : placed)
			{
				const float overlapW = std::min(box.x + box.w, other.x + other.w) - std::max(box.x, other.x);
				const float overlapH = std::min(box.y + box.h, other.y + other.h) - std::max(box.y, other.y);
				if (overlapW > 0 && overlapH > 0 && overlapW * overlapH > MostGlyphOverlap * box.w * box.h)
				{
					covered = true;
					break;
				}
			}
			if (covered)
				continue;
			placed.push_back(box);
			const int left = int(std::lround(box.x)), top = int(std::lround(box.y));
			// The chip: a dark rim, then the team's colour inside it. A
			// construction site is a paler chip. A square chip has its corners
			// cut, which at these sizes reads as rounded and takes three
			// rectangles a layer; only the few flag discs are drawn by rows.
			const Uint8 fill = glyph.site ? Uint8(glyph.alpha * 3 / 5) : glyph.alpha;
			const int corner = std::max(1, size / 7);
			const auto layer = [&](int inset, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
			{
				const int edge = size - 2 * inset;
				if (glyph.shape == Disc)
				{
					const float radius = edge / 2.f;
					for (int row = 0; row < edge; row++)
					{
						const float dy = row + 0.5f - radius;
						const int half = int(std::lround(std::sqrt(std::max(0.f, radius * radius - dy * dy))));
						if (half > 0)
							gfx.drawFilledRect(left + size / 2 - half, top + inset + row, 2 * half, 1, r, g, b, a);
					}
					return;
				}
				const int cut = std::max(1, corner - inset);
				gfx.drawFilledRect(left + inset + cut, top + inset, edge - 2 * cut, cut, r, g, b, a);
				gfx.drawFilledRect(left + inset, top + inset + cut, edge, edge - 2 * cut, r, g, b, a);
				gfx.drawFilledRect(left + inset + cut, top + size - inset - cut, edge - 2 * cut, cut, r, g, b, a);
			};
			layer(0, 12, 14, 22, glyph.alpha);
			layer(1, glyph.r, glyph.g, glyph.b, fill);
			if (!icons)
				continue;
			// The largest icon frame that fits inside the chip, drawn pixel for pixel.
			const float room = (size - std::max(4.f, size * 0.24f)) * raster;
			int frameSize = 0;
			for (int i = 0; i < IconSizeCount; i++)
				if (iconSizes[i] <= room)
					frameSize = i;
			const unsigned frame = unsigned(glyph.icon) * IconSizeCount + frameSize;
			const float drawn = iconSizes[frameSize] / raster;
			gfx.drawSprite(left + (size - drawn) / 2, top + (size - drawn) / 2, drawn, drawn, icons, frame, glyph.alpha);
			// One pip per upgrade level along the bottom edge.
			const int pip = std::max(1, size / 9);
			for (int level = 0; level < glyph.level; level++)
				gfx.drawFilledRect(left + corner + level * (pip + 1), top + size - pip - 2, pip, pip, 255, 255, 255, glyph.alpha);
		}
		if (icons)
			gfx.finishDrawingSprite(icons, 255);
		for (const Marker &marker : markers)
		{
			const int size = std::max(2, int(std::lround(marker.sizePoints * unitsPerPoint)));
			const int centerX = int(std::lround(marker.screenX)), centerY = int(std::lround(marker.screenY));
			fillMarker(gfx, marker.shape, centerX, centerY, size, 1, 0, 0, 0, marker.alpha);
			fillMarker(gfx, marker.shape, centerX, centerY, size, 0, marker.r, marker.g, marker.b, marker.alpha);
		}
		GAGCore::OpaqueRectangleBatch rectangles(&gfx);
		for (const Bar &bar : bars)
		{
			// Bar-local map pixels to screen, each edge rounded on its own so
			// neighbouring pips share boundaries at any scale.
			const double originX = bar.screenX + bar.offsetX * scale;
			const double originY = bar.screenY + bar.offsetY * scale;
			const auto edgeX = [&](int v) { return int(std::lround(originX + v * scale)); };
			const auto edgeY = [&](int v) { return int(std::lround(originY + v * scale)); };
			// along: pip axis; across: bar thickness.
			const auto rect = [&](int along, int across, int length, int thickness, bool filled,
								  Uint8 r, Uint8 g, Uint8 b)
			{
				const int ax = bar.vertical ? across : along, ay = bar.vertical ? along : across;
				const int aw = bar.vertical ? thickness : length;
				const int ah = bar.vertical ? length : thickness;
				const int left = edgeX(ax), top = edgeY(ay);
				const int w = std::max(1, edgeX(ax + aw) - left);
				const int h = std::max(1, edgeY(ay + ah) - top);
				if (filled)
					gfx.drawFilledRect(left, top, w, h, r, g, b, bar.alpha);
				else
					gfx.drawRect(left, top, w, h, r, g, b, bar.alpha);
			};
			rect(0, 0, bar.maxLength * 3 + 1, bar.barWidth + 2, true, 0, 0, 0);
			// Pips fill from the start, or from the end when reversed; the
			// primary colour comes first, then the secondary, then empty slots.
			for (int i = 0; i < bar.maxLength; i++)
			{
				const int slot = bar.reversed ? bar.maxLength - 1 - i : i;
				if (i < bar.actLength)
					rect(slot * 3 + 1, 1, 2, bar.barWidth, true, bar.r, bar.g, bar.b);
				else if (i < bar.actLength + bar.secondActLength)
					rect(slot * 3 + 1, 1, 2, bar.barWidth, true, bar.r2, bar.g2, bar.b2);
				else
					rect(slot * 3, 0, 4, bar.barWidth + 2, false, bar.r / 3, bar.g / 3, bar.b / 3);
			}
		}
		for (const Pip &pip : pips)
		{
			const int size = std::max(3, int(std::lround(5 * scale)));
			const int left = int(std::lround(pip.screenX)) - size / 2;
			const int top = int(std::lround(pip.screenY)) - size / 2;
			gfx.drawFilledRect(left - 1, top - 1, size + 2, size + 2, 0, 0, 0, pip.alpha);
			gfx.drawFilledRect(left, top, size, size, pip.r, pip.g, pip.b, pip.alpha);
		}
	}
	gfx.endScreenOverlay();
	bars.clear();
	pips.clear();
	markers.clear();
	glyphs.clear();
}
