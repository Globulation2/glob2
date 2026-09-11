// SPDX-License-Identifier: GPL-3.0-or-later
#include "FailureShapes.h"

using namespace GAGCore;

Color failureShapeColor()
{
	return Color(230, 30, 30);
}

static void outline(GraphicContext* gfx, int cx, int cy, int r, Building::UnitCantWorkReason reason, const Color& color)
{
	switch (reason)
	{
		case Building::UnitNotAvailable:
			// square
			gfx->drawRect(cx-r, cy-r, 2*r, 2*r, color);
			break;
		case Building::UnitTooLowLevel:
			// triangle, point up
			gfx->drawLine(cx-r, cy+r, cx+r, cy+r, color);
			gfx->drawLine(cx-r, cy+r, cx, cy-r, color);
			gfx->drawLine(cx+r, cy+r, cx, cy-r, color);
			break;
		case Building::UnitCantAccessBuilding:
			// cross
			gfx->drawLine(cx-r, cy-r, cx+r, cy+r, color);
			gfx->drawLine(cx-r, cy+r, cx+r, cy-r, color);
			break;
		case Building::UnitTooFarFromBuilding:
			// circle
			gfx->drawCircle(cx, cy, r, color);
			break;
		case Building::UnitCantAccessResource:
			// diamond
			gfx->drawLine(cx, cy-r, cx+r, cy, color);
			gfx->drawLine(cx+r, cy, cx, cy+r, color);
			gfx->drawLine(cx, cy+r, cx-r, cy, color);
			gfx->drawLine(cx-r, cy, cx, cy-r, color);
			break;
		case Building::UnitCantAccessFruit:
			// triangle, point down
			gfx->drawLine(cx-r, cy-r, cx+r, cy-r, color);
			gfx->drawLine(cx-r, cy-r, cx, cy+r, color);
			gfx->drawLine(cx+r, cy-r, cx, cy+r, color);
			break;
		case Building::UnitTooFarFromResource:
			// two rings
			gfx->drawCircle(cx, cy, r, color);
			gfx->drawCircle(cx, cy, r/2, color);
			break;
		case Building::UnitTooFarFromFruit:
			// plus
			gfx->drawLine(cx-r, cy, cx+r, cy, color);
			gfx->drawLine(cx, cy-r, cx, cy+r, color);
			break;
		default:
			break;
	}
}

void drawFailureShape(GraphicContext* gfx, int cx, int cy, int r, Building::UnitCantWorkReason reason, const Color& color)
{
	// A dark halo first, so the shape reads on a unit of any team colour, red included.
	outline(gfx, cx, cy, r+1, reason, Color(0, 0, 0));
	outline(gfx, cx, cy, r, reason, color);
}
