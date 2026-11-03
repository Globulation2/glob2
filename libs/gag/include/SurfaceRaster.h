// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <span>
#include <cstddef>

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
	SourceOver,
	// One /255 rounding of the combined RGB terms; alpha is source-over.
	PortableSourceOver
};
// Owner-resolved views. No surface, lock, modulation or backend state reaches
// the regional kernels. Owners retain pixels until all regions have completed.
struct ReadOnlyPixelView
{
    const Uint8 *pixels = nullptr;
    int width = 0, height = 0, pitch = 0;
    SDL_PixelFormatDetails format{};
};
struct WritablePixelView
{
    Uint8 *pixels = nullptr;
    int width = 0, height = 0, pitch = 0;
    SDL_PixelFormatDetails format{};
};
struct RasterCommand
{
    enum class Kind
    {
        Fill,
        Blit
    } kind = Kind::Fill;
    // Nearest uses rational pixel centers; SDLNearest preserves the historical
    // SDL quad path's truncated 16.16 step. Both refer to the full destination.
    enum class Sampling { Nearest, SDLNearest, LinearPremultiplied };
    enum class Blending { Triangle, Portable };
    ReadOnlyPixelView source;
    SDL_Rect sourceRect{}, destination{}, clip{};
    Uint32 color = 0;
    Uint8 opacity = 255;
    bool opaque = false;
    // SDL static ARGB copies historically force destination alpha on coverage.
    bool forceOpaqueAlpha = false;
    FillBlend fillBlend = FillBlend::SourceOver;
    Blending blend = Blending::Triangle;
    Sampling sampling = Sampling::Nearest;
    // Linear sampling preserves fractional pixel-center geometry. destination
    // remains its integer coverage bounds for clipping and command binning.
    SDL_FRect destinationFloat{};
    Uint8 red = 255, green = 255, blue = 255;
};
enum class KernelMode { Automatic, Scalar };
// Portable commands require packed 32-bit views with eight-bit RGB and optional
// eight-bit alpha. Sources may differ from the destination channel layout.
// Half-open regions partition writes. Source sampling uses the full captured
// destination even when a tile or captured clip cuts through a sprite.
void executeRegion(std::span<const RasterCommand>, WritablePixelView, SDL_Rect region,
                   KernelMode mode = KernelMode::Automatic);
void executeCommand(const RasterCommand &, WritablePixelView, SDL_Rect region,
                    KernelMode mode = KernelMode::Automatic);
// Raster operations borrow their surfaces and restore source SDL modulation state.
// Scale sampling uses the complete destination rectangle; clipping is applied by
// the raster operation afterwards, so clipping cannot stretch the remaining source pixels.
void blit(SDL_Surface *target, SDL_Surface *source, const SDL_Rect &sourceRect,
		  SDL_Rect destination, Uint8 alpha, bool opaque, BlitBlend blend);
void skinBlit(SDL_Surface *target,SDL_Surface *source,const SDL_Rect &src,SDL_Rect dst,Uint8 opacity);
void skinBlitFloat(SDL_Surface *target,SDL_Surface *source,const SDL_Rect &src,SDL_FRect dst,Uint8 opacity);
void fill(SDL_Surface *target, SDL_Rect rect, Uint32 packedColor, Uint8 alpha,
		  FillBlend blend = FillBlend::Native);
bool opaque(SDL_Surface *surface);
} // namespace GAGCore::SurfaceRaster
