// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL.h>
#include <vector>

namespace GAGCore { class GraphicContext; class Sprite; }

// Map overlays that keep their size on screen whatever the zoom (see
// ZoomDetail.h). Map passes queue them at a map position while the map transform
// is active; one flush draws them all in screen space, so a thousand bars cost a
// single transform change instead of one each.
struct MapOverlayQueue
{
	//! A point bar: the pips of Game::drawPointBar, laid out from an anchor.
	struct Bar
	{
		float screenX, screenY;      //!< the anchor, in logical screen units
		Sint16 offsetX, offsetY;     //!< bar corner from the anchor, in map pixels
		Sint16 maxLength, actLength, secondActLength;
		Uint8 vertical, reversed, barWidth, alpha;
		Uint8 r, g, b, r2, g2, b2;
	};
	//! A filled status dot with a dark rim.
	struct Pip
	{
		float screenX, screenY;
		Uint8 r, g, b, alpha;
	};

	//! A unit seen from too far to draw: a team-coloured shape with a dark rim.
	enum MarkerShape : Uint8 { Dot, Triangle, Diamond };
	struct Marker
	{
		float screenX, screenY, sizePoints;
		Uint8 shape, r, g, b, alpha;
	};
	//! A building or flag seen from too far to draw: a chip in its team's
	//! colour carrying an icon of what it is for. `icon` indexes the frames of
	//! GlobalContainer::mapIcons (tools/icons/export_map_icons.py); buildings
	//! get a rounded square, flags a disc.
	enum GlyphShape : Uint8 { Chip, Disc, Tile };
	struct Glyph
	{
		float left, top, right, bottom; //!< the footprint, in logical screen units
		Uint8 icon, shape, level, site, priority;
		Uint8 r, g, b, alpha;
	};

	std::vector<Bar> bars;
	std::vector<Pip> pips;
	std::vector<Marker> markers;
	std::vector<Glyph> glyphs;

	//! Sets where and how opaquely the bars queued next are drawn. The anchor is
	//! the map point that stays put while its bars change size around it.
	void anchor(GAGCore::GraphicContext &gfx, int mapX, int mapY, float opacity);
	//! Opacity given to the last anchor; nothing is queued at zero.
	float opacity() const { return anchorOpacity; }
	void bar(int mapX, int mapY, bool vertical, bool reversed, int maxLength, int actLength,
			 int secondActLength, Uint8 r, Uint8 g, Uint8 b, Uint8 r2, Uint8 g2, Uint8 b2,
			 int barWidth);
	void pip(GAGCore::GraphicContext &gfx, int mapX, int mapY, Uint8 r, Uint8 g, Uint8 b,
			 float opacity);

	void marker(GAGCore::GraphicContext &gfx, int mapX, int mapY, MarkerShape shape,
				float sizePoints, Uint8 r, Uint8 g, Uint8 b, float opacity);
	//! `priority` decides which glyph stays when several would cover each
	//! other. A Tile is the footprint filled with the team's colour, never
	//! enlarged or dropped: a wall stays a line of small tiles. `level` adds a
	//! pip per upgrade and `site` marks a construction site.
	void glyph(GAGCore::GraphicContext &gfx, int mapLeft, int mapTop, int mapRight, int mapBottom,
			   int icon, GlyphShape shape, int level, bool site, int priority, Uint8 r, Uint8 g,
			   Uint8 b, float opacity);

	//! Draws and empties the queue. `scale` converts map pixels of an overlay's
	//! own geometry to logical screen units (ZoomDetail::overlayScale);
	//! `unitsPerPoint` sizes markers and glyphs, which are measured in points.
	void flush(GAGCore::GraphicContext &gfx, GAGCore::Sprite *icons, double scale, double unitsPerPoint);

private:
	int anchorMapX = 0, anchorMapY = 0;
	float anchorScreenX = 0, anchorScreenY = 0, anchorOpacity = 1;
};
