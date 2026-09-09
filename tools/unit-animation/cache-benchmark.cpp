// SPDX-License-Identifier: GPL-3.0-or-later
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SDL.h>
#if defined(__APPLE__) || defined(OPENGL_HEADER_DIRECTORY_OPENGL)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include "render/UnitAnimation.h"
int main(int argc, char **argv)
{
	bool gpu = argc > 1 && std::string(argv[1]) == "gpu";
	GAGCore::Toolkit::init("codex-glob2-blur-benchmark");
	GAGCore::Sprite::setHighResolution(argc > 2 && std::string(argv[2]) == "hd");
	auto *gfx = GAGCore::Toolkit::initGraphic(1280, 480, gpu ? GAGCore::GraphicContext::USEGPU : 0,
	                                          "Unit blur benchmark");
	auto *sprites = GAGCore::Toolkit::getSprite("data/gfx/unit");
	auto *original = new GAGCore::Sprite();
	original->load("data/gfx/unit");
	auto *cachedSprite = sprites;
	const bool budgeted = argc > 4 && std::string(argv[4]) == "budgeted";
	GAGCore::Color colors[] = {GAGCore::Color(255, 60, 40), GAGCore::Color(0, 255, 128),
	                           GAGCore::Color(70, 110, 255)};
	auto render = [&](int blur, int step, int tick, int count)
	{
		auto *sprites = blur == 2 ? cachedSprite : original;
		GAGCore::Sprite::beginCompositeFrame();
		gfx->drawFilledRect(0, 0, 1280, 480, 24, 31, 40);
		for (int u = 0; u < count; ++u)
		{
			sprites->setBaseColor(colors[u % 3]);
			int delta = (tick * step + (u % 16) * 16) & 255, dir = u % 8;
			auto draw = [&](int id, int alpha)
			{ gfx->drawSprite((u % 32) * 40, (u / 32) * 44, sprites, id, alpha); };
			if (blur == 2)
			{
				std::vector<std::pair<int, int>> frames;
				drawUnitMotionBlur(64, dir, delta, step,
				                   [&](int f, int a) { frames.emplace_back(f, a); });
				if (budgeted)
					sprites->drawCachedComposite(gfx, (u % 32) * 40, (u / 32) * 44,
					                             unitAnimationFrame(64, dir, delta), frames);
				else
					gfx->drawSurface((u % 32) * 40, (u / 32) * 44, 38, 38,
					                 sprites->getCachedComposite(frames));
			}
			else if (blur)
				drawUnitMotionBlur(64, dir, delta, step, draw);
			else
				draw(unitAnimationFrame(64, dir, delta), 255);
		}
		if (gpu)
			glFinish();
	};
	// Cold-cache measurement includes generation and first texture uploads.
	if (argc > 3 && std::string(argv[3]) == "cold")
	{
		std::vector<double> times;
		for (int tick = 0; tick < 128; ++tick)
		{
			auto start = std::chrono::steady_clock::now();
			render(2, 30, tick, 300);
			times.push_back(std::chrono::duration<double, std::milli>(
			    std::chrono::steady_clock::now() - start).count());
		}
		const double first = times.front();
		std::sort(times.begin(), times.end());
		std::cout << "cold_cache units=300 first_ms=" << first
		          << " p95_ms=" << times[121] << " max_ms=" << times.back()
		          << " entries=" << sprites->getCompositeEntries() << std::endl;
		delete original;
		GAGCore::Toolkit::close();
		return 0;
	}
	for (int count : {10, 300})
		for (int step : {16, 30})
		{
			for (int t = 0; t < 128; ++t)
			{
				render(1, step, t, count);
				render(2, step, t, count);
			}
			const auto warmMisses = sprites->getCompositeMisses();
			std::vector<double> results[3];
			for (int round = 0; round < 6; ++round)
				for (int order = 0; order < 3; ++order)
				{
					int blur = (order + round) % 3;
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
			std::cout << (gpu ? "GPU" : "software") << " units=" << count << " step=" << step
			          << " sharp_ms=" << (results[0][2] + results[0][3]) / 2
			          << " blur_ms=" << (results[1][2] + results[1][3]) / 2
			          << " cached_ms=" << (results[2][2] + results[2][3]) / 2
			          << " measured_misses=" << sprites->getCompositeMisses() - warmMisses
			          << " cache_bytes=" << sprites->getCompositeBytes()
			          << " hits=" << sprites->getCompositeHits()
			          << " misses=" << sprites->getCompositeMisses() << std::endl;
		}
	delete original;
	GAGCore::Toolkit::close();
}
