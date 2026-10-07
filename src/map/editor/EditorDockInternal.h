// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Painting helpers shared by the editor dock's source files.

#include "GlobalContainer.h"
#include "ui/FrontendUI.h"
#include <GraphicContext.h>
#include <algorithm>

namespace EditorDockPaint
{
// Draws a sprite frame scaled to fit `r` (never above `maxScale`), centred.
// Recording canvases (harness layout passes) have no surface and draw nothing.
inline void spriteFit(GAGGUI::ui::Canvas &canvas, GAGGUI::ui::Rect r, GAGCore::Sprite *sprite, int frame,
					  double maxScale = 2.0)
{
	if (!sprite || !canvas.surface() || r.empty())
		return;
	const int w = sprite->getW(frame), h = sprite->getH(frame);
	if (w <= 0 || h <= 0)
		return;
	const double scale = std::min({double(r.w) / w, double(r.h) / h, maxScale});
	const int dw = int(w * scale), dh = int(h * scale);
	canvas.transformed(scale, {r.x + (r.w - dw) / 2, r.y + (r.h - dh) / 2}, r,
					   [&] { globalContainer->gfx->drawSprite(0, 0, sprite, frame); });
}
} // namespace EditorDockPaint
