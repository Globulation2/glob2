// SPDX-License-Identifier: GPL-3.0-or-later
// Run after installing the complete 32-pose HD unit pack; optional 'software' mode.
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SDL_image.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>
#include "render/UnitAnimation.h"

using namespace GAGCore;
struct InspectUnitSprite : Sprite
{
	size_t coloredEntries() const
	{
		size_t count = 0;
		for (auto layers : {&rotated, &experimentRotated})
			for (auto layer : *layers)
				if (layer)
					count += layer->rotationMap.size();
		return count;
	}
	void checkLayers(bool high) const
	{
		assert(images.size() == 1792 && rotated.size() == 1792);
		for (size_t i = 0; i < images.size(); ++i)
		{
			assert(bool(experimentImages[i]) == (high && bool(images[i])));
			assert(bool(experimentRotated[i]) == (high && bool(rotated[i])));
			if (high)
			{
				if (images[i])
					assert(experimentImages[i]->getW() == images[i]->getW() * 4);
				if (rotated[i])
					assert(experimentRotated[i]->orig->getW() == rotated[i]->orig->getW() * 4);
			}
		}
	}
};

int main(int argc, char **argv)
{
	const bool software = argc > 1 && std::string(argv[1]) == "software";
	Toolkit::init("glob2-unit-hd-cache-test");
	auto gfx = Toolkit::initGraphic(640, 480, software ? 0 : GraphicContext::USEGPU,
	                                "HD unit cache checks");
	Sprite::setHighResolution(true);
	if (argc > 1 && std::string(argv[1]) == "partial")
	{
		{
			InspectUnitSprite sprite;
			assert(sprite.load("data/gfx/unit"));
			assert(sprite.getCachedComposite({{256, 255}})->getW() == 152);
			assert(sprite.getCachedComposite({{256, 255}, {257, 127}})->getW() == 38);
			assert(sprite.coloredEntries() == 0);
		}
		Toolkit::close();
		std::cout << "PASS partial HD shutter falls back as a whole; complete HD frame stays sharp"
		          << std::endl;
		return 0;
	}
	{
		InspectUnitSprite sprite;
		assert(sprite.load("data/gfx/unit"));
		sprite.checkLayers(!software);
		// A spent budget defers misses, but never hides an existing composite.
		Sprite::beginCompositeFrame(0);
		assert(sprite.getCachedComposite({{256,255}}, true) == nullptr);
		const auto entries = sprite.getCompositeEntries();
		sprite.drawCachedComposite(gfx, 0, 0, 256, {{256,255}});
		assert(sprite.getCompositeEntries() == entries && sprite.coloredEntries() == 0);
		Sprite::beginCompositeFrame();
		auto ready = sprite.getCachedComposite({{256,255}}, true);
		assert(ready);
		Sprite::beginCompositeFrame(0);
		assert(sprite.getCachedComposite({{256,255}}, true) == ready);
		assert(sprite.getCachedComposite({{257,255}}, true) == nullptr);
		gfx->nextFrame(); // Presentation replenishes the budget on either backend.
		assert(sprite.getCachedComposite({{257,255}}, true));

		GLint viewport[4] = {0, 0, 640, 480};
		if (!software)
			glGetIntegerv(GL_VIEWPORT, viewport);
		const float pixelScale = float(viewport[2]) / gfx->getW();
		std::vector<Uint8> before(viewport[2] * viewport[3] * 4), after(before.size());
		int maximumError = 0;
		// All actions/directions/colors. Compare at one texture pixel per screen pixel
		// so this measures compositing, not differences in mip/filter order.
		for (int base = 0; base <= 384; base += 64)
			for (int dir = 0; dir < 8; ++dir)
				for (int color = 0; color < 3; ++color)
				{
					sprite.setBaseColor(color == 0   ? Color(255, 60, 40)
					                    : color == 1 ? Color(0, 255, 128)
					                                 : Color(70, 110, 255));
					std::vector<std::pair<int, int>> frames;
					drawUnitMotionBlur(base, dir, 7, 30, [&](int frame, int alpha)
					{ frames.emplace_back(frame, alpha); });
					const int logical = sprite.getW(frames.front().first);
					const float displaySize = logical * (software ? 1 : 4) / pixelScale;
					if (!software)
					{
						gfx->drawFilledRect(0, 0, 640, 480, 30, 90, 45);
						for (auto frame : frames)
							gfx->drawSprite(0.f, 0.f, displaySize, displaySize, &sprite,
							                frame.first, frame.second);
						glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGBA,
						             GL_UNSIGNED_BYTE, before.data());
					}
					auto result = sprite.getCachedComposite(frames);
					assert(result->getW() == logical * (software ? 1 : 4));
					assert(result->getH() == logical * (software ? 1 : 4));
					const auto misses = sprite.getCompositeMisses();
					assert(sprite.getCachedComposite(frames) == result &&
					       sprite.getCompositeMisses() == misses);
					assert(sprite.coloredEntries() == 0);
					if (!software)
					{
						gfx->drawFilledRect(0, 0, 640, 480, 30, 90, 45);
						gfx->drawSurface(0.f, 0.f, displaySize, displaySize, result);
						glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGBA,
						             GL_UNSIGNED_BYTE, after.data());
						for (size_t i = 0; i < before.size(); ++i)
							if (i % 4 != 3)
								maximumError = std::max(maximumError,
								                        std::abs(int(before[i]) - int(after[i])));
						assert(glGetError() == GL_NO_ERROR);
					}
				}
		std::cout << "Composite comparison maximum RGB error=" << maximumError
		          << "/255, cache entries=" << sprite.getCompositeEntries()
		          << " estimated bytes=" << sprite.getCompositeBytes() << std::endl;
		assert(maximumError <= 5);
		// Changing artwork invalidates composites; blur on/off itself does not.
		Sprite::setHighResolution(false);
		assert(sprite.getCompositeEntries() == 0 && sprite.getCompositeBytes() == 0);
		sprite.checkLayers(false);
		assert(sprite.getCachedComposite({{256, 255}})->getW() == 38);
		Sprite::setHighResolution(true);
		assert(sprite.getCompositeEntries() == 0);
		assert(sprite.getCachedComposite({{256, 255}})->getW() == (software ? 38 : 152));
		if (!software)
		{
			for (double zoom : {.5, 1., 2., 3.})
			{
				gfx->beginMapTransform(zoom, 0, 0, 0, 0, 640, 480);
				gfx->drawSurface(10, 10, 38, 38, sprite.getCachedComposite({{256, 255}}));
				gfx->drawSprite(64, 10, &sprite, 256);
				gfx->endMapTransform();
				assert(glGetError() == GL_NO_ERROR);
			}
		}
		std::cout << "PASS all 1792 layer mappings, 7 actions/8 directions/3 colors, cache reuse, "
		             "HD/classic switching, logical dimensions and zoom"
		          << std::endl;
	}
	Toolkit::close();
}
