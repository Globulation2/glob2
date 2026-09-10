// SPDX-License-Identifier: GPL-3.0-or-later
// Validates the GPU unit shader against an independent CPU reference:
//   1. Per-pixel HSV hue shift, isolated from compositing, over every one of
//      the 1,792 poses (three fixed colors) and over the 12 game-team hues
//      plus the existing 16-hue test palette (one representative pose per
//      action/direction).
//   2. The sharp (single-pose) and motion-blur (multi-pose shutter) combined
//      base+team framebuffer result, against a CPU reference that composites
//      each pose in premultiplied space and blends poses in shutter order --
//      the same math the removed CPU composite cache used.
// Both backends' whole-shutter native fallback for an incomplete HD pack, and
// the GLOB2_DISABLE_UNIT_SHADER escape hatch, are covered by
// UnitTeamColorCacheTest.cpp instead, since they don't need pixel comparison.
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include "render/UnitAnimation.h"

using namespace GAGCore;

namespace
{
	struct InspectUnitSprite : Sprite
	{
		DrawableSurface *teamOrig(int index, bool experiment)
		{
			auto *r = experiment ? experimentRotated[index] : rotated[index];
			return r ? r->orig : nullptr;
		}
		DrawableSurface *baseImage(int index, bool experiment)
		{
			return experiment ? experimentImages[index] : images[index];
		}
	};

	Uint8 clampByte(double v) { return static_cast<Uint8>(std::min(255.0, std::max(0.0, std::round(v)))); }

	// Exact replica of DrawableSurface::shiftHSV, restricted to the hue-only
	// shift Sprite::applyTeamHueShift always uses (sat/lum deltas are zero).
	void referenceHueShift(Uint8 r, Uint8 g, Uint8 b, Uint8 a, float hueShift, Uint8 &or_, Uint8 &og_, Uint8 &ob_)
	{
		Color c(r, g, b, a);
		float h, s, v;
		c.getHSV(&h, &s, &v);
		h += hueShift;
		if (h >= 360.0f) h -= 360.0f;
		if (h < 0.0f) h += 360.0f;
		c.setHSV(h, s, v);
		or_ = c.r; og_ = c.g; ob_ = c.b;
	}

	// Renders the team layer alone (base absent) with the shader, scaled so one
	// source texture pixel lands on exactly one physical framebuffer pixel
	// (compensating for any HiDPI drawable/logical ratio -- see pixelScale),
	// and compares every source pixel's hue-shifted value against the CPU
	// reference. Background is opaque so alpha is checked via a second draw
	// against a contrasting background (alpha exactness -> identical RGB read
	// through two different backgrounds only when alpha is 0 or 255 exactly;
	// partially-transparent edge texels are checked by reading the blended
	// result against both and solving for straight alpha/color).
	void checkHueShiftLayer(GraphicContext *gfx, DrawableSurface *team, float hueShift, float pixelScale,
	                         double &totalError, int &maxError, size_t &channels, size_t &alphaMismatches)
	{
		const int w = team->getW(), h = team->getH();
		SDL_Surface *raw = team->getSDLSurface();
		SDL_LockSurface(raw);
		std::vector<Uint8> refR(w * h), refG(w * h), refB(w * h), refA(w * h);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				Uint8 r, g, b, a;
				SDL_GetRGBA(reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(raw->pixels) + y * raw->pitch)[x], raw->format, &r, &g, &b, &a);
				Uint8 or_, og_, ob_;
				referenceHueShift(r, g, b, a, hueShift, or_, og_, ob_);
				refR[y * w + x] = or_; refG[y * w + x] = og_; refB[y * w + x] = ob_; refA[y * w + x] = a;
			}
		SDL_UnlockSurface(raw);

		const float displayW = w / pixelScale, displayH = h / pixelScale;
		// Anchor the quad's bottom edge to the window's bottom: glReadPixels is
		// bottom-up (row 0 = window bottom, X is not flipped), so a (0,0)-
		// anchored draw in a window taller than the content reads back empty
		// space below it instead. Bottom-anchoring in Y (left-anchoring in X,
		// already true at x=0) makes a plain (0,0,w,h) physical read line up.
		const float ax = 0.0f, ay = gfx->getH() - displayH;
		// Two backgrounds (black, white) let each pixel's straight RGBA be
		// solved for exactly, including partially transparent edge texels.
		std::vector<Uint8> onBlack((w) * h * 4), onWhite(w * h * 4);
		for (int pass = 0; pass < 2; ++pass)
		{
			gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), pass ? 255 : 0, pass ? 255 : 0, pass ? 255 : 0);
			gfx->drawTeamColoredQuad(nullptr, team, ax, ay, displayW, displayH, 255, hueShift);
			glFinish();
			std::vector<Uint8> &dest = pass ? onWhite : onBlack;
			glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, dest.data());
		}
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				// glReadPixels rows are bottom-up; row 0 of our quad is at GL y=h-1.
				const int ry = h - 1 - y;
				const size_t p = static_cast<size_t>(ry) * w + x;
				const Uint8 expectedA = refA[y * w + x];
				// black = straight_c * a directly (the premultiplied value, 0..255
				// scale, no division): straight_c*a over black is exactly black.
				// white - black = 255*(1-a) gives alpha without dividing either.
				const int whiteMinusBlack = onWhite[p * 4] - onBlack[p * 4];
				const double a = 1.0 - whiteMinusBlack / 255.0;
				const Uint8 measuredA = clampByte(a * 255.0);
				if (std::abs(static_cast<int>(measuredA) - static_cast<int>(expectedA)) > 1)
					++alphaMismatches;
				for (int c = 0; c < 3; ++c)
				{
					const int black = onBlack[p * 4 + c];
					const Uint8 *ref = c == 0 ? &refR[y * w + x] : c == 1 ? &refG[y * w + x] : &refB[y * w + x];
					// Compare in premultiplied space: a fully or near-fully
					// transparent pixel's straight color barely affects the
					// visible result and is not what "matches" is checking.
					const double refPremult = *ref * (expectedA / 255.0);
					const int diff = std::abs(black - static_cast<int>(std::round(refPremult)));
					totalError += diff; ++channels;
					maxError = std::max(maxError, diff);
				}
			}
	}
}

int main()
{
	Toolkit::init("codex-glob2-unit-shader-test");
	auto *gfx = Toolkit::initGraphic(256, 256, GraphicContext::USEGPU, "Unit shader validation");
	assert(gfx->hasUnitShader() && "GLSL 1.20 unit shader failed to compile/link on this driver");

	// HiDPI: the drawable can be a multiple of the logical resolution: to
	// compare rendered pixels 1:1 against source texture pixels, sizes must be
	// scaled by drawable/logical, and reads done in drawable (physical) pixels.
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	const float pixelScale = static_cast<float>(viewport[2]) / gfx->getW();

	InspectUnitSprite sprite;
	assert(sprite.load("data/gfx/unit"));

	// The 12 in-game team hues (Team::setCorrectColor's wheel, Game.h's
	// TEAM_COLOR_HUE_DEGREES / numTeams) plus the existing 16-hue test palette.
	// hueShifts[i] is exactly what sprite.setBaseColor(colors[i]) then
	// sprite.teamHueShiftDegrees() would compute, so drawSprite's shader path
	// and this test's independent CPU reference agree on the shift used.
	std::vector<Color> colors;
	for (int i = 0; i < 12; ++i)
	{ Color c; c.setHSV(i * 30.0f, 0.8f, 0.9f); colors.push_back(c); }
	for (int i = 0; i < 16; ++i)
	{ Color c; c.setHSV(i * 360.0f / 16, 1.0f, 1.0f); colors.push_back(c); }
	std::vector<float> hueShifts;
	for (auto &c : colors)
	{
		sprite.setBaseColor(c);
		hueShifts.push_back(sprite.teamHueShiftDegrees());
	}

	double totalError = 0; int maxError = 0; size_t channels = 0, alphaMismatches = 0;

	// 1) Exhaustive: all 1,792 poses (every action/direction/phase), native and
	// HD, at three fixed colors (reused from the removed composite tests).
	for (bool experiment : {false, true})
	for (int base = 0; base <= 384; base += 64)
	for (int dir = 0; dir < 8; ++dir)
	for (int phase = 0; phase < 32; ++phase)
	{
		const int index = unitAnimationFrame(base, dir, phase * 8);
		DrawableSurface *team = sprite.teamOrig(index, experiment);
		if (!team) continue;
		for (float hueShift : {hueShifts[0], hueShifts[1], hueShifts[2]})
			checkHueShiftLayer(gfx, team, hueShift, pixelScale, totalError, maxError, channels, alphaMismatches);
	}

	// 2) Breadth: all 28 hues (12 game colors + 16-hue palette), one
	// representative pose per action/direction (56 poses), native and HD.
	for (bool experiment : {false, true})
	for (int base = 0; base <= 384; base += 64)
	for (int dir = 0; dir < 8; ++dir)
	{
		const int index = unitAnimationFrame(base, dir, 128);
		DrawableSurface *team = sprite.teamOrig(index, experiment);
		if (!team) continue;
		for (float hueShift : hueShifts)
			checkHueShiftLayer(gfx, team, hueShift, pixelScale, totalError, maxError, channels, alphaMismatches);
	}

	std::cout << "Shader vs CPU HSV: mean=" << (totalError / channels) << "/255 max=" << maxError
	          << "/255 alpha mismatches=" << alphaMismatches << " comparisons=" << channels / 3 << std::endl;
	assert(alphaMismatches == 0);
	assert(maxError <= 2);

	// 3) Sharp and motion-blur framebuffer comparison: shader-rendered sequence
	// vs. an independent CPU reference that composites base+team per pose in
	// premultiplied space (team over base, unpremultiplied once) and then
	// blends poses in shutter order -- the same algebra the removed CPU
	// composite cache used, just not cached.
	{
		auto compositePose = [&](DrawableSurface *base, DrawableSurface *team, int x, int y, float hueShift,
		                          std::vector<double> &canvas, int cw, int poseAlpha)
		{
			SDL_Surface *braw = base ? base->getSDLSurface() : nullptr;
			SDL_Surface *traw = team ? team->getSDLSurface() : nullptr;
			if (braw) SDL_LockSurface(braw);
			if (traw) SDL_LockSurface(traw);
			const int w = base ? base->getW() : team->getW();
			const int h = base ? base->getH() : team->getH();
			for (int py = 0; py < h; ++py)
				for (int px = 0; px < w; ++px)
				{
					double br = 0, bg = 0, bb = 0, ba = 0, tr = 0, tg = 0, tb = 0, ta = 0;
					if (braw)
					{
						Uint8 r, g, b, a;
						SDL_GetRGBA(reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(braw->pixels) + py * braw->pitch)[px], braw->format, &r, &g, &b, &a);
						br = r; bg = g; bb = b; ba = a / 255.0;
					}
					if (traw)
					{
						Uint8 r, g, b, a;
						SDL_GetRGBA(reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(traw->pixels) + py * traw->pitch)[px], traw->format, &r, &g, &b, &a);
						Uint8 or_, og_, ob_;
						referenceHueShift(r, g, b, a, hueShift, or_, og_, ob_);
						tr = or_; tg = og_; tb = ob_; ta = a / 255.0;
					}
					const double groupA = ta + ba * (1 - ta);
					const double pr = tr * ta + br * ba * (1 - ta);
					const double pg = tg * ta + bg * ba * (1 - ta);
					const double pb = tb * ta + bb * ba * (1 - ta);
					const double gr = groupA > 0 ? pr / groupA : 0, gg = groupA > 0 ? pg / groupA : 0, gb = groupA > 0 ? pb / groupA : 0;
					const double effA = groupA * (poseAlpha / 255.0);
					double *dst = &canvas[((static_cast<size_t>(y + py)) * cw + (x + px)) * 3];
					dst[0] = gr * effA + dst[0] * (1 - effA);
					dst[1] = gg * effA + dst[1] * (1 - effA);
					dst[2] = gb * effA + dst[2] * (1 - effA);
				}
			if (braw) SDL_UnlockSurface(braw);
			if (traw) SDL_UnlockSurface(traw);
		};

		// Fill and read back the whole window: drawSprite positions its own
		// quad in logical coordinates the test doesn't control, and
		// glReadPixels is bottom-up from the window's own bottom-left, so
		// reading anything smaller than the full window would misalign with
		// where a (0,0)-drawn sprite actually ends up physically.
		const int cw = gfx->getW(), ch = gfx->getH();
		double maxFrameError = 0;
		size_t frameChannels = 0;
		sprite.setBaseColor(colors[0]);
		assert(sprite.teamHueShiftDegrees() == hueShifts[0]);
		for (bool experiment : {false, true})
		for (int base = 0; base <= 384; base += 64)
		for (int dir = 0; dir < 8; ++dir)
		for (int span : {1, 30})
		{
			const int index = unitAnimationFrame(base, dir, 64);
			DrawableSurface *baseLayer = sprite.baseImage(index, experiment);
			if (!baseLayer) continue;
			std::vector<std::pair<int, int>> frames;
			drawUnitMotionBlur(base, dir, 64, span, [&](int f, int a) { frames.emplace_back(f, a); });

			gfx->drawFilledRect(0, 0, cw, ch, 30, 90, 45);
			for (auto &f : frames)
				gfx->drawSprite(0, 0, &sprite, f.first, static_cast<Uint8>(f.second));
			glFinish();
			// drawSprite draws at the sprite's own logical size (uncontrolled by
			// this test), so the physical framebuffer is cw/ch scaled by
			// pixelScale; sample each logical pixel's corresponding physical one.
			const int pw = static_cast<int>(cw * pixelScale), ph = static_cast<int>(ch * pixelScale);
			std::vector<Uint8> rendered(static_cast<size_t>(pw) * ph * 4);
			glReadPixels(0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, rendered.data());

			std::vector<double> canvas(static_cast<size_t>(cw) * ch * 3);
			for (size_t i = 0; i < canvas.size(); i += 3) { canvas[i] = 30; canvas[i + 1] = 90; canvas[i + 2] = 45; }
			for (auto &f : frames)
			{
				DrawableSurface *b = sprite.baseImage(f.first, experiment);
				DrawableSurface *t = sprite.teamOrig(f.first, experiment);
				if (!b && !t) continue;
				compositePose(b, t, 0, 0, hueShifts[0], canvas, cw, f.second);
			}
			for (int y = 0; y < ch; ++y)
				for (int x = 0; x < cw; ++x)
				{
					const int px = std::min(pw - 1, static_cast<int>(x * pixelScale));
					const int py = std::min(ph - 1, static_cast<int>(y * pixelScale));
					const int ry = ph - 1 - py; // glReadPixels rows are bottom-up
					for (int c = 0; c < 3; ++c)
					{
						const double expected = canvas[(static_cast<size_t>(y) * cw + x) * 3 + c];
						const int actual = rendered[(static_cast<size_t>(ry) * pw + px) * 4 + c];
						maxFrameError = std::max(maxFrameError, std::abs(expected - actual));
						++frameChannels;
					}
				}
		}
		std::cout << "Sharp/motion-blur framebuffer vs CPU reference: max=" << maxFrameError << "/255 comparisons="
		          << frameChannels / 3 << std::endl;
		assert(maxFrameError <= 3);
	}

	std::cout << "PASS: shader vs CPU HSV over all 1,792 poses / 12 team hues + 16-hue palette; "
	             "sharp and motion-blur framebuffer within tolerance of an independent CPU reference"
	          << std::endl;
	Toolkit::close();
}
