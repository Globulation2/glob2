// SPDX-License-Identifier: GPL-3.0-or-later

#include "Glob2Test.h"
#include "IntroCover.h"

#include <GraphicContext.h>
#include <SDL3/SDL_surface.h>
#include <Toolkit.h>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
double mean(const std::vector<unsigned char> &v)
{
	double sum = 0;
	for (unsigned char c : v)
		sum += c;
	return v.size() ? sum / double(v.size()) : 0;
}

//! A colour with premultiplied channels in [0, 1].
struct Premultiplied
{
	double r = 0, g = 0, b = 0, a = 0;
};

double ramp(double e0, double e1, double x)
{
	const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
	return t * t * (3 - 2 * t);
}

//! The bank at screen position (x, y), worked out from the frame's lattice by
//! the rules IntroCover documents, independently of both renderers.
Premultiplied reference(const GAGCore::CloudCoverFrame &f, double x, double y)
{
	const auto sample = [&](double px, double py, int channel)
	{
		const double fx = std::clamp((px - f.originX) / f.cell, 0.0, double(f.latticeW - 1));
		const double fy = std::clamp((py - f.originY) / f.cell, 0.0, double(f.latticeH - 1));
		const int i = std::min(int(fx), f.latticeW - 2), j = std::min(int(fy), f.latticeH - 2);
		const double tx = fx - i, ty = fy - j;
		const auto at = [&](int ii, int jj) { return f.lattice[(size_t(jj) * f.latticeW + ii) * 4 + channel] / 65535.0; };
		return (at(i, j) * (1 - tx) + at(i + 1, j) * tx) * (1 - ty) + (at(i, j + 1) * (1 - tx) + at(i + 1, j + 1) * tx) * ty;
	};
	Premultiplied c;
	const auto over = [&](double alpha, double r, double g, double b)
	{
		c.r = c.r * (1 - alpha) + r / 255 * alpha;
		c.g = c.g * (1 - alpha) + g / 255 * alpha;
		c.b = c.b * (1 - alpha) + b / 255 * alpha;
		c.a = c.a + alpha * (1 - c.a);
	};
	const double centre = 1 - std::hypot(x - f.centreX, y - f.centreY) / f.maxDistance;
	const auto layer = [&](const GAGCore::CloudCoverFrame::Layer &l, int rankChannel)
	{
		const double th = l.threshold + centre * l.bias;
		const double rank = sample(x, y, rankChannel);
		over(.35 * ramp(th - .15, th + .35, sample(x - f.shadowX, y - f.shadowY, rankChannel)) * l.shadow, 20, 28, 44);
		const double slope = sample(x + f.lightX, y + f.lightY, rankChannel + 1) - sample(x - f.lightX, y - f.lightY, rankChannel + 1);
		const double tone = std::clamp(.5 + .3 * sample(x, y, rankChannel + 1) + 1.2 * slope + .25 * ramp(th, th + .5, rank) - .1, 0.0, 1.0);
		double r, g, b;
		if (tone < .5)
			r = 176 + 42 * tone * 2, g = 182 + 40 * tone * 2, b = 196 + 34 * tone * 2;
		else
			r = 218 + 32 * (tone * 2 - 1), g = 222 + 28 * (tone * 2 - 1), b = 230 + 18 * (tone * 2 - 1);
		r += (176 - r) * l.haze, g += (188 - g) * l.haze, b += (212 - b) * l.haze;
		over(ramp(th - .15, th + .35, rank) * l.opacity, r, g, b);
	};
	layer(f.farLayer, 2);
	layer(f.nearLayer, 0);
	if (f.shadeAlpha > 0)
		over(.32 * f.shadeAlpha * ramp(1.2, .2, std::hypot((x - f.shadeX) / f.shadeRX, (y - f.shadeY) / f.shadeRY)), 52, 64, 88);
	return c;
}

//! Both layers' lattices for a w x h screen, `seconds` in, with the bank at `part`.
void lattices(int w, int h, double seconds, float part, IntroCover::Lattice &nearLattice, IntroCover::Lattice &farLattice)
{
	IntroCover::computeLattice(w, h, seconds, part, nearLattice, IntroCover::nearDepth());
	IntroCover::computeLattice(w, h, seconds, part, farLattice, IntroCover::farDepth());
}

IntroCover::Shade titleShade(int w, int h)
{
	IntroCover::Shade shade;
	shade.alpha = .8f;
	shade.cx = w / 2.f;
	shade.cy = h * .4f;
	shade.rx = w * .3f;
	shade.ry = h * .15f;
	return shade;
}
}

TEST_SUITE("IntroCover")
{
TEST_CASE("three seconds: covered, then parting; a skip parts at once")
{
	IntroCover cover;
	const Uint64 t0 = 10000;
	REQUIRE_FALSE(cover.active(t0));
	REQUIRE_FALSE(cover.blocksInput(t0));
	cover.begin(t0, {"Map", {}});
	REQUIRE(cover.active(t0));
	REQUIRE(cover.blocksInput(t0));
	REQUIRE(cover.part(t0 + IntroCover::COVER_MS - 1) == 0);
	REQUIRE(cover.part(t0 + IntroCover::COVER_MS + IntroCover::PART_MS / 2) == doctest::Approx(.5f).epsilon(.01));
	REQUIRE_FALSE(cover.blocksInput(t0 + IntroCover::COVER_MS + IntroCover::PART_MS / 2));
	REQUIRE(cover.active(t0 + IntroCover::TOTAL_MS - 1));
	REQUIRE_FALSE(cover.active(t0 + IntroCover::TOTAL_MS));

	// The title is there at full strength from the first frame, like the bank.
	REQUIRE(cover.titleOpacity(t0) == 1.f);
	REQUIRE(cover.titleOpacity(t0 + IntroCover::COVER_MS - 1) == 1.f);
	REQUIRE(cover.titleOpacity(t0 + IntroCover::COVER_MS + IntroCover::PART_MS / 2) == 0.f);
	REQUIRE(cover.titleOpacity(t0 + IntroCover::TOTAL_MS) == 0.f);

	IntroCover skipped;
	skipped.begin(t0, {"Map", {}});
	skipped.skip(t0 + 300);
	REQUIRE(skipped.part(t0 + 300) == 0);
	REQUIRE(skipped.part(t0 + 300 + IntroCover::PART_MS / 4) > .4f);
	REQUIRE_FALSE(skipped.active(t0 + 300 + IntroCover::PART_MS / 2));
}

TEST_CASE("the bank is opaque when closed, thins steadily and is gone when parted")
{
	for (auto [w, h] : {std::pair{960, 540}, std::pair{1920, 1080}, std::pair{390, 844}})
	{
		IntroCover::Layers closed, middle, open, again;
		IntroCover::computeLayers(w, h, 1.25, 0.f, closed);
		// A power-of-two texture of at least a third of the screen's resolution (up to 512).
		for (int texels : {closed.w, closed.h})
			REQUIRE((texels >= 64 && texels <= 512 && (texels & (texels - 1)) == 0));
		REQUIRE((closed.w * 3 >= std::min(w, 1536) && closed.h * 3 >= std::min(h, 1536)));
		REQUIRE(closed.body.size() == size_t(closed.w) * closed.h);
		for (unsigned char a : closed.body)
			REQUIRE(a == 255);

		IntroCover::computeLayers(w, h, 1.25, 1.f, open);
		for (size_t i = 0; i < open.body.size(); ++i)
		{
			REQUIRE(open.body[i] == 0);
			REQUIRE(open.light[i] == 0);
			REQUIRE(open.shadow[i] == 0);
		}

		// Coverage falls steadily as the bank parts.
		double previous = 256;
		for (float part : {0.f, .2f, .4f, .6f, .8f, 1.f})
		{
			IntroCover::Layers l;
			IntroCover::computeLayers(w, h, 2.0, part, l);
			const double coverage = mean(l.body);
			REQUIRE(coverage <= previous);
			previous = coverage;
		}

		IntroCover::computeLayers(w, h, 3.0, .4f, middle);
		for (size_t i = 0; i < middle.body.size(); ++i)
			REQUIRE(middle.light[i] <= middle.body[i]);

		// Pure: the same inputs give the same lattice.
		IntroCover::computeLayers(w, h, 3.0, .4f, again);
		REQUIRE(again.body == middle.body);
		REQUIRE(again.shadow == middle.shadow);
	}
}

TEST_CASE("lattice ranks are an equalised order and heights stay in range")
{
	IntroCover::Lattice lattice, again;
	IntroCover::computeLattice(960, 540, 1.4, .3f, lattice, IntroCover::nearDepth());
	REQUIRE(lattice.ranks.size() == size_t(lattice.cw) * lattice.ch);
	std::vector<float> sorted(lattice.ranks);
	std::sort(sorted.begin(), sorted.end());
	REQUIRE(sorted.front() == 0.f);
	REQUIRE(sorted.back() < 1.f);
	// Ranks spread evenly over [0, 1): the median rank is about a half.
	REQUIRE(sorted[sorted.size() / 2] == doctest::Approx(.5f).epsilon(.01));
	for (float h : lattice.heights)
		REQUIRE((h >= 0 && h <= 1));
	IntroCover::computeLattice(960, 540, 1.4, .3f, again, IntroCover::nearDepth());
	REQUIRE(again.ranks == lattice.ranks);
	REQUIRE(again.heights == lattice.heights);
}

TEST_CASE("the texture matches the per-pixel bank at its texels")
{
	// The texture fallback (computeLayers and composite, rounded to bytes on
	// the way) and the per-pixel frame describe the same bank.
	const int w = 960, h = 540;
	for (float part : {0.f, .35f, .7f})
	{
		IntroCover::Lattice nearLattice, farLattice;
		lattices(w, h, 1.6, part, nearLattice, farLattice);
		IntroCover::Layers nearLayers, farLayers;
		IntroCover::computeLayers(nearLattice, IntroCover::threshold(part, IntroCover::nearDepth()), nearLayers);
		IntroCover::computeLayers(farLattice, IntroCover::threshold(part, IntroCover::farDepth()), farLayers);
		const IntroCover::Shade shade = titleShade(w, h);
		std::vector<Uint32> texels(size_t(nearLayers.w) * nearLayers.h);
		IntroCover::composite(w, h, part, nearLayers, farLayers, shade, texels.data(), nearLayers.w * 4);
		std::vector<Uint16> packed;
		const GAGCore::CloudCoverFrame frame = IntroCover::perPixelFrame(nearLattice, farLattice, part, shade, packed);
		REQUIRE(frame.lattice);

		double worst = 0, total = 0;
		for (int j = 0; j < nearLayers.h; ++j)
			for (int i = 0; i < nearLayers.w; ++i)
			{
				const Premultiplied expected = reference(frame, (i + .5) * w / nearLayers.w, (j + .5) * h / nearLayers.h);
				const Uint32 texel = texels[size_t(j) * nearLayers.w + i];
				const double a = (texel >> 24) / 255.0;
				const double measured[4] = {((texel >> 16) & 255) / 255.0 * a, ((texel >> 8) & 255) / 255.0 * a,
				                            (texel & 255) / 255.0 * a, a};
				const double wanted[4] = {expected.r, expected.g, expected.b, expected.a};
				for (int k = 0; k < 4; ++k)
				{
					const double error = std::fabs(measured[k] - wanted[k]) * 255;
					worst = std::max(worst, error);
					total += error;
				}
			}
		CAPTURE(part);
		REQUIRE(worst <= 4);
		REQUIRE(total / (4.0 * nearLayers.w * nearLayers.h) < .6);
	}
}

TEST_CASE("drawing on a plain surface covers it, then parts, with the lattices made on threads")
{
	// A plain surface cannot draw per pixel, so this is the texture path, fed
	// by the worker threads from prepare() on.
	const int w = 480, h = 270;
	GAGCore::DrawableSurface screen(w, h);
	IntroCover cover;
	const Uint64 t0 = 50000;
	cover.begin(t0, {"Map", {}});
	cover.prepare(screen, w, h);
	const auto brightness = [&]
	{
		double sum = 0;
		for (int y = 0; y < h; y += 9)
			for (int x = 0; x < w; x += 9)
			{
				Uint8 r = 0, g = 0, b = 0, a = 0;
				SDL_ReadSurfacePixel(screen.getSDLSurface(), x, y, &r, &g, &b, &a);
				sum += (r + g + b) / 3.0;
			}
		return sum / ((h / 9 + 1) * (w / 9 + 1));
	};
	double covered = 0, parted = 0;
	for (Uint64 t = t0; t < t0 + IntroCover::TOTAL_MS; t += 16)
	{
		screen.drawFilledRect(0, 0, w, h, 0, 0, 0, 255);
		cover.draw(screen, w, h, t, nullptr, nullptr, true);
		if (t - t0 < 800)
			covered = std::max(covered, brightness());
		if (t - t0 > IntroCover::TOTAL_MS - 100)
			parted = brightness();
	}
	// Closed, the bank is pale cloud; by the end, nearly all of the black screen shows.
	CHECK(covered > 150);
	CHECK(parted < 20);
	cover.reset();
}

#ifdef HAVE_OPENGL
TEST_CASE("the per-pixel bank on OpenGL matches the reference at every pixel [display]")
{
	glob2test::ToolkitScope toolkit;
	auto *gfx = GAGCore::Toolkit::initGraphic(480, 270, GAGCore::GraphicContext::USEGPU, "Cloud cover validation");
	REQUIRE(gfx);
	const int w = gfx->getW(), h = gfx->getH();
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	const int pw = viewport[2], ph = viewport[3];
	const double scale = double(pw) / w;

	for (float part : {0.f, .35f, .7f})
	{
		IntroCover::Lattice nearLattice, farLattice;
		lattices(w, h, 1.6, part, nearLattice, farLattice);
		std::vector<Uint16> packed;
		const GAGCore::CloudCoverFrame frame = IntroCover::perPixelFrame(nearLattice, farLattice, part, titleShade(w, h), packed);
		// Over black, a pixel reads as its premultiplied colour; over white less
		// black is 255 times one less its alpha.
		std::vector<Uint8> onBlack(size_t(pw) * ph * 4), onWhite(onBlack.size());
		for (int pass = 0; pass < 2; ++pass)
		{
			const Uint8 v = pass ? 255 : 0;
			gfx->drawFilledRect(0, 0, w, h, v, v, v, 255);
			REQUIRE((gfx->drawCloudCover(frame) && "GLSL 1.20 cloud cover program failed to compile or link"));
			glFinish();
			glReadPixels(0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, (pass ? onWhite : onBlack).data());
		}
		double worst = 0, total = 0;
		size_t offBy3 = 0;
		for (int py = 0; py < ph; ++py)
			for (int px = 0; px < pw; ++px)
			{
				// glReadPixels rows run up from the bottom of the window.
				const size_t at = (size_t(ph - 1 - py) * pw + px) * 4;
				const Premultiplied expected = reference(frame, (px + .5) / scale, (py + .5) / scale);
				const double wanted[4] = {expected.r, expected.g, expected.b, expected.a};
				for (int k = 0; k < 4; ++k)
				{
					const double measured = k < 3 ? onBlack[at + k] / 255.0 : 1 - (onWhite[at] - onBlack[at]) / 255.0;
					const double error = std::fabs(measured - wanted[k]) * 255;
					worst = std::max(worst, error);
					total += error;
					offBy3 += error > 3;
				}
			}
		CAPTURE(part);
		CAPTURE(worst);
		REQUIRE(total / (4.0 * pw * ph) < .8);
		// Interpolation on the GPU is coarser than in doubles; rare pixels may differ a little more.
		REQUIRE(offBy3 <= size_t(pw) * ph * 4 / 1000);
		REQUIRE(worst <= 8);
	}
}
#endif
}
