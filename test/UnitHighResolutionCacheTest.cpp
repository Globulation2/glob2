// SPDX-License-Identifier: GPL-3.0-or-later
// HD/native layer mapping, whole-block resolution fallback, and zoom, for the
// direct-draw replacement of the removed composite cache. See
// UnitTeamShaderTest.cpp for shader-vs-CPU pixel comparisons and
// UnitTeamColorCacheTest.cpp for the bounded CPU cache itself.
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SDL_image.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <cassert>
#include <iostream>
#include <string>
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
				if (images[i])
					assert(experimentImages[i]->getW() == images[i]->getW() * 4);
				if (rotated[i])
					assert(experimentRotated[i]->orig->getW() == rotated[i]->orig->getW() * 4);
			}
		}
	}
	// Test-only: simulate a corrupted HD install by dropping one frame's HD
	// team layer, without touching the checked-in production pack.
	void dropExperimentRotated(int index)
	{
		delete experimentRotated[index];
		experimentRotated[index] = nullptr;
	}
	// Test-only: simulate a frame with no team layer at all (as ordinary,
	// non-team-colored sprites already have for every frame).
	void dropRotated(int index)
	{
		delete rotated[index];
		rotated[index] = nullptr;
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
		}
		std::cout << "PASS all 1792 layer mappings, whole-block HD/native fallback, HD/classic "
		             "switching, zoom, and no CPU team-color cache growth under the shader"
		          << std::endl;
	}
	Toolkit::close();
}
