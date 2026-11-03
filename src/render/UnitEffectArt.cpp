// SPDX-License-Identifier: GPL-3.0-or-later

#include "UnitEffectArt.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

using GAGCore::Color;
using GAGCore::DrawableSurface;

namespace
{
constexpr int Size = 64;  //!< pixels across each image
constexpr int Samples = 4; //!< per pixel side, for antialiased edges

struct Rgba { float r, g, b, a; };

Rgba hsl(float hue, float saturation, float lightness, float alpha = 1)
{
	const float c = (1 - std::fabs(2 * lightness - 1)) * saturation;
	const float h = std::fmod(std::fmod(hue, 360.f) + 360.f, 360.f) / 60;
	const float x = c * (1 - std::fabs(std::fmod(h, 2.f) - 1)), m = lightness - c / 2;
	float r = 0, g = 0, b = 0;
	if (h < 1) { r = c; g = x; }
	else if (h < 2) { r = x; g = c; }
	else if (h < 3) { g = c; b = x; }
	else if (h < 4) { g = x; b = c; }
	else if (h < 5) { r = x; b = c; }
	else { r = c; b = x; }
	return {r + m, g + m, b + m, alpha};
}

Rgba mix(const Rgba &a, const Rgba &b, float t)
{
	return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

//! A radial gradient with stops at 0, 0.35 and 1 of its radius, clear beyond.
Rgba radial(float distance, const Rgba &centre, const Rgba &middle, const Rgba &edge)
{
	if (distance >= 1)
		return {0, 0, 0, 0};
	return distance < 0.35f ? mix(centre, middle, distance / 0.35f) : mix(middle, edge, (distance - 0.35f) / 0.65f);
}

//! Premultiplied accumulation of layers painted over one another.
struct Pixel
{
	float r = 0, g = 0, b = 0, a = 0;
	void over(const Rgba &c)
	{
		r = c.r * c.a + r * (1 - c.a);
		g = c.g * c.a + g * (1 - c.a);
		b = c.b * c.a + b * (1 - c.a);
		a = c.a + a * (1 - c.a);
	}
};

//! Paint an image from shade(u, v, pixel), with u and v from -1 to 1 across it.
//! Transparent pixels keep the colour `clear` rather than black, so filtering
//! the image down to a few pixels does not darken its soft edges.
template <class Shade>
std::unique_ptr<DrawableSurface> rasterize(Shade shade, const Rgba &clear)
{
	SDL_Surface *surface = SDL_CreateSurface(Size, Size, SDL_PIXELFORMAT_ARGB8888);
	if (!surface)
		throw std::runtime_error(SDL_GetError());
	for (int y = 0; y < Size; ++y)
	{
		Uint32 *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(surface->pixels) + y * surface->pitch);
		for (int x = 0; x < Size; ++x)
		{
			Pixel sum;
			for (int sy = 0; sy < Samples; ++sy)
				for (int sx = 0; sx < Samples; ++sx)
				{
					Pixel sample;
					shade((x + (sx + 0.5f) / Samples) * 2 / Size - 1, (y + (sy + 0.5f) / Samples) * 2 / Size - 1, sample);
					sum.r += sample.r; sum.g += sample.g; sum.b += sample.b; sum.a += sample.a;
				}
			const float n = Samples * Samples, a = sum.a / n;
			const auto channel = [&](float premultiplied, float fallback) {
				return Uint8(std::lround(255 * std::clamp(a > 1.f / 255 ? premultiplied / n / a : fallback, 0.f, 1.f)));
			};
			row[x] = SDL_MapSurfaceRGBA(surface, channel(sum.r, clear.r), channel(sum.g, clear.g), channel(sum.b, clear.b),
				Uint8(std::lround(255 * a)));
		}
	}
	auto result = std::make_unique<DrawableSurface>(surface);
	SDL_DestroySurface(surface);
	return result;
}

//! A soft round body with a cross of long spikes and a fainter diagonal cross.
void shadeStar(float u, float v, Pixel &pixel, float hue, float saturation, float bodyLightness, float spikeLightness)
{
	pixel.over(radial(std::hypot(u, v) / 0.55f, hsl(hue, saturation, bodyLightness),
		hsl(hue, saturation * 0.95f, bodyLightness - 0.2f), hsl(hue, saturation * 0.95f, bodyLightness - 0.25f, 0)));
	const Rgba spike = hsl(hue, saturation, spikeLightness);
	struct Cross { float width, length, alpha; bool diagonal; };
	for (const Cross &cross : {Cross{2.2f / 32, 0.95f, 1, false}, Cross{1.4f / 32, 0.55f, 0.55f, true}})
	{
		const float pu = cross.diagonal ? (u + v) * 0.70710678f : u;
		const float pv = cross.diagonal ? (v - u) * 0.70710678f : v;
		for (const auto &[along, across] : {std::pair{pu, pv}, std::pair{pv, pu}})
			if (std::fabs(across) <= cross.width / 2 && std::fabs(along) <= cross.length)
				pixel.over({spike.r, spike.g, spike.b, cross.alpha * (1 - std::fabs(along) / cross.length)});
	}
}

int hueKey(float hue)
{
	return int(std::lround(hue)) % 360;
}
}

UnitEffectArt::UnitEffectArt() = default;
UnitEffectArt::~UnitEffectArt() = default;

float UnitEffectArt::hueOf(const Color &team)
{
	const float r = team.r / 255.f, g = team.g / 255.f, b = team.b / 255.f;
	const float high = std::max({r, g, b}), low = std::min({r, g, b}), range = high - low;
	if (range <= 0)
		return 0;
	float hue = high == r ? std::fmod((g - b) / range, 6.f) : high == g ? (b - r) / range + 2 : (r - g) / range + 4;
	hue *= 60;
	return hue < 0 ? hue + 360 : hue;
}

Color UnitEffectArt::tint(float hue)
{
	const Rgba c = hsl(hue, 1, 0.7f);
	return Color(Uint8(std::lround(255 * c.r)), Uint8(std::lround(255 * c.g)), Uint8(std::lround(255 * c.b)));
}

Color UnitEffectArt::serviceTint(UnitEffect::Kind kind)
{
	const auto service = UnitEffect::serviceColor(kind);
	const Rgba c = hsl(service.hue, service.saturation, service.lightness);
	return Color(Uint8(std::lround(255 * c.r)), Uint8(std::lround(255 * c.g)), Uint8(std::lround(255 * c.b)));
}

DrawableSurface &UnitEffectArt::star(float hue)
{
	auto &image = stars[hueKey(hue)];
	if (!image)
		image = rasterize([hue](float u, float v, Pixel &pixel) { shadeStar(u, v, pixel, hue, 1, 0.75f, 0.72f); },
			hsl(hue, 0.95f, 0.5f));
	return *image;
}

DrawableSurface &UnitEffectArt::glow(float hue, float lightness)
{
	auto &image = glows[hueKey(hue) * 100 + int(std::lround(lightness * 99))];
	if (!image)
		image = rasterize([hue, lightness](float u, float v, Pixel &pixel) {
			pixel.over(radial(std::hypot(u, v), hsl(hue, 1, lightness), hsl(hue, 0.95f, 0.6f), hsl(hue, 0.95f, 0.55f, 0)));
		}, hsl(hue, 0.95f, 0.55f));
	return *image;
}

DrawableSurface &UnitEffectArt::core()
{
	if (!core_)
		core_ = rasterize([](float u, float v, Pixel &pixel) {
			const float d = std::hypot(u, v) / (6.f / 32);
			if (d < 1)
				pixel.over({1, 1, 1, 1 - d});
		}, {1, 1, 1, 0});
	return *core_;
}

DrawableSurface &UnitEffectArt::glint()
{
	// The star's shape in grey: its light is added, so it flashes white.
	if (!glint_)
		glint_ = rasterize([](float u, float v, Pixel &pixel) { shadeStar(u, v, pixel, 0, 0, 0.75f, 0.72f); }, {0.5f, 0.5f, 0.5f, 0});
	return *glint_;
}

namespace
{
Uint8 opacityByte(float alpha)
{
	return Uint8(std::lround(255 * std::clamp(alpha, 0.f, 1.f)));
}
}

void UnitEffectArt::draw(GAGCore::GraphicContext &gfx, float hue, float x, float y, const UnitEffect::Sparkle &sparkle, float opacity)
{
	const float cx = x + sparkle.x, cy = y + sparkle.y, size = sparkle.size;
	// The team colour blends normally (added, red would turn yellow on grass);
	// the white core and glint add light.
	if (const Uint8 alpha = opacityByte(sparkle.alpha * opacity))
	{
		gfx.drawSurface(cx - size / 2, cy - size / 2, size, size, &star(hue), alpha);
		gfx.drawSurfaceAdditive(cx - size / 2, cy - size / 2, size, size, &core(), opacityByte(sparkle.alpha * opacity * 0.7f));
	}
	if (const Uint8 alpha = opacityByte(sparkle.glint * opacity))
		gfx.drawSurfaceAdditive(cx - size, cy - size, 2 * size, 2 * size, &glint(), alpha);
}

void UnitEffectArt::draw(GAGCore::GraphicContext &gfx, float hue, float x, float y, const UnitEffect::Glow &glowSpec, float opacity)
{
	if (const Uint8 alpha = opacityByte(glowSpec.alpha * opacity))
	{
		const float r = glowSpec.radius;
		gfx.drawSurface(x + glowSpec.x - r, y + glowSpec.y - r, 2 * r, 2 * r, &glow(hue, glowSpec.lightness), alpha);
	}
}

void UnitEffectArt::draw(GAGCore::GraphicContext &gfx, GAGCore::Sprite &icons, int glyph, UnitEffect::Kind kind,
	float x, float y, const UnitEffect::Icon &icon, float opacity, float pixelsPerUnit)
{
	// Each mapicon glyph has frames this many pixels across (tools/icons/export_map_icons.py).
	static constexpr int Sizes[] = {10, 12, 16, 20, 24, 32, 48};
	constexpr int SizeCount = int(sizeof(Sizes) / sizeof(Sizes[0]));
	int size = 0;
	for (int i = 0; i < SizeCount; ++i)
		if (Sizes[i] <= icon.size * pixelsPerUnit)
			size = i;
	const unsigned frame = unsigned(glyph * SizeCount + size);
	const Uint8 alpha = opacityByte(icon.alpha * opacity);
	if (!alpha || int(frame) >= icons.getFrameCount())
		return;
	const float half = icon.size / 2, cx = x + icon.x, cy = y + icon.y;
	// The shadow keeps a green cross readable on grass.
	if (DrawableSurface *shadow = icons.silhouette(frame, Color(10, 20, 10)))
		gfx.drawSurface(cx - half + 0.6f, cy - half + 0.6f, icon.size, icon.size, shadow, opacityByte(icon.alpha * opacity * 0.55f));
	if (DrawableSurface *glyphShape = icons.silhouette(frame, serviceTint(kind)))
		gfx.drawSurface(cx - half, cy - half, icon.size, icon.size, glyphShape, alpha);
}
