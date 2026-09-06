// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Stubs for symbols referenced by Brush.o that BrushAccumulatorTest never
// exercises. BrushTool::draw/drawBrush call non-virtual sprite helpers on
// globalContainer->gfx; the accumulator paths under test touch neither.
// (drawRect is virtual, so it needs no stub — it dispatches via vtable.)

#include "GlobalContainer.h"

GlobalContainer *globalContainer = nullptr;

namespace GAGCore
{
	void DrawableSurface::drawSprite(int, int, Sprite *, unsigned, Uint8) {}
	void GraphicContext::finishDrawingSprite(Sprite *, Uint8) {}
}
