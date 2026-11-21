// SPDX-License-Identifier: GPL-3.0-or-later
#include "TouchCard.h"
#include "InGameTouchTheme.h"
#include "GlobalContainer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
using namespace GAGCore;

namespace TouchCard
{
namespace
{
Color scaled(const Color &c, double factor, Uint8 alpha)
{
	auto channel = [factor](Uint8 v) { return Uint8(std::clamp(v * factor, 0., 255.)); };
	return {channel(c.r), channel(c.g), channel(c.b), alpha};
}
Color mixed(const Color &a, const Color &b, double t)
{
	auto channel = [t](Uint8 x, Uint8 y) { return Uint8(std::lround(x + (y - x) * t)); };
	return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
}
struct Paint
{
	Color fill, rim, ink;
	double rimWidth;     // points
	double lift;         // top-to-bottom brightness spread
	double shadowAlpha;  // 0-1
	double shadowBlur;   // points
	double shadowOffset; // points
	double highlight;    // brightening of the inner top edge
};
Paint paint(Tone tone)
{
	const Color gold = InGameTouchTheme::border();
	switch (tone)
	{
	case Tone::Surface:
		return {scaled(InGameTouchTheme::paper(), 1.0, 240), Color(gold.r, gold.g, gold.b, 70), ink(tone), 1, .10, .55, 14, 5, .10};
	case Tone::Selected:
		return {scaled(InGameTouchTheme::selected(), 1.08, 250), gold, ink(tone), 2, .16, .40, 8, 3, .18};
	case Tone::Primary:
		return {scaled(gold, 1.05, 255), scaled(gold, .70, 255), ink(tone), 1, .28, .45, 8, 3, .30};
	case Tone::Danger:
	{
		const Color base = InGameTouchTheme::destroy();
		return {mixed(scaled(base, 1.55, 255), Color(178, 58, 64, 255), .45), Color(235, 130, 120, 150), ink(tone), 1,
				.22, .45, 8, 3, .20};
	}
	case Tone::Disabled:
		return {scaled(InGameTouchTheme::field(), .9, 225), Color(gold.r, gold.g, gold.b, 90), ink(tone), 1, .08, .25, 6, 2, .06};
	case Tone::Raised:
	default:
		return {scaled(InGameTouchTheme::field(), 1.06, 248), Color(gold.r, gold.g, gold.b, 80), ink(tone), 1, .18, .40, 8, 3, .14};
	}
}
double box(double px, double py, double hw, double hh, double r)
{
	const double qx = std::abs(px) - (hw - r), qy = std::abs(py) - (hh - r);
	return std::hypot(std::max(qx, 0.), std::max(qy, 0.)) + std::min(std::max(qx, qy), 0.) - r;
}
double segment(double px, double py, double ax, double ay, double bx, double by)
{
	const double dx = bx - ax, dy = by - ay;
	const double t = std::clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0., 1.);
	return std::hypot(px - ax - t * dx, py - ay - t * dy);
}
// Signed distance, in glyph units (a 24-unit square centred on the origin).
double glyphDistance(Glyph glyph, double x, double y)
{
	const double stroke = 1.6;
	switch (glyph)
	{
	case Glyph::Check:
		return std::min(segment(x, y, -7, 0.5, -2.5, 5.5), segment(x, y, -2.5, 5.5, 7.5, -5.5)) - stroke;
	case Glyph::Cross:
		return std::min(segment(x, y, -6, -6, 6, 6), segment(x, y, -6, 6, 6, -6)) - stroke;
	case Glyph::Plus:
		return std::min(segment(x, y, -7, 0, 7, 0), segment(x, y, 0, -7, 0, 7)) - stroke;
	case Glyph::Minus:
		return segment(x, y, -7, 0, 7, 0) - stroke;
	case Glyph::Trash:
		return std::min({std::abs(box(x, y + -2.5, 6, 7.5, 1)) - 1.2, box(x, y + 8.5, 8.5, 1, .5),
						 std::abs(box(x, y + 11, 2.5, 1.8, .5)) - 1, box(x + 2.2, y - 2.5, .6, 4.5, .3),
						 box(x - 2.2, y - 2.5, .6, 4.5, .3)});
	default:
		return 1e9;
	}
}
} // namespace

Color ink(Tone tone)
{
	switch (tone)
	{
	case Tone::Primary:
		return {38, 24, 44};
	case Tone::Disabled:
	{
		const auto &c = InGameTouchTheme::ink();
		return {c.r, c.g, c.b, 150};
	}
	case Tone::Danger:
		return {255, 236, 228};
	default:
		return InGameTouchTheme::ink();
	}
}

struct Painter::Cache
{
	struct Entry
	{
		std::array<double, 12> key;
		double x, y, w, h; // drawable units, relative to the card's origin
		std::unique_ptr<DrawableSurface> image;
	};
	std::vector<Entry> entries;
};
Painter::Painter() : cache(std::make_unique<Cache>()) {}
Painter::~Painter() = default;

ViewRect Painter::labelArea(ViewRect r, const Style &style)
{
	if (style.glyph == Glyph::None || !style.glyphLeading)
		return r;
	const double side = std::min(r.h, 56 * globalContainer->gfx->logicalUnitsPerPoint());
	const double lead = std::min(side * .8, r.w * .4);
	return {r.x + lead, r.y, std::max(0., r.w - lead), r.h};
}

void Painter::draw(ViewRect rect, const Style &style)
{
	if (rect.w <= 1 || rect.h <= 1)
		return;
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const double density = std::max(.25, double(gfx->getRasterScale()));
	const Paint p = paint(style.tone);
	auto pack = [](const Color &c) { return double((Uint32(c.a) << 24) | (Uint32(c.r) << 16) | (Uint32(c.g) << 8) | c.b); };
	const double w = std::round(rect.w * density) / density, h = std::round(rect.h * density) / density;
	const std::array<double, 12> key{w, h, style.radius * unit, double(int(style.tone)), double(int(style.glyph)),
									 double(style.glyphLeading) + 2 * double(style.compact), double(style.shadow), density,
									 pack(p.fill), pack(p.rim), pack(p.ink), unit};
	auto found = std::find_if(cache->entries.begin(), cache->entries.end(), [&](const auto &e) { return e.key == key; });
	if (found == cache->entries.end())
	{
		const double shadowScale = style.compact ? .55 : 1;
		const double blur = style.shadow ? p.shadowBlur * unit * shadowScale : 0,
					 offset = style.shadow ? p.shadowOffset * unit * shadowScale : 0;
		const double margin = std::ceil(blur + offset + 2 / density);
		const int pw = std::max(1, int(std::ceil((w + 2 * margin) * density))),
				  ph = std::max(1, int(std::ceil((h + 2 * margin) * density)));
		auto *pixels = SDL_CreateSurface(pw, ph, SDL_PIXELFORMAT_ARGB8888);
		if (!pixels)
			return;
		SDL_SetSurfaceBlendMode(pixels, SDL_BLENDMODE_BLEND);
		const double radius = std::min({style.radius * unit, w / 2, h / 2});
		const double rim = p.rimWidth * unit, px1 = 1 / density;
		// The glyph square: centred, or leading at the card's left.
		const double side = std::min(h, 56 * unit);
		const double gx = style.glyphLeading ? std::min(side / 2 + 4 * unit, w * .2) : w / 2, gy = h / 2;
		const double gscale = std::min(side, w) * .42 / 24;
		for (int y = 0; y < ph; ++y)
		{
			auto *line = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels->pixels) + y * pixels->pitch);
			for (int x = 0; x < pw; ++x)
			{
				const double fx = (x + .5) / density - margin, fy = (y + .5) / density - margin;
				const double d = box(fx - w / 2, fy - h / 2, w / 2, h / 2, radius);
				const double cover = std::clamp(.5 - d * density, 0., 1.);
				double shadow = 0;
				if (blur > 0)
				{
					const double ds = box(fx - w / 2, fy - h / 2 - offset, w / 2, h / 2, radius);
					const double t = std::clamp((ds + blur * .25) / blur, 0., 1.);
					shadow = p.shadowAlpha * (1 - t) * (1 - t);
				}
				double r = 0, g = 0, b = 0, a = 0;
				if (cover > 0)
				{
					const double t = std::clamp(fy / h, 0., 1.);
					double light = 1 + p.lift * (.5 - t);
					Color c = scaled(p.fill, light, p.fill.a);
					// Inner top highlight, then the rim on the outer edge.
					if (d > -rim - 2 * px1 && d <= -rim && fy < h * .5)
						c = mixed(c, Color(255, 255, 255, c.a), p.highlight * (1 - 2 * t));
					if (d > -rim)
						c = mixed(c, Color(p.rim.r, p.rim.g, p.rim.b, c.a), p.rim.a / 255.);
					if (style.glyph != Glyph::None)
					{
						const double gd = glyphDistance(style.glyph, (fx - gx) / gscale, (fy - gy) / gscale) * gscale;
						const double inked = std::clamp(.5 - gd * density, 0., 1.) * p.ink.a / 255.;
						c = mixed(c, Color(p.ink.r, p.ink.g, p.ink.b, c.a), inked);
					}
					r = c.r;
					g = c.g;
					b = c.b;
					a = c.a / 255. * cover;
				}
				const double sa = shadow * (1 - a);
				const double total = a + sa;
				if (total <= 0)
				{
					line[x] = 0;
					continue;
				}
				// Straight alpha: the shadow is black, so it only darkens.
				auto ch = [&](double v) { return Uint32(std::clamp(std::lround(v * a / total), 0L, 255L)); };
				line[x] = (Uint32(std::lround(total * 255)) << 24) | (ch(r) << 16) | (ch(g) << 8) | ch(b);
			}
		}
		if (cache->entries.size() >= 96)
			cache->entries.erase(cache->entries.begin());
		cache->entries.push_back({key, -margin, -margin, pw / density, ph / density,
								  std::make_unique<DrawableSurface>(pixels, DrawableSurface::AdoptPixels{})});
		found = std::prev(cache->entries.end());
	}
	gfx->drawSurface(float(rect.x + found->x), float(rect.y + found->y), float(found->w), float(found->h),
					 found->image.get());
}
} // namespace TouchCard
