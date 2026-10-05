// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ThemePainters.h"
#include <ui/Canvas.h>
#include <GraphicContext.h>
#include <array>
#include <cmath>
#include <memory>

namespace Glob2UI
{
using namespace GAGGUI::ui;

decltype(Theme::buttonPainter) spriteButtonPainter(const std::string &sprite)
{
	struct Slices
	{
		bool attempted = false, ready = false;
		// Left, left highlight, middle, middle highlight, right, right highlight.
		std::array<std::unique_ptr<GAGCore::DrawableSurface>, 6> frames;
	};
	auto slices = std::make_shared<Slices>();
	return [sprite, slices](Canvas &canvas, Rect bounds, const ButtonPaintState &state)
	{
		if (state.flat && !state.selected && !state.primary)
			return false;
		auto &s = *slices;
		if (!s.attempted)
		{
			s.attempted = true;
			s.ready = true;
			for (int i = 0; i < 6; ++i)
			{
				// Blended RGBA copies draw alike in the software and OpenGL renderers.
				GAGCore::DrawableSurface source(1, 1);
				SDL_Surface *pixels = source.loadImage(sprite + std::to_string(i) + ".webp")
										  ? SDL_ConvertSurface(source.getSDLSurface(), SDL_PIXELFORMAT_RGBA32)
										  : nullptr;
				s.ready = pixels && s.ready;
				if (!pixels)
					continue;
				SDL_SetSurfaceBlendMode(pixels, SDL_BLENDMODE_BLEND);
				s.frames[i] = std::make_unique<GAGCore::DrawableSurface>(pixels);
				SDL_DestroySurface(pixels);
			}
		}
		if (!s.ready || bounds.empty() || s.frames[0]->getH() <= 0)
			return false;
		const double scale = double(bounds.h) / s.frames[0]->getH();
		const int cap = std::min(bounds.w / 2, std::max(1, int(std::lround(s.frames[0]->getW() * scale))));
		const int tile = std::max(1, int(std::lround(s.frames[2]->getW() * scale)));
		auto paint = [&](int base, unsigned char alpha)
		{
			canvas.drawSurface({bounds.x, bounds.y, cap, bounds.h}, s.frames[base].get(), alpha);
			canvas.pushClip({bounds.x + cap, bounds.y, std::max(0, bounds.w - 2 * cap), bounds.h});
			for (int x = bounds.x + cap; x < bounds.right() - cap; x += tile)
				canvas.drawSurface({x, bounds.y, tile, bounds.h}, s.frames[base + 2].get(), alpha);
			canvas.popClip();
			canvas.drawSurface({bounds.right() - cap, bounds.y, cap, bounds.h}, s.frames[base + 4].get(), alpha);
		};
		paint(0, state.enabled ? 255 : 110);
		// The original lit buttons on hover; a selected button keeps a faint glow.
		const unsigned char glow = !state.enabled ? 0
								 : state.pressed  ? 255
								 : state.hovered  ? 190
								 : state.selected ? 70
												  : 0;
		if (glow)
			paint(1, glow);
		return true;
	};
}
} // namespace Glob2UI
