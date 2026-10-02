// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>

namespace GAGCore::SurfaceRaster
{
// Native modulation retains the facade's historical arithmetic. Surface uses
// SDL-style blending; Triangle preserves the old transformed triangle blitter's
// separate source/destination rounding, without its geometry overhead.
enum class BlitBlend
{
	Native,
	Surface,
	Triangle
};
enum class FillBlend
{
	Native,
	SourceOver
};
// Raster operations borrow their surfaces and restore source SDL modulation state.
// Scale sampling uses the complete destination rectangle; clipping is applied by
// the raster operation afterwards, so clipping cannot stretch the remaining source pixels.
void blit(SDL_Surface *target, SDL_Surface *source, const SDL_Rect &sourceRect,
		  SDL_Rect destination, Uint8 alpha, bool opaque, BlitBlend blend);
void fill(SDL_Surface *target, SDL_Rect rect, Uint32 packedColor, Uint8 alpha,
		  FillBlend blend = FillBlend::Native);
bool opaque(SDL_Surface *surface);
} // namespace GAGCore::SurfaceRaster
