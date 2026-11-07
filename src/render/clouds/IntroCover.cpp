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

//! Room for a player's team-colour dot before their name.
int dotSpace(Font *font) { return font->getStringHeight("Ag") * 3 / 4; }

unsigned char unitToByte(float v) { return static_cast<unsigned char>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); }

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

//! Billowy noise: the sum of |noise| over octaves, in about [0, 1]. Each
//! octave's zero crossings become sharp creases between rounded puffs, the
//! cauliflower look of cumulus seen from above.
float puffs(float x, float y, float unit, int z)
{
	float sum = 0, amplitude = 1, norm = 0, scale = 256 / unit;
	for (int octave = 0; octave < 4; ++octave)
	{
		const float o = 3000.f + 1000.f * octave;
		const float n = (SimplexNoise::getNoise3D(int((x + o) * scale), int((y + o) * scale), z + 53 * octave) - 128) / 128.f;
		sum += amplitude * std::fabs(n);
		norm += amplitude;
		amplitude *= .5f;
		scale *= 2.1f;
	}
	return std::min(1.f, 1.6f * sum / norm);
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

void IntroCover::computeLayers(int w, int h, double seconds, float part, Layers &out, const Depth &depth)
{
	const float s = std::min(w, h) / 540.f;               // offsets and drift scale with the screen
	// The shadow falls down and to the right; the light comes from up and left.
	const float shadowX = 30 * s, shadowY = 40 * s, lightX = 7 * s, lightY = 9 * s;
	part = std::min(1.f, part * depth.lead);

	// Sample the noise on a screen-space lattice, with margins for the
	// offsets. The view drops through the bank as it parts, so the layer grows
	// from the centre of the screen; it drifts with the wind throughout.
	const int cell = std::max(5, std::min(w, h) / 110);
	const float margin = shadowY + lightY + cell;
	const float ox = -margin, oy = -margin;
	const int cw = int((w + 2 * margin) / cell) + 3, ch = int((h + 2 * margin) / cell) + 3;
	const float cx = w / 2.f, cy = h / 2.f;
	const float k = std::exp(depth.zoom * part * part);
	const float windX = float(22 * s * seconds * depth.wind), windY = float(9 * s * seconds * depth.wind);
	const float unit = .45f * depth.size * std::min(w, h);
	const int z = int(seconds * 8) + depth.seed;
	std::vector<float> ranks(size_t(cw) * ch), heights(size_t(cw) * ch);
	for (int j = 0; j < ch; ++j)
		for (int i = 0; i < cw; ++i)
		{
			const float x = ox + float(i) * cell, y = oy + float(j) * cell;
			const float nx = cx + (x - cx) / k + windX + 5000.f * depth.seed, ny = cy + (y - cy) / k + windY;
			// Puffs both shade the cloud and bite into its edges.
			const float h = puffs(nx, ny, .3f * unit, z);
			heights[size_t(j) * cw + i] = h;
			ranks[size_t(j) * cw + i] = billows(nx, ny, unit, z) + .35f * h;
		}
	// Equalise to ranks in [0, 1]: raising the threshold then clears sky at a
	// steady rate, whatever the noise's distribution.
	std::vector<float> sorted(ranks);
	std::sort(sorted.begin(), sorted.end());
	const float n = float(sorted.size());
	for (float &v : ranks)
		v = float(std::lower_bound(sorted.begin(), sorted.end(), v) - sorted.begin()) / n;
	// A lattice field at a screen position, interpolated between lattice points.
	const auto sample = [&](const std::vector<float> &field, float x, float y)
	{
		const float fx = std::clamp((x - ox) / cell, 0.f, float(cw - 1.001f));
		const float fy = std::clamp((y - oy) / cell, 0.f, float(ch - 1.001f));
		const int i = int(fx), j = int(fy);
		const float tx = fx - i, ty = fy - j;
		const float *row = &field[size_t(j) * cw + i];
		return (row[0] * (1 - tx) + row[1] * tx) * (1 - ty) + (row[cw] * (1 - tx) + row[cw + 1] * tx) * ty;
	};
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
			const float th = -.55f + 1.75f * part + (1 - std::hypot(x - cx, y - cy) / maxDistance) * bias;
			const float u = rank(x, y);
			// Thin, translucent fringes; dense where the cloud is deep.
			// Density thins gradually into wisps rather than ending at an edge.
			const float a = smoothstep(th - .15f, th + .35f, u);
			const float shadow = smoothstep(th - .15f, th + .35f, rank(x - shadowX, y - shadowY));
			// Light from the upper left wraps over each puff: the side facing
			// it is bright, the far side and the creases are softly shaded,
			// and thin edges are greyer than the deep middle.
			const float hc = sample(heights, x, y);
			const float slope = sample(heights, x + lightX, y + lightY) - sample(heights, x - lightX, y - lightY);
			const float tone = std::clamp(.5f + .3f * hc + 1.2f * slope + .25f * smoothstep(th, th + .5f, u) - .1f,
			                              0.f, 1.f);
			const size_t at = size_t(j) * out.w + i;
			out.shadow[at] = unitToByte(.35f * shadow);
			out.body[at] = unitToByte(a);
			out.light[at] = unitToByte(a * tone);
		}
}

void IntroCover::computeLayers(int w, int h, double seconds, float part, Layers &out)
{
	computeLayers(w, h, seconds, part, out, Depth());
}

IntroCover::TitleLayout IntroCover::layoutTitle(int w, int h, Font *titleFont, Font *playerFont) const
{
	TitleLayout layout;
	if (!titleFont || title.map.empty())
		return layout;
	// Lay the player names out in centred rows that fit the screen.
	layout.gap = playerFont ? std::max(18, playerFont->getStringWidth("    ")) : 0;
	layout.rows.emplace_back();
	layout.widths.push_back(0);
	if (playerFont)
		for (const Player &player : title.players)
		{
			const int pw = dotSpace(playerFont) + playerFont->getStringWidth(player.name);
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
	layout.blockH = layout.titleH + (playersH ? 14 + playersH : 0);
	layout.top = int(h * .4f) - layout.blockH / 2;
	return layout;
}

void IntroCover::draw(DrawableSurface &gfx, int w, int h, Uint64 now, Font *titleFont, Font *playerFont, bool clouds)
{
	if (!active(now) || w <= 0 || h <= 0)
		return;
	const float p = ease(part(now));
	const float titleAlpha = (1 - ease(p / .18f)) * ease(float(now - start) / 250.f);
	const TitleLayout layout = layoutTitle(w, h, titleFont, playerFont);
	if (!clouds)
	{
		// Without cloud layers (the software renderer) or with reduced motion, a plain sky fades out.
		gfx.drawFilledRect(0, 0, w, h, 214, 220, 232, Uint8(std::lround(255 * (1 - p))));
		gfx.drawFilledRect(0, 0, w, h, 28, 38, 52, Uint8(std::lround(90 * titleAlpha)));
	}
	else
	{
		const double seconds = double(now - start) / 1000;
		computeLayers(w, h, seconds, p, layers);
		Depth far;
		far.zoom = .25f;
		far.lead = 1;
		far.wind = .55f;
		far.size = .7f;
		far.seed = 7;
		computeLayers(w, h, seconds, p, farLayers, far);
		if (!surface || surface->getW() != layers.w || surface->getH() != layers.h)
		{
			surface = std::make_unique<SmoothSurface>(layers.w, layers.h);
		}
		// Composite both layers (each a shadow, then cloud graded from shaded
		// creases to sunlit tops) and the title's shade into one straight-alpha
		// texture, as if each were drawn over the last.
		SDL_Surface *pixels = surface->getSDLSurface();
		const float hx = layout.blockW / 2.f + .12f * w, hy = layout.blockH / 2.f + .12f * h;
		const float hcx = w / 2.f, hcy = layout.top + layout.blockH / 2.f;
		for (int j = 0; j < layers.h; ++j)
		{
			auto *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels->pixels) + size_t(j) * pixels->pitch);
			for (int i = 0; i < layers.w; ++i)
			{
				const size_t at = size_t(j) * layers.w + i;
				float a = 0, r = 0, g = 0, b = 0; // premultiplied
				const auto over = [&](float alpha, float cr, float cg, float cb)
				{
					r = r * (1 - alpha) + cr * alpha;
					g = g * (1 - alpha) + cg * alpha;
					b = b * (1 - alpha) + cb * alpha;
					a = a + alpha * (1 - a);
				};
				const auto cloud = [&](const Layers &l, float haze, float opacity)
				{
					const float body = l.body[at] / 255.f * opacity;
					if (body <= 0)
						return;
					const float t = l.light[at] / 255.f / (body / opacity);
					// Crease blue-grey, through a cool mid tone, to warm sunlit white.
					float cr, cg, cb;
					if (t < .5f)
					{
						const float q = t / .5f;
						cr = 176 + (218 - 176) * q, cg = 182 + (222 - 182) * q, cb = 196 + (230 - 196) * q;
					}
					else
					{
						const float q = (t - .5f) / .5f;
						cr = 218 + (250 - 218) * q, cg = 222 + (250 - 222) * q, cb = 230 + (248 - 230) * q;
					}
					cr += (176 - cr) * haze, cg += (188 - cg) * haze, cb += (212 - cb) * haze;
					over(body, cr, cg, cb);
				};
				// Two translucent layers hide about 90% of the map while the
				// renderer settles underneath, then fly past.
				over(farLayers.shadow[at] / 255.f * .35f, 20, 28, 44);
				cloud(farLayers, .3f, .67f * (1 - .5f * p));
				over(layers.shadow[at] / 255.f * .5f, 20, 28, 44);
				cloud(layers, 0, .7f * (1 - .5f * p));
				if (titleAlpha > 0 && layout.blockW)
				{
					const float x = (i + .5f) * w / layers.w, y = (j + .5f) * h / layers.h;
					over(.32f * titleAlpha * smoothstep(1.2f, .2f, std::hypot((x - hcx) / hx, (y - hcy) / hy)), 52, 64, 88);
				}
				const float inv = a > 0 ? 1 / a : 0;
				row[i] = Uint32(unitToByte(a)) << 24 | Uint32(std::lround(std::min(255.f, r * inv))) << 16 |
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
	// Soft white letters: a wide, faint shadow stands in for a blur.
	const auto shadowed = [&](Font *font, int x, int y, const std::string &text, Uint8 textAlpha)
	{
		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 10, 16, 28));
		for (auto [dx, dy] : {std::pair{0, 1}, std::pair{1, 2}, std::pair{-1, 2}, std::pair{0, 3}, std::pair{2, 3},
		                      std::pair{-2, 3}, std::pair{0, 4}})
			gfx.drawString(x + dx, y + dy, font, text, 0, Uint8(textAlpha / 5));
		font->popStyle();
		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 255, 255));
		gfx.drawString(x, y, font, text, 0, textAlpha);
		font->popStyle();
	};
	shadowed(titleFont, (w - layout.titleW) / 2, layout.top, title.map, a);
	if (!playerFont)
		return;
	const int dot = dotSpace(playerFont);
	const int fontH = playerFont->getStringHeight("Ag");
	int y = layout.top + layout.titleH + 14;
	for (size_t r = 0; r < layout.rows.size(); ++r, y += layout.rowH)
	{
		int x = (w - layout.widths[r]) / 2;
		for (const Player *player : layout.rows[r])
		{
			// A small round dot in the team's colour, then the name in white.
			const float radius = dot * .28f, dcx = x + dot * .35f, dcy = y + fontH * .55f;
			for (int dy = -int(radius) - 1; dy <= int(radius) + 1; ++dy)
			{
				const float half = std::sqrt(std::max(0.f, radius * radius - float(dy * dy)));
				if (half > 0)
				{
					gfx.drawFilledRect(int(std::lround(dcx - half)), int(dcy) + dy + 1, int(std::lround(2 * half)), 1, 10, 16,
					                   28, Uint8(a / 3));
					gfx.drawFilledRect(int(std::lround(dcx - half)), int(dcy) + dy, int(std::lround(2 * half)), 1, player->r,
					                   player->g, player->b, a);
				}
			}
			shadowed(playerFont, x + dot, y, player->name, Uint8(a * 4 / 5));
			x += dot + playerFont->getStringWidth(player->name) + layout.gap;
		}
	}
}
