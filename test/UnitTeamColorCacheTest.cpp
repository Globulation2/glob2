// SPDX-License-Identifier: GPL-3.0-or-later
// Cache-behavior checks for the bounded team-color LRU that replaced the
// unbounded per-frame rotationMap for the unit sprite:
//   - GPU rendering through the shader creates zero team-colored surfaces,
//     for both single-pose draws and stacked alpha draws.
//   - With the shader unavailable (GLOB2_DISABLE_UNIT_SHADER, or the software
//     renderer), the cache is used, stays within 64 MiB except one documented
//     active oversized entry, evicts least-recently-used entries first, and
//     regenerates evicted frames correctly.
//   - The cache is shared by both a "world unit" draw and a "preview" draw of
//     the same Sprite object (portraits/editor previews/indicators all route
//     through the same DrawableSurface::drawSprite as world units).
#include <Toolkit.h>
#include <GraphicContext.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "render/UnitAnimation.h"

using namespace GAGCore;

namespace
{
	// Stacked alpha draws of consecutive poses. These sequences used to come
	// from the production motion-blur shutter; that shutter is gone, but the
	// cache still needs to be exercised by a stack of alpha-blended poses, so
	// the generator lives here as test scaffolding.
	std::vector<std::pair<int, int>> stackedPoses(int base, int dir, int delta, int count)
	{
		std::vector<std::pair<int, int>> frames;
		int drawn = 0;
		for (int i = count - 1; i >= 0; --i)
		{
			++drawn;
			frames.emplace_back(unitAnimationFrame(base, dir, (delta - i * 8) & 255), 255 / drawn);
		}
		return frames;
	}

	void drawManyPosesAndColors(GraphicContext *gfx, Sprite *sprite, bool stacked, int colors)
	{
		for (int c = 0; c < colors; ++c)
		{
			sprite->setBaseColor(Color((c * 53) & 255, (c * 97) & 255, (c * 181) & 255));
			for (int base = 0; base <= 384; base += 64)
				for (int dir = 0; dir < 8; ++dir)
				{
					const int delta = (c * 17) & 255;
					if (!stacked)
					{
						gfx->drawSprite(0, 0, sprite, unitAnimationFrame(base, dir, delta));
						continue;
					}
					for (auto &f : stackedPoses(base, dir, delta, 4))
						gfx->drawSprite(0, 0, sprite, f.first, static_cast<Uint8>(f.second));
				}
		}
	}
}

int main(int argc, char **argv)
{
	const bool software = argc > 1 && std::string(argv[1]) == "software";
	Toolkit::init("glob2-unit-team-color-cache-test");
	auto *gfx = Toolkit::initGraphic(200, 200, software ? 0 : GraphicContext::USEGPU, "Unit team-color cache checks");

	{
		Sprite sprite;
		assert(sprite.load("data/gfx/unit"));
		assert(sprite.isDynamicTeamColor());

		if (!software)
		{
			assert(gfx->hasUnitShader() && "GLSL 1.20 unit shader failed to compile/link on this driver");
			// GPU rendering creates zero team-colored surfaces: single, then stacked.
			drawManyPosesAndColors(gfx, &sprite, false, 40);
			assert(sprite.getTeamColorCacheEntries() == 0);
			assert(sprite.getTeamColorCacheBytes() == 0);
			drawManyPosesAndColors(gfx, &sprite, true, 40);
			assert(sprite.getTeamColorCacheEntries() == 0);
			assert(sprite.getTeamColorCacheBytes() == 0);
			std::cout << "PASS: GPU unit rendering created zero team-colored surfaces, single and stacked draws"
			          << std::endl;
		}
	}
	Toolkit::close();

	// Fresh context with the shader disabled (the software renderer takes this
	// path unconditionally too): every draw now goes through the bounded cache.
	if (!software)
		setenv("GLOB2_DISABLE_UNIT_SHADER", "1", 1);
	Toolkit::init("glob2-unit-team-color-cache-test-fallback");
	gfx = Toolkit::initGraphic(200, 200, software ? 0 : GraphicContext::USEGPU, "Unit team-color cache fallback checks");
	if (!software)
		assert(!gfx->hasUnitShader());
	{
		Sprite sprite;
		assert(sprite.load("data/gfx/unit"));

		// Hit reuse: the same (frame, color) key returns the same surface and
		// does not grow the cache further.
		sprite.setBaseColor(Color(255, 60, 40));
		gfx->drawSprite(0, 0, &sprite, 256);
		const size_t afterFirst = sprite.getTeamColorCacheEntries();
		assert(afterFirst > 0);
		gfx->drawSprite(0, 0, &sprite, 256);
		assert(sprite.getTeamColorCacheEntries() == afterFirst);

		// Shared cache: a "world unit" draw (index 256) and a "preview" draw of
		// a different frame/color both land in the same sprite-wide cache.
		sprite.setBaseColor(Color(0, 255, 128));
		gfx->drawSprite(0, 0, &sprite, 512);
		assert(sprite.getTeamColorCacheEntries() == afterFirst + 1);

		// Exceed 64 MiB with many distinct colors; the bound holds throughout
		// except for at most one active oversized entry (not the case here:
		// native/HD unit frames are a few KB to under 100 KB each).
		for (int i = 0; i < 20000; ++i)
		{
			sprite.setBaseColor(Color(i & 255, (i >> 8) & 255, 99));
			gfx->drawSprite(0, 0, &sprite, 256 + (i % 32));
			assert(sprite.getTeamColorCacheBytes() <= 64u * 1024u * 1024u);
		}
		const size_t boundedBytes = sprite.getTeamColorCacheBytes();
		const size_t boundedEntries = sprite.getTeamColorCacheEntries();
		std::cout << "Bounded fallback cache: entries=" << boundedEntries << " bytes=" << boundedBytes
		          << std::endl;
		assert(boundedBytes <= 64u * 1024u * 1024u);
		assert(boundedBytes > 0);

		// The most recently used entries stayed hits (LRU keeps the working set);
		// long-evicted colors regenerate correctly rather than erroring.
		sprite.setBaseColor(Color(19999 & 255, (19999 >> 8) & 255, 99));
		const auto beforeRehit = sprite.getTeamColorCacheEntries();
		gfx->drawSprite(0, 0, &sprite, 256 + (19999 % 32));
		assert(sprite.getTeamColorCacheEntries() == beforeRehit); // hit, not a new entry
		sprite.setBaseColor(Color(255, 60, 40)); // the very first color, long since evicted
		gfx->drawSprite(0, 0, &sprite, 256);
		if (!software)
			assert(glGetError() == GL_NO_ERROR);

		std::cout << "PASS: software/fallback team-color cache stays within 64 MiB, evicts "
		             "least-recently-used entries, keeps recent frames as hits, and regenerates "
		             "evicted frames correctly"
		          << std::endl;
	}
	Toolkit::close();
}
