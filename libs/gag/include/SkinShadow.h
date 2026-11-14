// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace GAGCore
{
class DrawableSurface;
class GraphicContext;
class Sprite;
// A shadow's logical canvas is independent of its texture resolution. Stock HD
// surfaces can be 128 pixels across while representing a 38- or 40-pixel unit.
// A sprite frame source is resolved only when the shadow is drawn, so artwork
// with no skin to draw never asks for that frame's HD layer.
struct SkinShadow
{
	DrawableSurface *surface = nullptr;
	unsigned width = 0, height = 0;
	Sprite *sprite = nullptr;
	unsigned frame = 0;
};
// Center the shadow on the mesh canvas, scaling both by the same world scale.
void drawSkinShadow(GraphicContext &gfx, SkinShadow shadow, float x, float y, float w, float h,
					unsigned logicalSize, std::uint8_t alpha);
} // namespace GAGCore
