// SPDX-License-Identifier: GPL-3.0-or-later
#include <SurfaceRaster.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <memory>
#include <cstdint>

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
	std::vector<int> columns(visible.w);
	for (int x = 0; x < visible.w; ++x)
		columns[x] = src.x + int(((std::int64_t(visible.x + x) - dst.x) * 2 + 1) * src.w /
								 (std::int64_t(dst.w) * 2));
	check(SDL_LockSurface(source));
	if (!SDL_LockSurface(target))
	{
		SDL_UnlockSurface(source);
		throw std::runtime_error(SDL_GetError());
	}
	for (int y = visible.y; y < visible.y + visible.h; ++y)
	{
		const int sy =
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
			const int sx = columns[x];
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

} // namespace

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
    const int left=int(std::ceil(dst.x-.5f)),top=int(std::ceil(dst.y-.5f));
    const SDL_Rect bounds{left,top,int(std::ceil(dst.x+dst.w-.5f))-left,int(std::ceil(dst.y+dst.h-.5f))-top};
    SDL_Rect clip,visible;SDL_GetSurfaceClipRect(target,&clip);
    if(!SDL_GetRectIntersection(&bounds,&clip,&visible))return;
    if(src.x<0 || src.y<0 || src.x+src.w>source->w || src.y+src.h>source->h)return;
    check(SDL_LockSurface(source));
    if(!SDL_LockSurface(target)){SDL_UnlockSurface(source);throw std::runtime_error(SDL_GetError());}
    for(int y=visible.y;y<visible.y+visible.h;++y)for(int x=visible.x;x<visible.x+visible.w;++x) {
        const float u=std::clamp((x-dst.x+0.5f)*src.w/dst.w-0.5f,0.f,float(src.w-1));
        const float v=std::clamp((y-dst.y+0.5f)*src.h/dst.h-0.5f,0.f,float(src.h-1));
        const int x0=int(u),y0=int(v),x1=std::min(x0+1,src.w-1),y1=std::min(y0+1,src.h-1);
        const float fx=u-x0,fy=v-y0,weights[]{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
        const auto pixel=[&](int a,int b){return reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(source->pixels)+(src.y+b)*source->pitch)[src.x+a];};
        const Uint32 samples[]{pixel(x0,y0),pixel(x1,y0),pixel(x0,y1),pixel(x1,y1)};
        float a=0,r=0,g=0,b=0;
        for(unsigned i=0;i<4;++i) {
            const float coverage=float(samples[i]>>24)*weights[i]/255.f;
            a+=coverage;r+=((samples[i]>>16)&255)*coverage;g+=((samples[i]>>8)&255)*coverage;b+=(samples[i]&255)*coverage;
        }
        const float fade=opacity/255.f; a*=fade;r*=fade;g*=fade;b*=fade;
        auto &out=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(target->pixels)+y*target->pitch)[x];
        const auto channel=[&](float color,unsigned original){return Uint32(std::clamp(std::lround(color+original*(1-a)),0l,255l));};
        out=(channel(a*255,out>>24)<<24)|(channel(r,(out>>16)&255)<<16)|(channel(g,(out>>8)&255)<<8)|channel(b,out&255);
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
	if (alpha == 255 && blend != FillBlend::Native)
	{
		check(SDL_FillSurfaceRect(target, &clipped, color));
		return;
	}
	if (blend == FillBlend::SourceOver && alpha == 0)
		return;
	// Preserve the legacy divide-by-256 RGB arithmetic, including alpha zero.
	// Changing this rounding would change accumulated translucent overlays.
	const Uint32 inverse = 255 - alpha;
	const Uint32 redBlue = (color & 0x00ff00ff) * alpha;
	const Uint32 greenAlpha = ((color >> 8) & 0x00ff00ff) * alpha;
	// The presenter owns plain CPU buffers. SDL only requires a lock for
	// RLE targets; avoid compatibility-layer calls for every small fog cell.
	const bool needsLock = SDL_MUSTLOCK(target);
	if (needsLock)
		check(SDL_LockSurface(target));
	// Copy the SDL-written rectangle to scalar bounds before pixel writes.
	// Keeping an escaped SDL_Rect in the inner loop creates possible aliases
	// with the destination and prevents the native blend loop from vectorizing.
	const int left = clipped.x, top = clipped.y, width = clipped.w;
	const int bottom = clipped.y + clipped.h;
	// Keep legacy arithmetic in a separate loop. A per-pixel choice between
	// /256 and source-over alpha prevents vectorization of native fog fills.
	if (blend == FillBlend::Native && alpha == 255)
	{
		// Native fog contains many small opaque cells. Keep the original
		// vectorizable stores instead of entering SDL for every individual cell.
		for (int y = top; y < bottom; ++y)
		{
			auto *pixel = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(target->pixels) +
													 y * target->pitch) +
						  left;
			std::fill_n(pixel, width, color);
		}
	}
	else if (blend == FillBlend::Native)
	{
		for (int y = top; y < bottom; ++y)
		{
			auto *pixel = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(target->pixels) +
													 y * target->pitch) +
						  left;
			for (int x = 0; x < width; ++x)
			{
				const Uint32 a = (pixel[x] & 0x00ff00ff) * inverse + redBlue;
				const Uint32 b = ((pixel[x] >> 8) & 0x00ff00ff) * inverse + greenAlpha;
				pixel[x] = ((a >> 8) & 0x00ff00ff) | (b & 0xff00ff00);
			}
		}
	}
	else
	{
		const Uint32 mask = SDL_GetPixelFormatDetails(target->format)->Amask,
					 shift = SDL_GetPixelFormatDetails(target->format)->Ashift;
		for (int y = top; y < bottom; ++y)
		{
			auto *pixel = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(target->pixels) +
													 y * target->pitch) +
						  left;
			for (int x = 0; x < width; ++x)
			{
				const auto lane = [](Uint32 value)
				{ return ((value + 0x00010001 + ((value >> 8) & 0x00ff00ff)) >> 8) & 0x00ff00ff; };
				// SDL3 recognizes uniform rectangular geometry as fill commands;
				// its source-over path truncates each blend term separately.
				const Uint32 rb = lane(redBlue) + lane((pixel[x] & 0x00ff00ff) * inverse);
				const Uint32 ga = lane(greenAlpha) + lane(((pixel[x] >> 8) & 0x00ff00ff) * inverse);
				const Uint32 product = ((pixel[x] & mask) >> shift) * inverse;
				const Uint32 outAlpha = alpha + ((product + 1 + (product >> 8)) >> 8);
				pixel[x] = (rb | (ga << 8)) & ~mask;
				if (mask)
					pixel[x] |= outAlpha << shift;
			}
		}
	}
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
