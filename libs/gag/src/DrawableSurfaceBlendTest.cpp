// SPDX-License-Identifier: GPL-3.0-or-later
// Software alpha-blend precision under many stacked draws onto the same
// pixels, as repeated translucent effects produce. A single draw's rounding
// bias is invisible; this catches it compounding into a visible darkening.
#include "Glob2Test.h"
#include <cmath>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SDL3/SDL.h>
#include <ui/Canvas.h>
#include <cstdlib>
#include <iostream>
using namespace GAGCore;
struct Inspect : GraphicContext
{
	Uint8 alphaAt(int x, int y)
	{
		SDL_LockSurface(sdlsurface);
		Uint32 pixel = static_cast<Uint32 *>(sdlsurface->pixels)[y * (sdlsurface->pitch >> 2) + x];
		Uint8 r, g, b, a;
		SDL_GetRGBA(pixel, SDL_GetPixelFormatDetails(sdlsurface->format), SDL_GetSurfacePalette(sdlsurface), &r, &g, &b, &a);
		SDL_UnlockSurface(sdlsurface);
		return r; // background and probe are greyscale; any channel matches
	}
};
namespace
{
void checkIconRendering(unsigned flags)
{
	glob2test::ToolkitScope toolkit;
	auto *gfx = Toolkit::initGraphic(64, 64, flags, "icon rendering");
	{
		using namespace GAGGUI::ui;
		Theme theme;
		auto presentation = Presentation::forSurface(64, 64);
		SurfaceCanvas canvas(*gfx, theme, presentation);
		IconAsset asset;
		asset.name = "probe";
		for (int pixels : {20, 40})
		{
			SDL_Surface *rawMask =
				SDL_CreateSurface(pixels, pixels, SDL_PIXELFORMAT_RGBA32);
			GLOB2_REQUIRE(rawMask, "icon raster and tint contract");
			SDL_FillSurfaceRect(rawMask, nullptr, SDL_MapSurfaceRGBA(rawMask, 255, 255, 255, 128));
			auto raster = std::make_shared<DrawableSurface>(rawMask);
			SDL_DestroySurface(rawMask);
			asset.rasters.push_back({pixels, raster});
		}
		const Color tint(30, 60, 90, 128);
		canvas.drawIcon({0, 0, 24, 24}, asset, tint);
		GLOB2_REQUIRE(asset.colours.size() == 1 && asset.colours.begin()->first.first == 40,
					  "icon raster and tint contract");
		auto cached = asset.colours.begin()->second;
		SDL_Surface *surface = cached->getSDLSurface();
		SDL_LockSurface(surface);
		Uint8 r, g, b, a;
		SDL_GetRGBA(*static_cast<Uint32 *>(surface->pixels), SDL_GetPixelFormatDetails(surface->format), SDL_GetSurfacePalette(surface), &r, &g, &b, &a);
		SDL_UnlockSurface(surface);
		GLOB2_REQUIRE(r == 30 && g == 60 && b == 90 && a == 64, "icon raster and tint contract");
		canvas.drawIcon({24, 0, 24, 24}, asset, tint);
		GLOB2_REQUIRE(asset.colours.begin()->second == cached, "icon raster and tint contract");
		canvas.drawIcon({0, 24, 20, 20}, asset, tint);
		const bool needsLargeRaster = gfx->getRasterScale() > 1;
		GLOB2_REQUIRE(asset.colours.size() == (needsLargeRaster ? 1u : 2u),
					  "icon raster and tint contract");
		GLOB2_REQUIRE(asset.colours.begin()->first.first == (needsLargeRaster ? 40 : 20),
					  "icon raster and tint contract");
		for (int i = 0; i < 100; ++i)
			canvas.drawIcon({0, 0, 24, 24}, asset, Color(i, 0, 0));
		GLOB2_REQUIRE(asset.colours.size() <= 64, "icon raster and tint contract");
	}
}
} // namespace
TEST_SUITE("DrawableSurfaceBlend")
{
TEST_CASE("software alpha blend stays accurate under stacked draws")
{
	glob2test::ToolkitScope toolkit;
	auto *raw = Toolkit::initGraphic(64, 64, 0 /* software */, "blend precision");
	auto *gfx = static_cast<Inspect *>(raw);
	const Uint8 background = 200;
	gfx->drawFilledRect(0, 0, 64, 64, background, background, background);

	// A surface that is fully transparent everywhere: drawing it must never
	// change the destination, no matter how many times or at what alpha.
	DrawableSurface transparent(32, 32);
	transparent.setClipRect();
	transparent.drawFilledRect(0, 0, 32, 32, 0, 0, 0, 0);

	for (int pass = 0; pass < 60; ++pass)
		gfx->drawSurface(16, 16, &transparent, static_cast<Uint8>(80 + (pass % 5) * 30));

	const Uint8 after = gfx->alphaAt(20, 20);
	std::cout << "background=" << (int)background << " after 60 blended transparent draws=" << (int)after << std::endl;
	REQUIRE(after == background);

	// A half-opaque source drawn many times should converge to a stable
	// value (a proper running blend), not drift away with each pass.
	DrawableSurface halfOpaque(32, 32);
	halfOpaque.setClipRect();
	halfOpaque.drawFilledRect(0, 0, 32, 32, 0, 0, 0, 128);
	for (int pass = 0; pass < 60; ++pass)
		gfx->drawSurface(16, 16, &halfOpaque, 255);
	const Uint8 stable = gfx->alphaAt(20, 20);
	for (int pass = 0; pass < 5; ++pass)
		gfx->drawSurface(16, 16, &halfOpaque, 255);
	const Uint8 restable = gfx->alphaAt(20, 20);
	std::cout << "converged=" << (int)stable << " after 5 more passes=" << (int)restable << std::endl;
	REQUIRE(std::abs(stable - restable) <= 1);

}
TEST_CASE("icon masks preserve resolution alpha and bounded recolours")
{
	checkIconRendering(0);
}
TEST_CASE("portable icon masks preserve resolution alpha and bounded recolours [display]")
{
	checkIconRendering(GraphicContext::PORTABLEGPU);
}
}
