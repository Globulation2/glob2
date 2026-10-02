// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <memory>

namespace GAGCore
{
// Owns two CPU buffers. Exposure always reads the completed buffer; drawing
// starts on the other buffer. Neither the window nor a backend owns these pixels.
class SoftwareFramePresenter
{
	using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
	Surface drawing{nullptr, SDL_DestroySurface}, retained{nullptr, SDL_DestroySurface};
	bool pending = false, valid = false;

  public:
	// Allocates the spare before taking ownership, so failure leaves the caller's
	// current framebuffer and the legacy copy-based presenter intact.
	using AllocateSurface = decltype(&SDL_CreateSurface);
	// Allocation seam makes failure ownership testable without exhausting memory.
	explicit SoftwareFramePresenter(SDL_Surface *initial,
									AllocateSurface allocate = SDL_CreateSurface);
	SDL_Surface *begin(bool preserve);
	bool needsBegin() const { return pending; }
	SDL_Surface *takeCompleted()
	{
		return valid ? (pending ? drawing.release() : retained.release()) : nullptr;
	}
	void complete()
	{
		valid = true;
		pending = true;
	}
	SDL_Surface *completed() const
	{
		return valid ? (pending ? drawing.get() : retained.get()) : nullptr;
	}
};
} // namespace GAGCore
