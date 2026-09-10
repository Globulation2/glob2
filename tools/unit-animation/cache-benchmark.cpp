// SPDX-License-Identifier: GPL-3.0-or-later
// Warmed sprite-only timings for sharp vs. motion-blur unit rendering, direct
// through GraphicContext::drawSprite -- no separate cache/composite draw mode
// exists anymore. GPU runs go through the unit shader unless
// GLOB2_DISABLE_UNIT_SHADER forces the bounded CPU fallback, in which case the
// cache's byte usage is also reported. This is sprite-only cost, excluding
// first-use texture uploads and every other draw call a real frame makes; see
// the top-level 12-team benchmark for whole-game figures.
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SDL.h>
#if defined(__APPLE__) || defined(OPENGL_HEADER_DIRECTORY_OPENGL)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "render/UnitAnimation.h"
int main(int argc, char **argv)
{
	bool gpu = argc > 1 && std::string(argv[1]) == "gpu";
	bool fallback = argc > 2 && std::string(argv[2]) == "fallback";
	if (gpu && fallback)
		setenv("GLOB2_DISABLE_UNIT_SHADER", "1", 1);
	GAGCore::Toolkit::init("codex-glob2-blur-benchmark");
	GAGCore::Sprite::setHighResolution(argc > 3 && std::string(argv[3]) == "hd");
	auto *gfx = GAGCore::Toolkit::initGraphic(1280, 480, gpu ? GAGCore::GraphicContext::USEGPU : 0,
	                                          "Unit blur benchmark");
	if (gpu)
		std::cout << "shader active: " << (gfx->hasUnitShader() ? "yes" : "no") << std::endl;
	auto *sprites = GAGCore::Toolkit::getSprite("data/gfx/unit");
	GAGCore::Color colors[] = {GAGCore::Color(255, 60, 40), GAGCore::Color(0, 255, 128),
	                           GAGCore::Color(70, 110, 255)};
	auto render = [&](bool blur, int step, int tick, int count)
	{
		gfx->drawFilledRect(0, 0, 1280, 480, 24, 31, 40);
		for (int u = 0; u < count; ++u)
		{
			sprites->setBaseColor(colors[u % 3]);
			int delta = (tick * step + (u % 16) * 16) & 255, dir = u % 8;
			int px = (u % 32) * 40, py = (u / 32) * 44;
			if (blur)
				drawUnitMotionBlur(64, dir, delta, step, [&](int f, int a)
				{ gfx->drawSprite(px, py, sprites, f, static_cast<Uint8>(a)); });
			else
				gfx->drawSprite(px, py, sprites, unitAnimationFrame(64, dir, delta));
		}
		if (gpu)
			glFinish();
	};
	for (int count : {10, 300})
		for (int step : {16, 30})
		{
			for (int t = 0; t < 128; ++t)
			{
				render(false, step, t, count);
				render(true, step, t, count);
			}
			std::vector<double> results[2];
			for (int round = 0; round < 6; ++round)
				for (int order = 0; order < 2; ++order)
				{
					bool blur = (order + round) % 2;
					SDL_PumpEvents();
					auto start = std::chrono::steady_clock::now();
					for (int t = 0; t < 64; ++t)
						render(blur, step, t, count);
					auto end = std::chrono::steady_clock::now();
					results[blur].push_back(
					    std::chrono::duration<double, std::milli>(end - start).count() / 64);
				}
			for (auto &r : results)
				std::sort(r.begin(), r.end());
			std::cout << (gpu ? (fallback ? "GPU-fallback" : "GPU-shader") : "software")
			          << " units=" << count << " step=" << step
			          << " sharp_ms=" << (results[0][2] + results[0][3]) / 2
			          << " blur_ms=" << (results[1][2] + results[1][3]) / 2
			          << " team_color_cache_entries=" << sprites->getTeamColorCacheEntries()
			          << " team_color_cache_bytes=" << sprites->getTeamColorCacheBytes() << std::endl;
		}
	GAGCore::Toolkit::close();
}
