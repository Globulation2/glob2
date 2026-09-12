// SPDX-License-Identifier: GPL-3.0-or-later
#include "FailureShapes.h"

#include <algorithm>

using namespace GAGCore;

Color failureShapeColor()
{
	return Color(230, 30, 30);
}

namespace
{
	// Half-width of a filled disc of radius r at row dy. The r*r+r bias is the
	// usual half-pixel one: it keeps the disc round rather than diamond-tipped
	// at the radii the badge is actually drawn at (9 and 11 pixels across).
	int discHalfWidth(int r, int dy)
	{
		int h = 0;
		while ((h+1)*(h+1) <= r*r + r - dy*dy)
			++h;
		return h;
	}

	void disc(GraphicContext* gfx, int cx, int cy, int r, const Color& color)
	{
		for (int dy = -r; dy <= r; ++dy)
		{
			const int h = discHalfWidth(r, dy);
			gfx->drawHorzLine(cx-h, cy+dy, 2*h+1, color);
		}
	}
}

void drawFailureShape(GraphicContext* gfx, int cx, int cy, int r, Building::UnitCantWorkReason /*reason*/, const Color& color)
{
	// Every reason wears the same badge: a mini "no entry" sign, a filled disc
	// with a white bar across the middle. Which reason applies is spelled out
	// as text next to the shape in the building panel; the tiny map badge only
	// needs to say "blocked". Drawn from the disc equation rather than from an
	// asset so it stays exact at any radius, including the interface scales
	// that blow the badge up several times over.
	disc(gfx, cx, cy, r+1, Color(0, 0, 0)); // a dark rim, so it reads on a unit of any team colour, red included
	disc(gfx, cx, cy, r, color);
	// The bar is a straight-ended rectangle clipped to the disc, about 2/7 of
	// it tall and 3/4 of it wide as on the sign itself, with a floor of one
	// pixel either way for the radii where those fractions round to nothing.
	const int barHalfH = std::max(1, (r+1)/4);
	const int barHalfW = r - std::max(1, r/4);
	for (int dy = -barHalfH; dy <= barHalfH; ++dy)
	{
		const int h = std::min(barHalfW, discHalfWidth(r, dy));
		gfx->drawHorzLine(cx-h, cy+dy, 2*h+1, Color(255, 255, 255));
	}
}
