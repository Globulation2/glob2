// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace GAGCore
{
class DrawableSurface;
class GraphicContext;
// A shadow's logical canvas is independent of its texture resolution. Stock HD
// surfaces can be 128 pixels across while representing a 38- or 40-pixel unit.
struct SkinShadow
{
	DrawableSurface *surface = nullptr;
	unsigned width = 0, height = 0;
};
// Center the shadow on the mesh canvas, scaling both by the same world scale.
void drawSkinShadow(GraphicContext &gfx, SkinShadow shadow, float x, float y, float w, float h,
					unsigned logicalSize, std::uint8_t alpha);
} // namespace GAGCore
