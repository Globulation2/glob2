// SPDX-License-Identifier: GPL-3.0-or-later
#include "FailureShapes.h"

using namespace GAGCore;

Color failureShapeColor()
{
	return Color(230, 30, 30);
}

static void outline(GraphicContext* gfx, int cx, int cy, int r, Building::UnitCantWorkReason /*reason*/, const Color& color)
{
	// Every reason wears the same badge: a mini stop sign, octagon outline
	// with the bar of a "no entry" sign through the middle, no lettering
	// (there isn't a pixel to spare for "STOP" at this size anyway). Which
	// reason applies is spelled out as text next to the shape in the
	// building panel; the tiny map badge only needs to say "blocked".
	const int c = r/2; // corner cut that turns the square into an octagon
	gfx->drawLine(cx-c, cy-r, cx+c, cy-r, color); // top
	gfx->drawLine(cx+c, cy-r, cx+r, cy-c, color); // top-right
	gfx->drawLine(cx+r, cy-c, cx+r, cy+c, color); // right
	gfx->drawLine(cx+r, cy+c, cx+c, cy+r, color); // bottom-right
	gfx->drawLine(cx+c, cy+r, cx-c, cy+r, color); // bottom
	gfx->drawLine(cx-c, cy+r, cx-r, cy+c, color); // bottom-left
	gfx->drawLine(cx-r, cy+c, cx-r, cy-c, color); // left
	gfx->drawLine(cx-r, cy-c, cx-c, cy-r, color); // top-left
	gfx->drawLine(cx-r, cy, cx+r, cy, color);     // bar
}

void drawFailureShape(GraphicContext* gfx, int cx, int cy, int r, Building::UnitCantWorkReason reason, const Color& color)
{
	// A dark halo first, so the shape reads on a unit of any team colour, red included.
	outline(gfx, cx, cy, r+1, reason, Color(0, 0, 0));
	outline(gfx, cx, cy, r, reason, color);
}
