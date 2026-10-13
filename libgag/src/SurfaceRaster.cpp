// SPDX-License-Identifier: GPL-3.0-or-later
#include <SurfaceRaster.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <memory>
#include <cstdint>
#include <limits>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace GAGCore::SurfaceRaster
{
namespace
{
void check(bool result)
{
	if (!result)
		throw std::runtime_error(SDL_GetError());
}
class SourceState
{
	SDL_Surface *surface;
	SDL_BlendMode blend;
	Uint8 alpha;
	bool changedAlpha = false, changedBlend = false;

  public:
	explicit SourceState(SDL_Surface *value) : surface(value)
	{
		check(SDL_GetSurfaceBlendMode(surface, &blend));
		check(SDL_GetSurfaceAlphaMod(surface, &alpha));
	}
	~SourceState()
	{
		if (changedAlpha)
			SDL_SetSurfaceAlphaMod(surface, alpha);
		if (changedBlend)
			SDL_SetSurfaceBlendMode(surface, blend);
	}
	void setAlpha(Uint8 value)
	{
		if (value != alpha)
		{
			check(SDL_SetSurfaceAlphaMod(surface, value));
			changedAlpha = true;
		}
	}
	void setBlend(SDL_BlendMode value)
	{
		if (value != blend)
		{
			check(SDL_SetSurfaceBlendMode(surface, value));
			changedBlend = true;
		}
	}
	Uint8 modulation() const { return alpha; }
	SDL_BlendMode mode() const { return blend; }
};
// Verified opaque, format-identical native pixels need no blend-state changes.
// Toggling SDL_BLENDMODE_NONE around every tile invalidates SDL's cached blit
// mapping; copying rows also avoids that hidden setup cost. Clip source first,
// move the destination by the same amount, then clip destination writes.
bool copyRows(SDL_Surface *target, SDL_Surface *source, SDL_Rect src, SDL_Rect dst,
			  bool ignoreColorMod)
{
	SDL_Rect targetClip;
	check(SDL_GetSurfaceClipRect(target, &targetClip));
	if (source == target || source->format != target->format ||
		SDL_GetPixelFormatDetails(source->format)->bytes_per_pixel != 4)
		return false;
	Uint8 red, green, blue;
	check(SDL_GetSurfaceColorMod(source, &red, &green, &blue));
	if (!ignoreColorMod && (red != 255 || green != 255 || blue != 255))
		return false;
	const SDL_Rect sourceBounds{0, 0, source->w, source->h};
	SDL_Rect sourceVisible, visible;
	if (!SDL_GetRectIntersection(&src, &sourceBounds, &sourceVisible))
		return true;
	dst.x += sourceVisible.x - src.x;
	dst.y += sourceVisible.y - src.y;
	dst.w = sourceVisible.w;
	dst.h = sourceVisible.h;
	if (!SDL_GetRectIntersection(&dst, &targetClip, &visible))
		return true;
	sourceVisible.x += visible.x - dst.x;
	sourceVisible.y += visible.y - dst.y;
	check(SDL_LockSurface(source));
	if (!SDL_LockSurface(target))
	{
		SDL_UnlockSurface(source);
		throw std::runtime_error(SDL_GetError());
	}

	for (int y = 0; y < visible.h; ++y)
	{
		auto *output =
			static_cast<Uint8 *>(target->pixels) + (visible.y + y) * target->pitch + visible.x * 4;
		const auto *input = static_cast<const Uint8 *>(source->pixels) +
							(sourceVisible.y + y) * source->pitch + sourceVisible.x * 4;
		// Fixed-size terrain copies inline into SIMD loads/stores, avoiding
		// a library call for each short row. Other widths keep libc's copy.
		if (visible.w == 32)
			std::memcpy(output, input, 128);
		else
			std::memcpy(output, input, visible.w * 4);
	}
	SDL_UnlockSurface(target);
	SDL_UnlockSurface(source);
	return true;
}
// SDL_BlitSurfaceScaled clips and then rescales the remaining source rectangle. At
// fractional sizes this changes sampling along a clipped edge. Map destination
// pixel centers into the ORIGINAL rectangle instead, and clip only the writes.
void nearest(SDL_Surface *target, SDL_Surface *source, const SDL_Rect &src, const SDL_Rect &dst,
			 Uint8 opacity, bool isOpaque, bool triangleBlend)
{
	SDL_Rect targetClip;
	check(SDL_GetSurfaceClipRect(target, &targetClip));
	SDL_Rect visible;
	if (!SDL_GetRectIntersection(&dst, &targetClip, &visible))
		return;
	if (SDL_GetPixelFormatDetails(target->format)->bytes_per_pixel != 4)
		throw std::invalid_argument("Nearest target must be 32-bit");
	const bool sameFormat = source->format == target->format;
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> converted(nullptr,
																		  SDL_DestroySurface);
	if (source == target || source->format != target->format)
	{
		converted.reset(SDL_ConvertSurface(source, target->format));
		if (!converted)
			throw std::runtime_error(SDL_GetError());
		source = converted.get();
	}
	Uint8 red = 255, green = 255, blue = 255;
	// SDL geometry uses vertex modulation; it ignores texture/surface RGB mods.
	if (!triangleBlend)
		check(SDL_GetSurfaceColorMod(source, &red, &green, &blue));
	Uint32 key = 0;
	const bool keyed = SDL_GetSurfaceColorKey(source, &key);
	const bool separateTerms =
		(triangleBlend && sameFormat) || opacity < 255 || red != 255 || green != 255 || blue != 255;
	const bool copy = isOpaque && opacity == 255 && red == 255 && green == 255 && blue == 255;
	const auto *format = SDL_GetPixelFormatDetails(source->format);
	const Uint32 alphaMask = format->Amask;
	const bool rleBlend = triangleBlend && sameFormat && opacity == 255 && src.w == dst.w &&
						  src.h == dst.h && alphaMask == 0xff000000;
	const auto divide255 = [](Uint32 value) { return (value + 1 + (value >> 8)) >> 8; };
	// At native size the original pixel-center mapping reduces exactly to an
	// offset. Keep scaled sampling unchanged, without allocating its lookup for
	// common alpha sprite copies.
	const bool nativeWidth = src.w == dst.w, nativeHeight = src.h == dst.h;
    if (triangleBlend && sameFormat && !keyed &&
        (source->format == SDL_PIXELFORMAT_ARGB8888 || source->format == SDL_PIXELFORMAT_RGBA32))
    {
        check(SDL_LockSurface(source));
        if (!SDL_LockSurface(target))
        {
            SDL_UnlockSurface(source);
            throw std::runtime_error(SDL_GetError());
        }
        RasterCommand command;
        command.kind = RasterCommand::Kind::Blit;
        command.source = {static_cast<const Uint8 *>(source->pixels), source->w, source->h,
                          source->pitch, *format};
        command.sourceRect = src;
        command.destination = dst;
        command.clip = targetClip;
        command.opacity = opacity;
        command.opaque = isOpaque;
        try
        {
            executeCommand(command,
                           {static_cast<Uint8 *>(target->pixels), target->w, target->h,
                            target->pitch, *format},
                           targetClip);
        }
        catch (...)
        {
            SDL_UnlockSurface(target);
            SDL_UnlockSurface(source);
            throw;
        }
        SDL_UnlockSurface(target);
        SDL_UnlockSurface(source);
        return;
    }
    std::vector<int> columns;
    if (!nativeWidth)
    {
        columns.resize(visible.w);
        for (int x = 0; x < visible.w; ++x)
            columns[x] = src.x + int(((std::int64_t(visible.x + x) - dst.x) * 2 + 1) * src.w /
                                     (std::int64_t(dst.w) * 2));
    }
    check(SDL_LockSurface(source));
	if (!SDL_LockSurface(target))
	{
		SDL_UnlockSurface(source);
		throw std::runtime_error(SDL_GetError());
	}
	for (int y = visible.y; y < visible.y + visible.h; ++y)
	{
		const int sy =
			nativeHeight ? src.y + (y - dst.y) :
			src.y + int(((std::int64_t(y) - dst.y) * 2 + 1) * src.h / (std::int64_t(dst.h) * 2));
		if (sy < 0 || sy >= source->h)
			continue;
		const auto *row = reinterpret_cast<const Uint32 *>(
			static_cast<const Uint8 *>(source->pixels) + sy * source->pitch);
		auto *output =
			reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(target->pixels) + y * target->pitch) +
			visible.x;
		for (int x = 0; x < visible.w; ++x)
		{
			const int sx = nativeWidth ? src.x + (visible.x - dst.x + x) : columns[x];
			if (sx < 0 || sx >= source->w)
				continue;
			Uint32 value = row[sx];
			if (keyed && (value & ~alphaMask) == (key & ~alphaMask))
				continue;
			if (copy)
			{
				output[x] = value;
				continue;
			}
			const Uint32 alpha =
				divide255((alphaMask ? (value & alphaMask) >> format->Ashift : 255) * opacity);
			if (!alpha)
				continue; // Transparent borders cannot darken the destination.
			if (red != 255 || green != 255 || blue != 255)
			{
				const Uint32 r = divide255(((value & format->Rmask) >> format->Rshift) * red);
				const Uint32 g = divide255(((value & format->Gmask) >> format->Gshift) * green);
				const Uint32 b = divide255(((value & format->Bmask) >> format->Bshift) * blue);
				value = (r << format->Rshift) | (g << format->Gshift) | (b << format->Bshift) |
						(value & alphaMask);
			}
			// A mixed-alpha chunk can contain large opaque interiors. Verification
			// at this pixel is enough to avoid reading or blending its destination.
			if (alpha == 255)
			{
				output[x] = value;
				continue;
			}
			const Uint32 destination = output[x], inverse = 255 - alpha;
			if (rleBlend)
			{
				// SDL3 static software textures use RLE for same-format,
				// unscaled ARGB blits, interpolating RGB with /256 arithmetic.
				const Uint32 rb = value & 0x00ff00ff, destinationRB = destination & 0x00ff00ff;
				const Uint32 g = value & 0x0000ff00, destinationG = destination & 0x0000ff00;
				output[x] = ((destinationRB + (((rb - destinationRB) * alpha) >> 8)) & 0x00ff00ff) |
							((destinationG + (((g - destinationG) * alpha) >> 8)) & 0x0000ff00) |
							alphaMask;
				continue;
			}
			// Two independent 16-bit lanes, with exact /255 rounding. Mask the
			// correction per lane to avoid carries leaking between components.
			const auto lane = [](Uint32 value)
			{ return ((value + 0x00010001 + ((value >> 8) & 0x00ff00ff)) >> 8) & 0x00ff00ff; };
			Uint32 rb, ga;
			if (separateTerms)
			{
				// SDL3 recognizes rectangular geometry as texture blits. Modulated
				// blits truncate source and destination terms independently.
				rb =
					lane((value & 0x00ff00ff) * alpha) + lane((destination & 0x00ff00ff) * inverse);
				ga = lane(((value >> 8) & 0x00ff00ff) * alpha) +
					 lane(((destination >> 8) & 0x00ff00ff) * inverse);
			}
			else
			{
				rb = lane((value & 0x00ff00ff) * alpha + (destination & 0x00ff00ff) * inverse);
				ga = lane(((value >> 8) & 0x00ff00ff) * alpha +
						  ((destination >> 8) & 0x00ff00ff) * inverse);
			}
			Uint32 blended = rb | (ga << 8);
			if (alphaMask)
				blended =
					(blended & ~alphaMask) |
					((alpha + divide255(((destination & alphaMask) >> format->Ashift) * inverse))
					 << format->Ashift);
			output[x] = blended;
		}
	}
	SDL_UnlockSurface(target);
	SDL_UnlockSurface(source);
}

// Native draw-opacity modulation historically blends all four stored lanes.
// Keep that arithmetic (including alpha) separate from SDL's source-over path:
// changing it would alter retained surfaces and native-scale output.
void nativeAlpha(SDL_Surface *target, SDL_Surface *source, SDL_Rect src, SDL_Rect dst, Uint8 alpha)
{
	SDL_Rect targetClip;
	check(SDL_GetSurfaceClipRect(target, &targetClip));
	const SDL_Rect clip = targetClip;
	if (dst.x < clip.x)
	{
		const int difference = clip.x - dst.x;
		src.x += difference;
		src.w -= difference;
		dst.x = clip.x;
	}
	// Retain the historical native vertical clipping convention. The transformed
	// rasterizer clips every edge; native compatibility includes this old rule.
	if (dst.y < 0)
	{
		const int difference = clip.y - dst.y;
		src.y += difference;
		src.h -= difference;
		dst.y = clip.y;
	}
	src.w = std::min(src.w, clip.x + clip.w - dst.x);
	src.h = std::min(src.h, clip.y + clip.h - dst.y);
	if (src.w <= 0 || src.h <= 0)
		return;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	constexpr Uint32 alphaShift = 0;
#else
	constexpr Uint32 alphaShift = 24;
#endif
	check(SDL_LockSurface(source));
	if (!SDL_LockSurface(target))
	{
		SDL_UnlockSurface(source);
		throw std::runtime_error(SDL_GetError());
	}
	for (int y = 0; y < src.h; ++y)
	{
		const auto *input =
			reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(source->pixels) +
											 (src.y + y) * source->pitch) +
			src.x;
		auto *output = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(target->pixels) +
												  (dst.y + y) * target->pitch) +
					   dst.x;
		for (int x = 0; x < src.w; ++x)
		{
			const Uint32 value = input[x];
			const Uint32 product = ((value >> alphaShift) & 255) * alpha;
			const Uint32 opacity = (product + 1 + (product >> 8)) >> 8, inverse = 255 - opacity;
			Uint32 rb = (value & 0x00ff00ff) * opacity + (output[x] & 0x00ff00ff) * inverse;
			Uint32 ga =
				((value >> 8) & 0x00ff00ff) * opacity + ((output[x] >> 8) & 0x00ff00ff) * inverse;
			// Exact /255 per 16-bit lane. /256 would compound into dark sprite
			// borders under repeated fades (DrawableSurfaceBlendTest).
			rb += 0x00010001 + ((rb >> 8) & 0x00ff00ff);
			ga += 0x00010001 + ((ga >> 8) & 0x00ff00ff);
			output[x] = ((rb >> 8) & 0x00ff00ff) | (ga & 0xff00ff00);
		}
	}
	SDL_UnlockSurface(target);
    SDL_UnlockSurface(source);
}

// Portable commands carry resolved pixels and modulation. The sum is divided
// once, rather than rounding source and destination terms independently. All
// products fit sixteen-bit lanes and the /255 identity is exact for 0..65025.
Uint32 divide255(Uint32 value) { return (value + 1 + (value >> 8)) >> 8; }
Uint32 packedDivide255(Uint32 value)
{
    return ((value + 0x00010001 + ((value >> 8) & 0x00ff00ff)) >> 8) & 0x00ff00ff;
}
Uint32 packChannels(Uint32 r, Uint32 g, Uint32 b, Uint32 a,
                    const SDL_PixelFormatDetails &format)
{
    return (r << format.Rshift) | (g << format.Gshift) | (b << format.Bshift) |
           (format.Amask ? a << format.Ashift : 0);
}
Uint32 portableBlend(Uint32 value, Uint32 original, Uint32 alpha,
                     const SDL_PixelFormatDetails &format)
{
    const Uint32 inverse = 255 - alpha;
    const Uint32 rb = packedDivide255((value & 0x00ff00ff) * alpha +
                                      (original & 0x00ff00ff) * inverse);
    const Uint32 ga = packedDivide255(((value >> 8) & 0x00ff00ff) * alpha +
                                      ((original >> 8) & 0x00ff00ff) * inverse);
    const Uint32 outAlpha = alpha + divide255(((original & format.Amask) >> format.Ashift) * inverse);
    return ((rb | (ga << 8)) & (format.Rmask | format.Gmask | format.Bmask)) |
           (format.Amask ? outAlpha << format.Ashift : 0);
}
// Filtering stays in premultiplied space: invisible RGB cannot bleed into the
// edge. Coordinates always refer to the whole sprite, independent of the tile.
void linearCommand(const RasterCommand &command, WritablePixelView target,
                   int left, int top, int right, int bottom)
{
    const auto &src = command.sourceRect;
    const auto &source = command.source;
    const auto &dst = command.destinationFloat;
    if (!std::isfinite(dst.x) || !std::isfinite(dst.y) || !std::isfinite(dst.w) ||
        !std::isfinite(dst.h) || dst.w <= 0 || dst.h <= 0 || src.x < 0 || src.y < 0 ||
        std::int64_t(src.x) + src.w > source.width || std::int64_t(src.y) + src.h > source.height)
        return;
    const auto &sf = source.format;
    const auto &tf = target.format;
    for (int y = top; y < bottom; ++y)
    {
        const float v = std::clamp((y - dst.y + .5f) * src.h / dst.h - .5f, 0.f, float(src.h - 1));
        const int y0 = int(v), y1 = std::min(y0 + 1, src.h - 1);
        const float fy = v - y0;
        auto *output = reinterpret_cast<Uint32 *>(target.pixels + std::ptrdiff_t(y) * target.pitch);
        const auto *row0 = reinterpret_cast<const Uint32 *>(source.pixels + std::ptrdiff_t(src.y + y0) * source.pitch);
        const auto *row1 = reinterpret_cast<const Uint32 *>(source.pixels + std::ptrdiff_t(src.y + y1) * source.pitch);
        for (int x = left; x < right; ++x)
        {
            const float u = std::clamp((x - dst.x + .5f) * src.w / dst.w - .5f, 0.f, float(src.w - 1));
            const int x0 = int(u), x1 = std::min(x0 + 1, src.w - 1);
            const float fx = u - x0;
            const float weights[]{(1-fx)*(1-fy), fx*(1-fy), (1-fx)*fy, fx*fy};
            const Uint32 samples[]{row0[src.x+x0], row0[src.x+x1], row1[src.x+x0], row1[src.x+x1]};
            float a = 0, r = 0, g = 0, b = 0;
            for (unsigned i = 0; i < 4; ++i)
            {
                const float coverage = float(command.opaque || !sf.Amask ? 255 :
                    (samples[i] & sf.Amask) >> sf.Ashift) * weights[i] / 255.f;
                a += coverage;
                r += ((samples[i] & sf.Rmask) >> sf.Rshift) * coverage;
                g += ((samples[i] & sf.Gmask) >> sf.Gshift) * coverage;
                b += ((samples[i] & sf.Bmask) >> sf.Bshift) * coverage;
            }
            const float fade = command.opacity / 255.f;
            a *= fade; r *= fade; g *= fade; b *= fade;
            r *= command.red / 255.f; g *= command.green / 255.f; b *= command.blue / 255.f;
            const Uint32 original = output[x];
            const auto channel = [a](float color, Uint32 original)
            { return Uint32(std::clamp(std::lround(color + original * (1-a)), 0l, 255l)); };
            output[x] = packChannels(channel(r, (original & tf.Rmask) >> tf.Rshift),
                channel(g, (original & tf.Gmask) >> tf.Gshift),
                channel(b, (original & tf.Bmask) >> tf.Bshift),
                channel(a*255, (original & tf.Amask) >> tf.Ashift), tf);
        }
    }
}
#if defined(__SSE2__)
// Four pixels share the scalar rule exactly. Restrict this path to alpha in the
// high byte and RGB/BGR byte layouts; other packed formats use the same scalar
// arithmetic below. SSE2 is baseline on x86-64, with no runtime ISA dependency.
__m128i divide255Words(__m128i value)
{
    return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(value, _mm_set1_epi16(1)),
                                       _mm_srli_epi16(value, 8)), 8);
}
__m128i blendFour(__m128i source, __m128i destination, Uint8 opacity)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i fade = _mm_set1_epi16(opacity), full = _mm_set1_epi16(255);
    const __m128i alphaMask = _mm_set_epi16(-1, 0, 0, 0, -1, 0, 0, 0);
    const auto pair = [=](__m128i src, __m128i dst)
    {
        auto alpha = _mm_shufflelo_epi16(src, _MM_SHUFFLE(3,3,3,3));
        alpha = _mm_shufflehi_epi16(alpha, _MM_SHUFFLE(3,3,3,3));
        alpha = divide255Words(_mm_mullo_epi16(alpha, fade));
        const auto inverse = _mm_sub_epi16(full, alpha);
        const auto destinationTerm = _mm_mullo_epi16(dst, inverse);
        const auto rgb = divide255Words(_mm_add_epi16(_mm_mullo_epi16(src, alpha), destinationTerm));
        const auto outAlpha = _mm_add_epi16(alpha, divide255Words(destinationTerm));
        return _mm_or_si128(_mm_andnot_si128(alphaMask, rgb), _mm_and_si128(alphaMask, outAlpha));
    };
    return _mm_packus_epi16(pair(_mm_unpacklo_epi8(source, zero), _mm_unpacklo_epi8(destination, zero)),
                            pair(_mm_unpackhi_epi8(source, zero), _mm_unpackhi_epi8(destination, zero)));
}
#endif
std::int64_t nearestOffset(std::int64_t delta, int sourceExtent, int destinationExtent,
                           RasterCommand::Sampling sampling)
{
    if (sampling == RasterCommand::Sampling::SDLNearest)
    {
        // SDL's uniform textured quad truncates a 16.16 step before walking
        // pixel centers. Rational centers differ at exact sample boundaries.
        const auto step = (std::int64_t(sourceExtent) << 16) / destinationExtent;
        return (step/2 + delta*step) >> 16;
    }
    return (delta*2+1)*sourceExtent/(std::int64_t(destinationExtent)*2);
}
void portableCommand(const RasterCommand &command, WritablePixelView target,
                     int left, int top, int right, int bottom, KernelMode mode)
{
    if (!command.opacity) return;
#if !defined(__SSE2__)
    (void)mode;
#endif
    if (command.kind == RasterCommand::Kind::Fill)
    {
#if defined(__SSE2__)
        const auto &format=target.format;
        const bool vectorFill=mode==KernelMode::Automatic && format.Amask==0xff000000 &&
            format.Rbits==8 && format.Gbits==8 && format.Bbits==8 &&
            (format.Rmask|format.Gmask|format.Bmask)==0x00ffffff;
        // Fill opacity is separate from the packed color's stored alpha. Reuse
        // the sprite kernel with fully covered source pixels to blend four at once.
        const auto pixels=_mm_set1_epi32(command.color|0xff000000);
#endif
        for (int y = top; y < bottom; ++y)
        {
            auto *output = reinterpret_cast<Uint32 *>(target.pixels + std::ptrdiff_t(y) * target.pitch);
            if (command.opacity == 255)
                std::fill(output + left, output + right,
                          (command.color & (target.format.Rmask | target.format.Gmask | target.format.Bmask)) | target.format.Amask);
            else
            {
                int x=left;
#if defined(__SSE2__)
                if (vectorFill)
                    for (;x<=right-4;x+=4)
                    {
                        const auto original=_mm_loadu_si128(reinterpret_cast<const __m128i *>(output+x));
                        _mm_storeu_si128(reinterpret_cast<__m128i *>(output+x),blendFour(pixels,original,command.opacity));
                    }
#endif
                for (;x<right;++x)
                    output[x] = portableBlend(command.color, output[x], command.opacity, target.format);
            }
        }
        return;
    }
    const auto &src = command.sourceRect;
    if (src.w <= 0 || src.h <= 0 || !command.source.pixels) return;
    if (command.sampling == RasterCommand::Sampling::LinearPremultiplied)
    {
        linearCommand(command, target, left, top, right, bottom);
        return;
    }
    const auto &dst = command.destination;
    const auto &source = command.source;
    const auto &sf = source.format;
    const auto &tf = target.format;
    const bool nativeWidth = src.w == dst.w, nativeHeight = src.h == dst.h;
    const bool unmodulated = command.red == 255 && command.green == 255 && command.blue == 255;
    const bool sameRGB = sf.Rmask == tf.Rmask && sf.Gmask == tf.Gmask && sf.Bmask == tf.Bmask;
    const bool copy = command.opaque && command.opacity == 255 && unmodulated &&
                      sf.format == tf.format;
    const auto sourceAddress = reinterpret_cast<std::uintptr_t>(source.pixels);
    const auto targetAddress = reinterpret_cast<std::uintptr_t>(target.pixels);
    const bool disjoint = source.pitch > 0 && target.pitch > 0 &&
        (sourceAddress <= targetAddress ? targetAddress-sourceAddress >= std::size_t(source.pitch)*source.height
                                         : sourceAddress-targetAddress >= std::size_t(target.pitch)*target.height);
    if (copy && nativeWidth && disjoint)
    {
        const auto copyLeft = std::max(std::int64_t(left), std::int64_t(dst.x)-src.x);
        const auto copyRight = std::min(std::int64_t(right), std::int64_t(dst.x)+source.width-src.x);
        if (copyRight <= copyLeft) return;
        for (int y = top; y < bottom; ++y)
        {
            const auto sy = std::int64_t(src.y) + (nativeHeight ? std::int64_t(y)-dst.y :
                nearestOffset(std::int64_t(y)-dst.y, src.h, dst.h, command.sampling));
            if (sy < 0 || sy >= source.height) continue;
            const auto sx = std::int64_t(src.x)+copyLeft-dst.x;
            auto *output = target.pixels + std::ptrdiff_t(y)*target.pitch + copyLeft*4;
            const auto *input = source.pixels + sy*source.pitch + sx*4;
            if (copyRight-copyLeft == 32) std::memcpy(output,input,128);
            else std::memcpy(output,input,std::size_t(copyRight-copyLeft)*4);
        }
        return;
    }
    if (!nativeWidth && disjoint && right-left > 128)
    {
        // Owner execution uses the same bounded sampling scratch as a tile,
        // without allocating a lookup or submitting another worker job.
        for (int x = left; x < right;)
        {
            const int next = x + std::min(128, right-x);
            portableCommand(command, target, x, top, next, bottom, mode);
            x = next;
        }
        return;
    }
    std::array<std::int64_t, 128> columns;
    const bool sampledColumns = !nativeWidth && right-left <= int(columns.size());
    if (sampledColumns)
        for (int x = left; x < right; ++x)
            columns[x-left] = std::int64_t(src.x) + nearestOffset(std::int64_t(x)-dst.x, src.w, dst.w, command.sampling);
#if defined(__SSE2__)
    const bool vectorCopy = command.opaque && command.opacity == 255 && unmodulated;
    const bool vectorBlend = disjoint && mode == KernelMode::Automatic && (!command.opaque || vectorCopy) &&
        sf.Amask == 0xff000000 && tf.Amask == 0xff000000 &&
        sf.Gmask == 0xff00 && tf.Gmask == 0xff00 &&
        ((sf.Rmask == 0xff0000 && sf.Bmask == 0xff) || (sf.Rmask == 0xff && sf.Bmask == 0xff0000)) &&
        ((tf.Rmask == 0xff0000 && tf.Bmask == 0xff) || (tf.Rmask == 0xff && tf.Bmask == 0xff0000));
#endif
    for (int y = top; y < bottom; ++y)
    {
        const auto sy = std::int64_t(src.y) + (nativeHeight ? std::int64_t(y)-dst.y :
            nearestOffset(std::int64_t(y)-dst.y, src.h, dst.h, command.sampling));
        if (sy < 0 || sy >= source.height) continue;
        const auto *input = reinterpret_cast<const Uint32 *>(source.pixels + sy*source.pitch);
        auto *output = reinterpret_cast<Uint32 *>(target.pixels + std::ptrdiff_t(y)*target.pitch);
        int x = left;
#if defined(__SSE2__)
        if (vectorBlend)
            for (; x <= right - 4; x += 4)
            {
                const auto sx0 = nativeWidth ? std::int64_t(src.x)+x-dst.x : columns[x-left];
                const auto sx3 = nativeWidth ? sx0+3 : columns[x-left+3];
                if (sx0 < 0 || sx3 >= source.width) break;
                auto pixels = nativeWidth ? _mm_loadu_si128(reinterpret_cast<const __m128i *>(input+sx0)) :
                    _mm_setr_epi32(input[sx0], input[columns[x-left+1]], input[columns[x-left+2]], input[sx3]);
                if (!sameRGB)
                    pixels = _mm_or_si128(_mm_and_si128(pixels, _mm_set1_epi32(0xff00ff00)),
                        _mm_or_si128(_mm_slli_epi32(_mm_and_si128(pixels, _mm_set1_epi32(0xff)), 16),
                                     _mm_srli_epi32(_mm_and_si128(pixels, _mm_set1_epi32(0xff0000)), 16)));
                if (vectorCopy)
                {
                    if (!copy) pixels = _mm_or_si128(pixels, _mm_set1_epi32(0xff000000));
                    _mm_storeu_si128(reinterpret_cast<__m128i *>(output+x), pixels);
                    continue;
                }
                if (!unmodulated)
                {
                    const short first = tf.Rshift == 0 ? command.red : command.blue;
                    const short third = tf.Rshift == 0 ? command.blue : command.red;
                    const auto modulation = _mm_set_epi16(255, third, command.green, first,
                                                          255, third, command.green, first);
                    const auto zero = _mm_setzero_si128();
                    pixels = _mm_packus_epi16(divide255Words(_mm_mullo_epi16(_mm_unpacklo_epi8(pixels, zero), modulation)),
                                              divide255Words(_mm_mullo_epi16(_mm_unpackhi_epi8(pixels, zero), modulation)));
                }
                const auto previous = _mm_loadu_si128(reinterpret_cast<const __m128i *>(output+x));
                auto blended = blendFour(pixels, previous, command.opacity);
                if (command.forceOpaqueAlpha)
                {
                    // Tiny nonzero source alpha may vanish after opacity.
                    const auto alpha = _mm_srli_epi32(pixels, 24);
                    const auto product = _mm_mullo_epi16(alpha, _mm_set1_epi32(command.opacity));
                    const auto effective = _mm_srli_epi32(_mm_add_epi32(_mm_add_epi32(product, _mm_set1_epi32(1)),
                                                                      _mm_srli_epi32(product, 8)), 8);
                    const auto covered = _mm_andnot_si128(_mm_cmpeq_epi32(effective, _mm_setzero_si128()),
                                                         _mm_set1_epi32(0xff000000));
                    blended = _mm_or_si128(blended, covered);
                }
                _mm_storeu_si128(reinterpret_cast<__m128i *>(output+x), blended);
            }
#endif
        for (; x < right; ++x)
        {
            const auto sx = nativeWidth ? std::int64_t(src.x)+x-dst.x :
                sampledColumns ? columns[x-left] :
                std::int64_t(src.x)+nearestOffset(std::int64_t(x)-dst.x, src.w, dst.w, command.sampling);
            if (sx < 0 || sx >= source.width) continue;
            const Uint32 sample = input[sx];
            // Blend NONE retains stored alpha on exact opaque copies. Other
            // paths resolve alpha separately, including formats without alpha.
            if (copy) { output[x] = sample; continue; }
            const Uint32 alpha = divide255((command.opaque || !sf.Amask ? 255 :
                (sample & sf.Amask) >> sf.Ashift)*command.opacity);
            if (!alpha) continue;
            Uint32 value = sample;
            if (!sameRGB || !unmodulated)
                value = packChannels(divide255(((sample & sf.Rmask) >> sf.Rshift)*command.red),
                    divide255(((sample & sf.Gmask) >> sf.Gshift)*command.green),
                    divide255(((sample & sf.Bmask) >> sf.Bshift)*command.blue), alpha, tf);
            if (alpha == 255)
                output[x] = (value & (tf.Rmask | tf.Gmask | tf.Bmask)) | tf.Amask;
            else
                output[x] = portableBlend(value, output[x], alpha, tf);
            if (command.forceOpaqueAlpha) output[x] |= tf.Amask;
        }
    }
}

} // namespace

void executeCommand(const RasterCommand &command, WritablePixelView target, SDL_Rect region, KernelMode mode)
{
    // Intersect with widened endpoints; callers resolve coordinates once.
    const auto &dst = command.destination;
    const auto left = std::max({std::int64_t(0), std::int64_t(dst.x), std::int64_t(command.clip.x),
                                std::int64_t(region.x)});
    const auto top = std::max({std::int64_t(0), std::int64_t(dst.y), std::int64_t(command.clip.y),
                               std::int64_t(region.y)});
    const auto right = std::min({std::int64_t(target.width), std::int64_t(dst.x) + dst.w,
                                 std::int64_t(command.clip.x) + command.clip.w,
                                 std::int64_t(region.x) + region.w});
    const auto bottom = std::min({std::int64_t(target.height), std::int64_t(dst.y) + dst.h,
                                  std::int64_t(command.clip.y) + command.clip.h,
                                  std::int64_t(region.y) + region.h});
    if (right <= left || bottom <= top || dst.w <= 0 || dst.h <= 0)
        return;
    if (command.blend == RasterCommand::Blending::Portable ||
        (command.kind == RasterCommand::Kind::Fill && command.fillBlend == FillBlend::PortableSourceOver))
    {
        portableCommand(command, target, int(left), int(top), int(right), int(bottom), mode);
        return;
    }
    // Packed sixteen-bit lanes implement exact /255 with independent carries.
    // Triangle-style blending truncates source and destination terms separately.
    const auto lane = [](Uint32 value)
    { return ((value + 0x00010001 + ((value >> 8) & 0x00ff00ff)) >> 8) & 0x00ff00ff; };
    const auto divide255 = [](Uint32 value) { return (value + 1 + (value >> 8)) >> 8; };
    const Uint32 alphaMask = target.format.Amask, alphaShift = target.format.Ashift;
    if (command.kind == RasterCommand::Kind::Fill)
    {
        const Uint32 alpha = command.opacity, inverse = 255 - alpha;
        if (!alpha && command.fillBlend == FillBlend::SourceOver)
            return;
        const Uint32 rb = (command.color & 0x00ff00ff) * alpha;
        const Uint32 ga = ((command.color >> 8) & 0x00ff00ff) * alpha;
        // Keep each mode outside the pixel loop so fills remain vectorizable.
        if (alpha == 255)
        {
            for (int y = int(top); y < bottom; ++y)
            {
                auto *output = reinterpret_cast<Uint32 *>(target.pixels + y * target.pitch);
                std::fill(output + left, output + right, command.color);
            }
        }
        else if (command.fillBlend == FillBlend::Native)
        {
            // Legacy fog uses /256, including its alpha-zero rounding.
            for (int y = int(top); y < bottom; ++y)
            {
                auto *output = reinterpret_cast<Uint32 *>(target.pixels + y * target.pitch);
                for (int x = int(left); x < right; ++x)
                {
                    const auto a = (output[x] & 0x00ff00ff) * inverse + rb;
                    const auto b = ((output[x] >> 8) & 0x00ff00ff) * inverse + ga;
                    output[x] = ((a >> 8) & 0x00ff00ff) | (b & 0xff00ff00);
                }
            }
        }
        else
        {
            for (int y = int(top); y < bottom; ++y)
            {
                auto *output = reinterpret_cast<Uint32 *>(target.pixels + y * target.pitch);
                for (int x = int(left); x < right; ++x)
                {
                    const auto a = lane(rb) + lane((output[x] & 0x00ff00ff) * inverse);
                    const auto b = lane(ga) + lane(((output[x] >> 8) & 0x00ff00ff) * inverse);
                    const auto outAlpha =
                        alpha + divide255(((output[x] & alphaMask) >> alphaShift) * inverse);
                    output[x] =
                        ((a | (b << 8)) & ~alphaMask) | (alphaMask ? outAlpha << alphaShift : 0);
                }
            }
        }
        return;
    }
    const auto &source = command.source;
    const auto &src = command.sourceRect;
    if (src.w <= 0 || src.h <= 0)
        return;
    const bool nativeWidth = src.w == dst.w, nativeHeight = src.h == dst.h;
    const bool copy = command.opaque && command.opacity == 255;
    // Recorded sources have separate storage. Immediate owner views may alias:
    // retain their forward pixel traversal instead of passing overlap to memcpy.
    // This range proof covers forward rows only; signed-pitch views stay scalar.
    const auto sourceFirst = reinterpret_cast<std::uintptr_t>(source.pixels);
    const auto targetFirst = reinterpret_cast<std::uintptr_t>(target.pixels);
    const auto sourceBytes = std::size_t(source.pitch) * source.height;
    const auto targetBytes = std::size_t(target.pitch) * target.height;
    const bool disjoint = source.pitch > 0 && target.pitch > 0 &&
        (sourceFirst <= targetFirst ? targetFirst - sourceFirst >= sourceBytes
                                   : sourceFirst - targetFirst >= targetBytes);
    if (copy && nativeWidth && disjoint)
    {
        const auto copyLeft = std::max(left, std::int64_t(dst.x) - src.x);
        const auto copyRight =
            std::min(right, std::int64_t(dst.x) + source.width - src.x);
        if (copyRight <= copyLeft)
            return;
        for (int y = int(top); y < bottom; ++y)
        {
            const auto sy = std::int64_t(src.y) +
                (nativeHeight ? std::int64_t(y) - dst.y
                              : ((std::int64_t(y) - dst.y) * 2 + 1) * src.h /
                                    (std::int64_t(dst.h) * 2));
            if (sy < 0 || sy >= source.height)
                continue;
            const auto sx = std::int64_t(src.x) + copyLeft - dst.x;
            std::memcpy(target.pixels + y * target.pitch + copyLeft * 4,
                        source.pixels + sy * source.pitch + sx * 4,
                        std::size_t(copyRight - copyLeft) * 4);
        }
        return;
    }
    // Tile jobs use bounded stack scratch; the owner full-region path can
    // allocate a wider lookup. Reuse each sampled column across all rows.
    std::array<std::int64_t, 128> localColumns;
    std::vector<std::int64_t> wideColumns;
    auto *columns = localColumns.data();
    if (!nativeWidth)
    {
        if (right - left > std::int64_t(localColumns.size()))
        {
            wideColumns.resize(std::size_t(right - left));
            columns = wideColumns.data();
        }
        for (int x = int(left); x < right; ++x)
            columns[x - left] = std::int64_t(src.x) + ((std::int64_t(x) - dst.x) * 2 + 1) * src.w /
                                                          (std::int64_t(dst.w) * 2);
    }

    const bool rleBlend =
        command.opacity == 255 && nativeWidth && nativeHeight && alphaMask == 0xff000000;
    for (int y = int(top); y < bottom; ++y)
    {
        const auto sy =
            std::int64_t(src.y) + (nativeHeight ? std::int64_t(y) - dst.y
                                                : ((std::int64_t(y) - dst.y) * 2 + 1) * src.h /
                                                      (std::int64_t(dst.h) * 2));
        if (sy < 0 || sy >= source.height)
            continue;
        const auto *row = reinterpret_cast<const Uint32 *>(source.pixels + sy * source.pitch);
        auto *output = reinterpret_cast<Uint32 *>(target.pixels + y * target.pitch);
        for (int x = int(left); x < right; ++x)
        {
            const auto sx = nativeWidth ? std::int64_t(src.x) + x - dst.x : columns[x - left];
            if (sx < 0 || sx >= source.width)
                continue;
            const Uint32 value = row[sx];
            if (copy)
            {
                output[x] = value;
                continue;
            }
            const Uint32 alpha =
                divide255((alphaMask ? (value & alphaMask) >> alphaShift : 255) * command.opacity);
            if (!alpha)
                continue;
            if (alpha == 255)
            {
                output[x] = value;
                continue;
            }
            const Uint32 original = output[x], inverse = 255 - alpha;
            if (rleBlend)
            {
                // SDL's unscaled ARGB software texture path interpolates RGB
                // with /256 and writes opaque destination alpha.
                const auto rb = value & 0x00ff00ff, dstRB = original & 0x00ff00ff;
                const auto g = value & 0x0000ff00, dstG = original & 0x0000ff00;
                output[x] = ((dstRB + (((rb - dstRB) * alpha) >> 8)) & 0x00ff00ff) |
                            ((dstG + (((g - dstG) * alpha) >> 8)) & 0x0000ff00) | alphaMask;
            }
            else
            {
                const auto rb =
                    lane((value & 0x00ff00ff) * alpha) + lane((original & 0x00ff00ff) * inverse);
                const auto ga = lane(((value >> 8) & 0x00ff00ff) * alpha) +
                                lane(((original >> 8) & 0x00ff00ff) * inverse);
                output[x] =
                    ((rb | (ga << 8)) & ~alphaMask) |
                    (alphaMask
                         ? (alpha + divide255(((original & alphaMask) >> alphaShift) * inverse))
                               << alphaShift
                         : 0);
            }
        }
    }
}
void executeRegion(std::span<const RasterCommand> commands, WritablePixelView target,
                   SDL_Rect region, KernelMode mode)
{
    for (const auto &command : commands)
        executeCommand(command, target, region, mode);
}

void blit(SDL_Surface *target, SDL_Surface *source, const SDL_Rect &sourceRect,
          SDL_Rect destination, Uint8 alpha, bool isOpaque, BlitBlend blend)
{
    SDL_Rect targetClip;
    check(SDL_GetSurfaceClipRect(target, &targetClip));
    if (destination.w <= 0 || destination.h <= 0 || sourceRect.w <= 0 || sourceRect.h <= 0)
        return;
    if (blend == BlitBlend::Native && alpha < 255 && destination.w == sourceRect.w &&
        destination.h == sourceRect.h)
    {
        nativeAlpha(target, source, sourceRect, destination, alpha);
        return;
    }
    SourceState saved(source);
    if (blend == BlitBlend::Surface && alpha == 255)
        alpha = saved.modulation();
    const bool unscaled = destination.w == sourceRect.w && destination.h == sourceRect.h;
    const bool opaqueCopy = isOpaque && alpha == 255 &&
                            (blend != BlitBlend::Native ||
                             (saved.modulation() == 255 && (saved.mode() == SDL_BLENDMODE_BLEND ||
                                                            saved.mode() == SDL_BLENDMODE_NONE)));
    if (unscaled && opaqueCopy &&
		copyRows(target, source, sourceRect, destination, blend == BlitBlend::Triangle))
		return;
	if (unscaled && blend != BlitBlend::Triangle)
	{
		if (blend != BlitBlend::Native)
		{
			saved.setAlpha(alpha);
			saved.setBlend(opaqueCopy ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
		}
		else if (opaqueCopy)
			saved.setBlend(SDL_BLENDMODE_NONE);
		check(SDL_BlitSurface(source, &sourceRect, target, &destination));
	}
	else if (blend == BlitBlend::Surface && SDL_HasRectIntersection(&destination, &targetClip) &&
			 destination.x >= targetClip.x && destination.y >= targetClip.y &&
			 destination.x + destination.w <= targetClip.x + targetClip.w &&
			 destination.y + destination.h <= targetClip.y + targetClip.h && sourceRect.x >= 0 &&
			 sourceRect.y >= 0 && sourceRect.x + sourceRect.w <= source->w &&
			 sourceRect.y + sourceRect.h <= source->h)
	{
		// SDL's optimized nearest scaler is safe when neither rectangle is cut.
		// In particular, native UI minimaps keep their original fast SIMD path.
		// Clipped or transformed draws retain the original-coordinate sampler.
		saved.setAlpha(alpha);
		saved.setBlend(opaqueCopy ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
		SDL_Rect destinationCopy = destination;
		check(SDL_BlitSurfaceScaled(source, &sourceRect, target, &destinationCopy,
									SDL_SCALEMODE_NEAREST));
	}
	else
		nearest(target, source, sourceRect, destination, alpha, isOpaque,
				blend == BlitBlend::Triangle);
}

// Skin-only bilinear sampling in premultiplied space, then source-over. Map
// from the original rectangle even when clipped; never sample adjacent frames.
void skinBlit(SDL_Surface *target,SDL_Surface *source,const SDL_Rect &src,SDL_Rect dst,Uint8 opacity)
{
    skinBlitFloat(target,source,src,SDL_FRect{float(dst.x),float(dst.y),float(dst.w),float(dst.h)},opacity);
}
void skinBlitFloat(SDL_Surface *target,SDL_Surface *source,const SDL_Rect &src,SDL_FRect dst,Uint8 opacity)
{
    if(!target || !source || !opacity || dst.w<=0 || dst.h<=0 || src.w<=0 || src.h<=0)return;
    if(source->format!=SDL_PIXELFORMAT_ARGB8888 || target->format!=SDL_PIXELFORMAT_ARGB8888)
        throw std::invalid_argument("Skin sprites require ARGB8888 surfaces");
    const double leftEdge=std::ceil(dst.x-.5f),topEdge=std::ceil(dst.y-.5f);
    const double rightEdge=std::ceil(dst.x+dst.w-.5f),bottomEdge=std::ceil(dst.y+dst.h-.5f);
    for (double edge : {leftEdge,topEdge,rightEdge,bottomEdge})
        if (!std::isfinite(edge) || edge < std::numeric_limits<int>::min() ||
            edge > std::numeric_limits<int>::max()) return;
    if (rightEdge-leftEdge > std::numeric_limits<int>::max() ||
        bottomEdge-topEdge > std::numeric_limits<int>::max()) return;
    const int left=int(leftEdge),top=int(topEdge);
    const SDL_Rect bounds{left,top,int(rightEdge-leftEdge),int(bottomEdge-topEdge)};
    SDL_Rect clip,visible;SDL_GetSurfaceClipRect(target,&clip);
    if(!SDL_GetRectIntersection(&bounds,&clip,&visible))return;
    if(src.x<0 || src.y<0 || std::int64_t(src.x)+src.w>source->w || std::int64_t(src.y)+src.h>source->h)return;
    check(SDL_LockSurface(source));
    if(!SDL_LockSurface(target)){SDL_UnlockSurface(source);throw std::runtime_error(SDL_GetError());}
    RasterCommand command;
    command.kind = RasterCommand::Kind::Blit;
    command.source = {static_cast<const Uint8 *>(source->pixels), source->w, source->h,
                      source->pitch, *SDL_GetPixelFormatDetails(source->format)};
    command.sourceRect = src;
    command.destination = bounds;
    command.destinationFloat = dst;
    command.clip = clip;
    command.opacity = opacity;
    command.blend = RasterCommand::Blending::Portable;
    command.sampling = RasterCommand::Sampling::LinearPremultiplied;
    try
    {
        executeCommand(command, {static_cast<Uint8 *>(target->pixels), target->w, target->h,
                       target->pitch, *SDL_GetPixelFormatDetails(target->format)}, visible);
    }
    catch (...)
    {
        SDL_UnlockSurface(target);
        SDL_UnlockSurface(source);
        throw;
    }
    SDL_UnlockSurface(target);SDL_UnlockSurface(source);
}

void fill(SDL_Surface *target, SDL_Rect rect, Uint32 color, Uint8 alpha, FillBlend blend)
{
	SDL_Rect targetClip;
	check(SDL_GetSurfaceClipRect(target, &targetClip));
	SDL_Rect clipped;
	if (!SDL_GetRectIntersection(&rect, &targetClip, &clipped))
		return;
	if (alpha == 255 && blend != FillBlend::Native && blend != FillBlend::PortableSourceOver)
	{
		check(SDL_FillSurfaceRect(target, &clipped, color));
		return;
	}
	if (blend == FillBlend::SourceOver && alpha == 0)
		return;
	const bool needsLock = SDL_MUSTLOCK(target);
	if (needsLock)
		check(SDL_LockSurface(target));
    RasterCommand command;
    command.destination = rect;
    command.clip = targetClip;
    command.color = color;
    command.opacity = alpha;
    command.fillBlend = blend;
    executeCommand(command,
                   {static_cast<Uint8 *>(target->pixels), target->w, target->h, target->pitch,
                    *SDL_GetPixelFormatDetails(target->format)},
                   targetClip);
	if (needsLock)
		SDL_UnlockSurface(target);
}

bool opaque(SDL_Surface *surface)
{
	if (!surface)
		return false;
	Uint32 key;
	if (SDL_GetSurfaceColorKey(surface, &key))
		return false;
	const auto *details = SDL_GetPixelFormatDetails(surface->format);
	if (!details->Amask)
		return true;
	if (details->bytes_per_pixel != 4)
		return false;
	check(SDL_LockSurface(surface));
	bool result = true;
	for (int y = 0; y < surface->h && result; ++y)
	{
		const auto *row = static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch;
		for (int x = 0; x < surface->w; ++x)
		{
			Uint32 pixel;
			std::memcpy(&pixel, row + x * 4, 4);
			if ((pixel & details->Amask) != details->Amask)
			{
				result = false;
				break;
			}
		}
	}
	SDL_UnlockSurface(surface);
	return result;
}
} // namespace GAGCore::SurfaceRaster
