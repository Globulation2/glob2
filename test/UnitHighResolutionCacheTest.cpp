// SPDX-License-Identifier: GPL-3.0-or-later
// HD/native layer mapping, whole-block resolution fallback, and zoom for
// drawSprite's direct per-pose drawing. See UnitTeamShaderTest.cpp for
// shader-vs-CPU pixel comparisons and UnitTeamColorCacheTest.cpp for the
// bounded CPU cache itself.
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SDL_image.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "render/UnitAnimation.h"

using namespace GAGCore;
struct InspectUnitSprite : Sprite
{
	void checkLayers(bool high) const
	{
		assert(images.size() == 1792 && rotated.size() == 1792);
		for (size_t i = 0; i < images.size(); ++i)
		{
			assert(bool(experimentImages[i]) == (high && bool(images[i])));
			assert(bool(experimentRotated[i]) == (high && bool(rotated[i])));
			if (high)
			{
				// Every unit HD layer renders onto a fixed pixel canvas
				// regardless of this frame's own native size (32, 38 or 40).
				if (images[i])
				{
					assert(experimentImages[i]->getW() == Sprite::highResolutionTextureSize);
					assert(experimentImages[i]->getH() == Sprite::highResolutionTextureSize);
				}
				if (rotated[i])
				{
					assert(experimentRotated[i]->orig->getW() == Sprite::highResolutionTextureSize);
					assert(experimentRotated[i]->orig->getH() == Sprite::highResolutionTextureSize);
				}
			}
		}
	}
	// Test-only: simulate a corrupted HD install by dropping one frame's HD
	// team layer, without touching the checked-in production pack. Production
	// code only ever changes these arrays through load()/reloadHighResolution(),
	// both of which recompute the per-block HD-completeness cache themselves;
	// poking the arrays directly here has to do the same.
	void dropExperimentRotated(int index)
	{
		delete experimentRotated[index];
		experimentRotated[index] = nullptr;
		recomputeBlockCompleteHD();
	}
	// Test-only: simulate a frame with no team layer at all (as ordinary,
	// non-team-colored sprites already have for every frame).
	void dropRotated(int index)
	{
		delete rotated[index];
		rotated[index] = nullptr;
		recomputeBlockCompleteHD();
	}
};

int main(int argc, char **argv)
{
	const bool software = argc > 1 && std::string(argv[1]) == "software";
	Toolkit::init("glob2-unit-hd-cache-test");
	auto gfx = Toolkit::initGraphic(640, 480, software ? 0 : GraphicContext::USEGPU, "HD unit cache checks");
	Sprite::setHighResolution(true);
	{
		InspectUnitSprite sprite;
		assert(sprite.load("data/gfx/unit"));
		sprite.checkLayers(!software);
		assert(sprite.getTeamColorCacheEntries() == 0);

		// Whole-block native fallback: index 256 and 257 share a 32-phase
		// action/direction block (see UnitAnimation.h); dropping 257's HD team
		// layer must make the whole block ineligible for HD, not just 257.
		if (!software)
		{
			assert(sprite.blockHasCompleteHD(256) && sprite.blockHasCompleteHD(257));
			sprite.dropExperimentRotated(257);
			assert(!sprite.blockHasCompleteHD(256));
			assert(!sprite.blockHasCompleteHD(257));
			sprite.setBaseColor(Color(255, 60, 40));
			gfx->drawFilledRect(0, 0, 640, 480, 10, 10, 10);
			gfx->drawSprite(0, 0, &sprite, 256); // still has its own HD team layer
			gfx->drawSprite(40, 0, &sprite, 257); // HD team layer missing; native still intact
			assert(glGetError() == GL_NO_ERROR);
			// Neither draw grew the CPU cache: the shader drew both at native
			// resolution (256 forced there by its corrupted block-mate).
			assert(sprite.getTeamColorCacheEntries() == 0);

			// A frame with no team layer at all draws base-only, no GL error.
			sprite.dropRotated(258);
			gfx->drawSprite(80, 0, &sprite, 258);
			assert(glGetError() == GL_NO_ERROR);
			assert(sprite.getTeamColorCacheEntries() == 0);
		}

		// All actions/directions/colors at logical size, HD active throughout.
		GLint viewport[4] = {0, 0, 640, 480};
		if (!software)
			glGetIntegerv(GL_VIEWPORT, viewport);
		for (int base = 0; base <= 384; base += 64)
			for (int dir = 0; dir < 8; ++dir)
				for (int color = 0; color < 3; ++color)
				{
					sprite.setBaseColor(color == 0   ? Color(255, 60, 40)
					                    : color == 1 ? Color(0, 255, 128)
					                                 : Color(70, 110, 255));
					const int index = unitAnimationFrame(base, dir, 96);
					gfx->drawSprite(0, 0, &sprite, index);
				}
		if (!software)
			assert(glGetError() == GL_NO_ERROR);

		// Disabling HD drops back to native logical dimensions (unchanged) and
		// clears the bounded cache; re-enabling restores HD layer mapping.
		Sprite::setHighResolution(false);
		sprite.checkLayers(false);
		assert(sprite.getTeamColorCacheEntries() == 0);
		Sprite::setHighResolution(true);
		{
			InspectUnitSprite fresh;
			assert(fresh.load("data/gfx/unit"));
			fresh.checkLayers(!software);
		}

		if (!software)
		{
			for (double zoom : {.5, 1., 2., 3.})
			{
				gfx->beginMapTransform(zoom, 0, 0, 0, 0, 640, 480);
				gfx->drawSprite(10, 10, &sprite, 256);
				gfx->endMapTransform();
				assert(glGetError() == GL_NO_ERROR);
			}

			// Pixel-alignment regression check for the sized drawSprite overload
			// (int/int/int/int): base and team layers must land in the exact same
			// destination box regardless of the HD texture's own pixel size. Cross
			// -check it against beginMapTransform's zoom at the same effective
			// scale, which goes through the already-verified shader path instead
			// (see UnitTeamShaderTest.cpp) -- a HD:native ratio bug in either
			// layer's scaling would misalign the two renderers' output.
			{
				const int index = unitAnimationFrame(0, 0, 96); // explorer: always has both layers
				sprite.setBaseColor(Color(255, 60, 40));
				const float zoom = 2.5f;
				const int scaledW = static_cast<int>(sprite.getW(index) * zoom);
				const int scaledH = static_cast<int>(sprite.getH(index) * zoom);

				gfx->drawFilledRect(0, 0, 640, 480, 5, 5, 5);
				gfx->drawSprite(50, 50, scaledW, scaledH, &sprite, index);
				glFinish();
				std::vector<Uint8> sized(640 * 480 * 4);
				glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, sized.data());

				gfx->drawFilledRect(0, 0, 640, 480, 5, 5, 5);
				gfx->beginMapTransform(zoom, 50, 50, 0, 0, 640, 480);
				gfx->drawSprite(0, 0, &sprite, index);
				gfx->endMapTransform();
				glFinish();
				std::vector<Uint8> zoomed(640 * 480 * 4);
				glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, zoomed.data());

				int maxDiff = 0;
				for (size_t i = 0; i < sized.size(); ++i)
					maxDiff = std::max(maxDiff, std::abs(static_cast<int>(sized[i]) - static_cast<int>(zoomed[i])));
				assert(glGetError() == GL_NO_ERROR);
				assert(maxDiff <= 3); // filtering/rounding only, not an exact-pixel match
				std::cout << "Sized-overload vs. zoomed-shader alignment: max diff=" << maxDiff << "/255" << std::endl;
			}
		}
		std::cout << "PASS all 1792 layer mappings, whole-block HD/native fallback, HD/classic "
		             "switching, zoom, and no CPU team-color cache growth under the shader"
		          << std::endl;
	}
	Toolkit::close();
}
