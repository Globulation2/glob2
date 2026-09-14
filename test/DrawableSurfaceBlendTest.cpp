// SPDX-License-Identifier: GPL-3.0-or-later
// Software alpha-blend precision under many stacked draws onto the same
// pixels, as a wide motion-blur shutter produces. A single draw's rounding
// bias is invisible; this catches it compounding into a visible darkening.
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SDL.h>
#include <cassert>
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
		SDL_GetRGBA(pixel, sdlsurface->format, &r, &g, &b, &a);
		SDL_UnlockSurface(sdlsurface);
		return r; // background and probe are greyscale; any channel matches
	}
};
int main()
{
	Toolkit::init("drawable-surface-blend-test");
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
	assert(after == background);

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
	assert(std::abs(stable - restable) <= 1);

	Toolkit::close();
	std::cout << "PASS: software alpha blend stays accurate under many stacked draws\n";
}
