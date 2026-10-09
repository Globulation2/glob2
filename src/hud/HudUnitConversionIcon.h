// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDLGraphicContext.h>
#include <ViewportTransform.h>
#include "UnitConsts.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

// Presentation shared by the desktop and touch HUDs. Cached GPU surfaces live
// with the owning GameGUI, rather than surviving its graphics context.
class HudUnitConversionIcon
{
	std::array<std::unique_ptr<GAGCore::DrawableSurface>, 2> arrows;
	std::array<double, 2> rasterKey{};

  public:
	void draw(GAGCore::GraphicContext *gfx, GAGCore::Sprite *glob,
		GAGCore::ViewRect rect, GAGCore::Color teamColor, bool gained)
	{
		using namespace GAGCore;
		const double unit = gfx->logicalUnitsPerPoint(), density = std::max(.25, double(gfx->getRasterScale()));
		const std::array<double, 2> key{unit, density};
		if (key != rasterKey)
		{
			for (auto &arrow : arrows) arrow.reset();
			rasterKey = key;
		}
		const int index = gained ? 0 : 1;
		if (!arrows[index])
		{
			const int size = std::max(1, int(std::ceil(28 * unit * density)));
			auto *pixels = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_ARGB8888);
			if (!pixels) return;
			SDL_SetSurfaceBlendMode(pixels, SDL_BLENDMODE_BLEND);
			const Color ink = gained ? Color(139, 225, 163) : Color(241, 157, 154);
			for (int row = 0; row < size; ++row)
				for (int col = 0; col < size; ++col)
				{
					const double x = (col + .5) / (density * unit) - 14;
					const double y = (row + .5) / (density * unit) - 14;
					double distance = 100;
					const auto line = [&](double ax, double ay, double bx, double by) {
						const double dx = bx - ax, dy = by - ay;
						const double t = std::clamp(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy), 0., 1.);
						distance = std::min(distance, std::hypot(x - ax - t * dx, y - ay - t * dy) - 1.05);
					};
					line(-10, 0, 10, 0);
					if (gained) { line(-10, 0, -4, -6); line(-10, 0, -4, 6); }
					else { line(10, 0, 4, -6); line(10, 0, 4, 6); }
					const Uint32 alpha = Uint8(std::round(255 * std::clamp(.5 - distance * density * unit, 0., 1.)));
					auto *scan = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels->pixels) + row * pixels->pitch);
					scan[col] = (alpha << 24) | (Uint32(ink.r) << 16) | (Uint32(ink.g) << 8) | ink.b;
				}
			arrows[index] = std::make_unique<DrawableSurface>(pixels, DrawableSurface::AdoptPixels{});
		}
		const auto drawGlob = [&](double x, Color color) {
			glob->setBaseColor(color);
			const double scale = std::min(rect.h / glob->getW(WORKER), rect.h / glob->getH(WORKER));
			SDL_Rect clip{int(x), int(rect.y), int(std::ceil(rect.h)), int(std::ceil(rect.h))};
			gfx->setUITransform(scale, x + (rect.h - glob->getW(WORKER) * scale) / 2,
				rect.y + (rect.h - glob->getH(WORKER) * scale) / 2, &clip);
			gfx->drawSprite(0, 0, glob, WORKER);
			gfx->setUITransform(); gfx->setClipRect();
		};
		// Our team remains on the left in both counters. The same Glob changes
		// colour across an arrow pointing toward us for gains and away for losses.
		drawGlob(rect.x, teamColor);
		drawGlob(rect.x + rect.w - rect.h, Color(184, 170, 205));
		glob->setBaseColor(teamColor);
		const double arrowSize = .56 * rect.h;
		gfx->drawSurface(float(rect.x + (rect.w - arrowSize) / 2), float(rect.y + (rect.h - arrowSize) / 2),
			float(arrowSize), float(arrowSize), arrows[index].get());
	}
};
