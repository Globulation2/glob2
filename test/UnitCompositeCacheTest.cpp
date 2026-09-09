// SPDX-License-Identifier: GPL-3.0-or-later
#include <Toolkit.h>
#include <GraphicContext.h>
#include <cassert>
#include <iostream>
#include <cstring>
#include <cmath>
#include "render/UnitAnimation.h"
struct InspectSprite : GAGCore::Sprite
{
	size_t colors()
	{
		size_t n = 0;
		for (auto r : rotated)
			if (r)
				n += r->rotationMap.size();
		return n;
	}
};
int main()
{
	using namespace GAGCore;
	Toolkit::init("codex-glob2-cache-test");
	Toolkit::initGraphic(100, 100, 0, "Cache validation");
	{
		InspectSprite sprite;
		assert(sprite.load("data/gfx/unit"));
		double total = 0;
		size_t channels = 0;
		int largest = 0;
		for (int base = 0; base <= 384; base += 64)
			for (int dir = 0; dir < 8; ++dir)
				for (int phase : {0, 7, 64, 255})
					for (int color = 0; color < 3; ++color)
					{
						sprite.setBaseColor(color == 0   ? Color(255, 60, 40)
						                    : color == 1 ? Color(0, 255, 128)
						                                 : Color(70, 110, 255));
						std::vector<std::pair<int, int>> frames;
						drawUnitMotionBlur(base, dir, phase, 30,
						                   [&](int f, int a) { frames.emplace_back(f, a); });
						auto *cached = sprite.getCachedComposite(frames);
						auto misses = sprite.getCompositeMisses();
						assert(sprite.getCachedComposite(frames) == cached);
						assert(sprite.getCompositeMisses() == misses);
						int w = cached->getW(), h = cached->getH();
						DrawableSurface reference(w, h), actual(w, h);
						reference.drawFilledRect(0, 0, w, h, 30, 90, 45);
						actual.drawFilledRect(0, 0, w, h, 30, 90, 45);
						for (auto f : frames)
							reference.drawSprite(0, 0, &sprite, f.first, f.second);
						actual.drawSurface(0, 0, cached);
						auto a = actual.getSDLSurface(), b = reference.getSDLSurface();
						for (int y = 0; y < h; ++y)
							for (int x = 0; x < w; ++x)
							{
								Uint8 ar, ag, ab, aa, br, bg, bb, ba;
								SDL_GetRGBA(((Uint32 *)((Uint8 *)a->pixels + y * a->pitch))[x],
								            a->format, &ar, &ag, &ab, &aa);
								SDL_GetRGBA(((Uint32 *)((Uint8 *)b->pixels + y * b->pitch))[x],
								            b->format, &br, &bg, &bb, &ba);
								for (int d : {abs(ar - br), abs(ag - bg), abs(ab - bb)})
								{
									total += d;
									++channels;
									largest = std::max(largest, d);
								}
							}
						assert(sprite.colors() == 0);
					}
		std::cout << "RGB difference from legacy software blits: mean=" << total / channels
		          << " max=" << largest << std::endl;
		assert(largest <= 12);
		// Exceed the former 64 MiB cap; the first entry must remain cached.
		for (int i = 0; i < 12500; ++i)
		{
			sprite.setBaseColor(i & 255, (i >> 8) & 255, 99);
			sprite.getCachedComposite({{256, 255}, {257, 127}});
		}
		auto misses = sprite.getCompositeMisses();
		sprite.setBaseColor(0, 0, 99);
		sprite.getCachedComposite({{256, 255}, {257, 127}});
		assert(sprite.getCompositeMisses() == misses);
		assert(sprite.getCompositeBytes() > 64 * 1024 * 1024);
		assert(sprite.colors() == 0);
		std::cout << "PASS hit reuse, team colors, all animations/directions, no intermediate "
		             "cache, unbounded retention. bytes="
		          << sprite.getCompositeBytes() << " entries=" << sprite.getCompositeEntries()
		          << std::endl;
	}
	Toolkit::close();
}
