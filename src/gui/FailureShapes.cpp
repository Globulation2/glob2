// SPDX-License-Identifier: GPL-3.0-or-later
#include "FailureShapes.h"

#include <algorithm>
#include <cmath>

using namespace GAGCore;

Color failureShapeColor()
{
	return Color(230, 30, 30);
}

namespace
{
	// How much of one pixel a shape covers, from how far that pixel's centre
	// lies inside the shape's edge: full half a pixel in, empty half a pixel
	// out, linear across the edge itself. The game's own artwork is drawn this
	// way -- data/gfx/area-forbidden0.png, the overlay for a forbidden area,
	// has 16 opaque pixels against 126 partly transparent ones -- so a badge
	// with hard edges would be the one thing on screen that stair-steps.
	float coverage(float inside)
	{
		return std::clamp(inside + 0.5f, 0.0f, 1.0f);
	}

	Uint8 alphaOf(float cov)
	{
		return static_cast<Uint8>(cov * 255.0f + 0.5f);
	}

	// One row of a shape, with pixels of equal alpha merged into a single
	// line: a badge then costs a few draw calls per row rather than one per
	// pixel, and the solid middle of the disc costs exactly one.
	template<typename AlphaAt>
	void drawRow(GraphicContext* gfx, int cx, int y, int span, const Color& color, AlphaAt alphaAt)
	{
		int runStart = -span;
		int runAlpha = alphaAt(-span);
		for (int dx = -span+1; dx <= span+1; ++dx)
		{
			const int alpha = dx <= span ? alphaAt(dx) : -1;
			if (alpha == runAlpha)
				continue;
			if (runAlpha > 0)
				gfx->drawHorzLine(cx+runStart, y, dx-runStart, Color(color.r, color.g, color.b, static_cast<Uint8>(runAlpha)));
			runStart = dx;
			runAlpha = alpha;
		}
	}
}

void drawFailureShape(GraphicContext* gfx, int cx, int cy, int r, Building::UnitCantWorkReason /*reason*/, const Color& color)
{
	// Every reason wears the same badge: a mini "no entry" sign, a red disc
	// with a white bar across the middle inside a white border. Which reason
	// applies is spelled out as text next to the shape in the building panel;
	// the tiny map badge only needs to say "blocked", and several reasons can
	// apply to the same unit at once with only the first one reported, so
	// telling them apart by shape would be a promise the engine cannot keep.
	//
	// Drawn from the circle equation rather than loaded from an asset, so the
	// badge is exact at any size the interface is laid out at, including the
	// scaled-up interface where a fixed-size sprite would be resampled.
	const float radius = r + 0.5f;       // the red disc: 2r+1 pixels across
	const float border = radius + 1.0f;  // the sign's white border, a pixel of it
	// The bar keeps the sign's proportions as the badge grows: a little under
	// a third of the disc tall and three quarters of it wide.
	const float barHalfH = radius * 2.0f / 7.0f;
	const float barHalfW = radius * 3.0f / 4.0f;
	const Color white(255, 255, 255);
	const int span = r + 2;
	for (int dy = -span; dy <= span; ++dy)
	{
		auto distance = [dy](int dx) { return std::sqrt(static_cast<float>(dx*dx + dy*dy)); };
		// The border is a full disc rather than a ring, so the red disc's own
		// soft edge blends into white instead of into whatever is behind the
		// badge. Without it the sign disappears on a red team's own units,
		// which are exactly the units it is ever drawn on.
		drawRow(gfx, cx, cy+dy, span, white,
			[&](int dx) { return alphaOf(coverage(border - distance(dx))); });
		drawRow(gfx, cx, cy+dy, span, color,
			[&](int dx) { return alphaOf(coverage(radius - distance(dx))); });
		drawRow(gfx, cx, cy+dy, span, white, [&](int dx) {
			return alphaOf(std::min({coverage(barHalfW - std::abs(dx)),
			                         coverage(barHalfH - std::abs(dy)),
			                         coverage(radius - distance(dx))}));
		});
	}
}
