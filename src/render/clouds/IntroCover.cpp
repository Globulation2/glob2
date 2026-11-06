// SPDX-License-Identifier: GPL-3.0-or-later
#include "IntroCover.h"

#include "GraphicContext.h"
#include "SimplexNoise.h"

#include <algorithm>
#include <cmath>

using namespace GAGCore;

namespace
{
constexpr float Pi = 3.14159265f;

float smoothstep(float e0, float e1, float x)
{
	const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
	return t * t * (3 - 2 * t);
}

float ease(float t)
{
	t = std::clamp(t, 0.f, 1.f);
	return t * t * (3 - 2 * t);
}

unsigned char byte(float v) { return static_cast<unsigned char>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); }

//! A texture sampled smoothly when stretched, like generated HD terrain pages.
class SmoothSurface : public DrawableSurface
{
public:
	SmoothSurface(int w, int h) : DrawableSurface(w, h) { highResolutionSampling = true; }
};

int texelsFor(int pixels)
{
	// A power of two, so the texture samples smoothly with mipmaps everywhere.
	int size = 64;
	while (size < pixels / 3 && size < 512)
		size *= 2;
	return size;
}

//! Five octaves of the game's own simplex noise, roughly in [-1, 1]. `unit` is
//! the size in pixels of one lattice cell of the largest octave.
float billows(float x, float y, float unit, int z)
{
	float sum = 0, amplitude = 1, norm = 0, scale = 256 / unit;
	for (int octave = 0; octave < 5; ++octave)
	{
		// Offset each octave so their lattices do not line up.
		const float o = 1000.f * octave;
		sum += amplitude * (SimplexNoise::getNoise3D(int((x + o) * scale), int((y - o) * scale), z + 97 * octave) - 128) / 128.f;
		norm += amplitude;
		amplitude *= .55f;
		scale *= 2;
	}
	return sum / norm;
}
}

IntroCover::IntroCover() = default;
IntroCover::~IntroCover() = default;
IntroCover::IntroCover(IntroCover &&) noexcept = default;
IntroCover &IntroCover::operator=(IntroCover &&) noexcept = default;

void IntroCover::reset()
{
	*this = IntroCover();
}

void IntroCover::begin(Uint64 now, Title title)
{
	*this = IntroCover();
	start = std::max<Uint64>(now, 1);
	this->title = std::move(title);
}

void IntroCover::skip(Uint64 now)
{
	// Part from here, a little faster than usual.
	if (!start || now - start >= partFrom)
		return;
	partFrom = Uint32(now - start);
	end = partFrom + PART_MS / 2;
}

float IntroCover::part(Uint64 now) const
{
	if (!start || now - start < partFrom)
		return 0;
	return std::clamp(float(now - start - partFrom) / float(end - partFrom), 0.f, 1.f);
}

void IntroCover::computeLayers(int w, int h, double seconds, float part, Layers &out)
{
	const float s = std::min(w, h) / 540.f;               // offsets and drift scale with the screen
	// The shadow falls down and to the right; the light comes from up and left.
	const float shadowX = 34 * s, shadowY = 44 * s, lightX = 10 * s, lightY = 12 * s;

	// Sample the noise on a coarse screen-space lattice, with margins for both
	// offsets. The bank slides outward from the centre of the screen as it
	// parts, and drifts with the wind throughout.
	const int cell = std::max(8, std::min(w, h) / 60);
	const float ox = -shadowX - cell, oy = -shadowY - cell;
	const int cw = int((w - ox + lightX) / cell) + 3, ch = int((h - oy + lightY) / cell) + 3;
	const float cx = w / 2.f, cy = h / 2.f;
	const float k = 1 + .9f * part;
	const float windX = float(22 * s * seconds), windY = float(9 * s * seconds);
	const float unit = .6f * std::min(w, h);
	const int z = int(seconds * 24);
	std::vector<float> ranks(size_t(cw) * ch);
	for (int j = 0; j < ch; ++j)
		for (int i = 0; i < cw; ++i)
		{
			const float x = ox + float(i) * cell, y = oy + float(j) * cell;
			ranks[size_t(j) * cw + i] = billows(cx + (x - cx) / k + windX, cy + (y - cy) / k + windY, unit, z);
		}
	// Equalise to ranks in [0, 1]: raising the threshold then clears sky at a
	// steady rate, whatever the noise's distribution.
	std::vector<float> sorted(ranks);
	std::sort(sorted.begin(), sorted.end());
	const float n = float(sorted.size());
	for (float &v : ranks)
		v = float(std::lower_bound(sorted.begin(), sorted.end(), v) - sorted.begin()) / n;
	// The rank at a screen position, interpolated between lattice points.
	const auto rank = [&](float x, float y)
	{
		const float fx = std::clamp((x - ox) / cell, 0.f, float(cw - 1.001f));
		const float fy = std::clamp((y - oy) / cell, 0.f, float(ch - 1.001f));
		const int i = int(fx), j = int(fy);
		const float tx = fx - i, ty = fy - j;
		const float *row = &ranks[size_t(j) * cw + i];
		return (row[0] * (1 - tx) + row[1] * tx) * (1 - ty) + (row[cw] * (1 - tx) + row[cw + 1] * tx) * ty;
	};

	// The threshold is applied per texel of a small texture that is drawn
	// stretched with smooth filtering, so cloud edges are curves, not steps.
	out.w = texelsFor(w);
	out.h = texelsFor(h);
	out.shadow.resize(size_t(out.w) * out.h);
	out.body.resize(size_t(out.w) * out.h);
	out.light.resize(size_t(out.w) * out.h);
	const float maxDistance = std::hypot(cx, cy);
	const float bias = .18f * std::sin(Pi * part);
	for (int j = 0; j < out.h; ++j)
		for (int i = 0; i < out.w; ++i)
		{
			const float x = (i + .5f) * w / out.w, y = (j + .5f) * h / out.h;
			// The centre clears first; the threshold passes every rank by part = 1.
			const float th = -.16f + 1.2f * part + (1 - std::hypot(x - cx, y - cy) / maxDistance) * bias;
			const float u = rank(x, y);
			const float a = smoothstep(th - .03f, th + .14f, u);
			const float shadow = smoothstep(th - .03f, th + .14f, rank(x - shadowX, y - shadowY));
			const float lit = std::clamp((u - rank(x + lightX, y + lightY)) * 5 + .72f, 0.f, 1.f);
			const size_t at = size_t(j) * out.w + i;
			out.shadow[at] = byte(.4f * shadow);
			out.body[at] = byte(a);
			out.light[at] = byte(a * lit);
		}
}

IntroCover::TitleLayout IntroCover::layoutTitle(int w, int h, Font *titleFont, Font *playerFont) const
{
	TitleLayout layout;
	if (!titleFont || title.map.empty())
		return layout;
	// Lay the player names out in centred rows that fit the screen.
	layout.gap = playerFont ? std::max(12, playerFont->getStringWidth("  ")) : 0;
	layout.rows.emplace_back();
	layout.widths.push_back(0);
	if (playerFont)
		for (const Player &player : title.players)
		{
			const int pw = playerFont->getStringWidth(player.name);
			if (!layout.rows.back().empty() && layout.widths.back() + layout.gap + pw > w * 9 / 10)
			{
				layout.rows.emplace_back();
				layout.widths.push_back(0);
			}
			layout.widths.back() += (layout.rows.back().empty() ? 0 : layout.gap) + pw;
			layout.rows.back().push_back(&player);
		}
	layout.titleW = titleFont->getStringWidth(title.map);
	layout.titleH = titleFont->getStringHeight(title.map);
	layout.rowH = playerFont ? playerFont->getStringHeight("Ag") + 4 : 0;
	const int playersH = title.players.empty() ? 0 : int(layout.rows.size()) * layout.rowH;
	layout.blockW = std::max(layout.titleW, *std::max_element(layout.widths.begin(), layout.widths.end()));
	layout.blockH = layout.titleH + (playersH ? 10 + playersH : 0);
	layout.top = int(h * .4f) - layout.blockH / 2;
	return layout;
}

void IntroCover::draw(DrawableSurface &gfx, int w, int h, Uint64 now, Font *titleFont, Font *playerFont, bool clouds)
{
	if (!active(now) || w <= 0 || h <= 0)
		return;
	const float p = ease(part(now));
	const float titleAlpha = (1 - ease(p / .22f)) * ease(float(now - start) / 500.f);
	const TitleLayout layout = layoutTitle(w, h, titleFont, playerFont);
	if (!clouds)
	{
		// Without cloud layers (the software renderer) or with reduced motion, a plain sky fades out.
		gfx.drawFilledRect(0, 0, w, h, 214, 220, 232, Uint8(std::lround(255 * (1 - p))));
		gfx.drawFilledRect(0, 0, w, h, 28, 38, 52, Uint8(std::lround(90 * titleAlpha)));
	}
	else
	{
		computeLayers(w, h, double(now - start) / 1000, p, layers);
		if (!surface || surface->getW() != layers.w || surface->getH() != layers.h)
		{
			surface = std::make_unique<SmoothSurface>(layers.w, layers.h);
		}
		// Composite shadow, underside, sunlit tops and the title's dark halo into
		// one straight-alpha texture, as if each were drawn over the last.
		SDL_Surface *pixels = surface->getSDLSurface();
		const float hx = layout.blockW / 2.f + .06f * w, hy = layout.blockH / 2.f + .06f * h;
		const float hcx = w / 2.f, hcy = layout.top + layout.blockH / 2.f;
		for (int j = 0; j < layers.h; ++j)
		{
			auto *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels->pixels) + size_t(j) * pixels->pitch);
			for (int i = 0; i < layers.w; ++i)
			{
				const size_t at = size_t(j) * layers.w + i;
				float a = layers.shadow[at] / 255.f, r = 0, g = 0, b = 0;       // premultiplied
				const auto over = [&](float alpha, float cr, float cg, float cb)
				{
					r = r * (1 - alpha) + cr * alpha;
					g = g * (1 - alpha) + cg * alpha;
					b = b * (1 - alpha) + cb * alpha;
					a = a + alpha * (1 - a);
				};
				over(layers.body[at] / 255.f, 168, 178, 198);
				over(layers.light[at] / 255.f, 253, 253, 255);
				if (titleAlpha > 0 && layout.blockW)
				{
					const float x = (i + .5f) * w / layers.w, y = (j + .5f) * h / layers.h;
					over(.7f * titleAlpha * smoothstep(1.3f, .35f, std::hypot((x - hcx) / hx, (y - hcy) / hy)), 24, 32, 46);
				}
				const float inv = a > 0 ? 1 / a : 0;
				row[i] = Uint32(byte(a)) << 24 | Uint32(std::lround(std::min(255.f, r * inv))) << 16 |
				         Uint32(std::lround(std::min(255.f, g * inv))) << 8 | Uint32(std::lround(std::min(255.f, b * inv)));
			}
		}
		surface->markPixelsChanged();
		gfx.drawSurface(0, 0, w, h, surface.get());
	}
	if (titleAlpha > 0)
		drawTitle(gfx, w, layout, titleAlpha, titleFont, playerFont);
}

void IntroCover::drawTitle(DrawableSurface &gfx, int w, const TitleLayout &layout, float alpha, Font *titleFont,
                           Font *playerFont)
{
	if (!titleFont || title.map.empty())
		return;
	const Uint8 a = Uint8(std::lround(255 * alpha));
	// A dark outline and drop shadow keep the letters readable on bright cloud.
	const auto outlined = [&](Font *font, int x, int y, const std::string &text, Color colour)
	{
		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 16, 22, 32));
		gfx.drawString(x + 2, y + 2, font, text, 0, Uint8(a / 2));
		for (auto [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}})
			gfx.drawString(x + dx, y + dy, font, text, 0, Uint8(a * 3 / 4));
		font->popStyle();
		font->pushStyle(Font::Style(Font::STYLE_NORMAL, colour));
		gfx.drawString(x, y, font, text, 0, a);
		font->popStyle();
	};
	outlined(titleFont, (w - layout.titleW) / 2, layout.top, title.map, Color(255, 255, 255));
	if (!playerFont)
		return;
	int y = layout.top + layout.titleH + 10;
	for (size_t r = 0; r < layout.rows.size(); ++r, y += layout.rowH)
	{
		int x = (w - layout.widths[r]) / 2;
		for (const Player *player : layout.rows[r])
		{
			outlined(playerFont, x, y, player->name, Color(player->r, player->g, player->b));
			x += playerFont->getStringWidth(player->name) + layout.gap;
		}
	}
}
