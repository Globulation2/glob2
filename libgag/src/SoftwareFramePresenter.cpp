// SPDX-License-Identifier: GPL-3.0-or-later
#include <SoftwareFramePresenter.h>
#include <stdexcept>

namespace GAGCore
{
SoftwareFramePresenter::SoftwareFramePresenter(SDL_Surface *initial, AllocateSurface allocate)
{
	retained.reset(allocate(initial->w, initial->h, initial->format));
	if (!retained)
		throw std::runtime_error(SDL_GetError());
	SDL_SetSurfaceBlendMode(retained.get(), SDL_BLENDMODE_NONE);
	drawing.reset(initial);
	SDL_SetSurfaceBlendMode(drawing.get(), SDL_BLENDMODE_NONE);
}
SDL_Surface *SoftwareFramePresenter::begin(bool preserve)
{
	if (!pending)
		return drawing.get();
	// Copy while the completed source is still unclipped. The destination's
	// drawing clip is installed by GraphicContext after the target is rebound.
	SDL_SetSurfaceClipRect(retained.get(), nullptr);
	if (preserve && !SDL_BlitSurface(drawing.get(), nullptr, retained.get(), nullptr))
		throw std::runtime_error(SDL_GetError());
	drawing.swap(retained);
	pending = false;
	return drawing.get();
}
} // namespace GAGCore
