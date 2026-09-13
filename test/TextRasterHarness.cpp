// SPDX-License-Identifier: GPL-3.0-or-later
// Text drawn on a scaled screen must be rasterised for the pixels it actually covers,
// while the sizes it reports stay those of the authored font size.
#include "GraphicContextPrivate.h"
#include <TrueTypeFont.h>
#include <SDL_ttf.h>
#include <Toolkit.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace GAGCore;
namespace {
const char *const SAMPLES[] = {
	"Globulation",
	"iiillmmWW",
	"The quick brown fox jumps over the lazy dog",
	"0123456789",
	"Wheat: 42/60",
};

class Context : public GraphicContext
{
public:
	Context(int w, int h) : GraphicContext(w, h, USEGPU, "Glob2 text raster regression") {}
	int surfaceW() const { return sdlsurface ? sdlsurface->w : 0; }
	int surfaceH() const { return sdlsurface ? sdlsurface->h : 0; }
	int pixelW() const { return drawableW; }
	int pixelH() const { return drawableH; }
	using GraphicContext::textRenderScale;
	//! Where the scaled interface sits inside the drawable, which is centred when the two
	//! aspect ratios differ.
	void letterbox(float &scale, int &offX, int &offY) { glLetterbox(scale, offX, offY); }
};

void require(bool condition, const std::string &message)
{
	if (!condition) throw std::runtime_error(message);
}

struct Metrics
{
	std::vector<int> widths, heights;
};

Metrics measure(Font *font)
{
	Metrics m;
	for (const char *sample : SAMPLES)
	{
		m.widths.push_back(font->getStringWidth(sample));
		m.heights.push_back(font->getStringHeight(sample));
	}
	return m;
}

// The drawable, top row first.
std::vector<unsigned char> readFrame(Context &context)
{
	const int w = context.pixelW(), h = context.pixelH();
	std::vector<unsigned char> pixels(size_t(w) * h * 4, 0);
#ifdef HAVE_OPENGL
	glFlush();
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	require(glGetError() == GL_NO_ERROR, "OpenGL reported an error while reading the frame");
	std::vector<unsigned char> flipped(pixels.size());
	for (int row = 0; row < h; ++row)
		std::memcpy(&flipped[size_t(row) * w * 4], &pixels[size_t(h - 1 - row) * w * 4], size_t(w) * 4);
	pixels.swap(flipped);
#endif
	return pixels;
}

void saveFrame(const std::vector<unsigned char> &pixels, int w, int h, const std::string &path)
{
	if (path.empty()) return;
	SDL_Surface *frame = SDL_CreateRGBSurfaceWithFormatFrom(
		const_cast<unsigned char *>(pixels.data()), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
	if (!frame) return;
	SDL_SaveBMP(frame, path.c_str());
	SDL_FreeSurface(frame);
}

// Draw one line of text on a black frame at a position that lands on the block grid.
std::vector<unsigned char> drawSample(Context &context, Font *font, int logicalX, int logicalY)
{
	// Present once so the window system applies the current backing-buffer size, then draw
	// into the back buffer and read that back before it is swapped away.
	context.nextFrame();
	context.setClipRect();
	context.drawFilledRect(0, 0, context.surfaceW(), context.surfaceH(), Color::black);
	font->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 255, 255));
	context.drawString(logicalX, logicalY, font, SAMPLES[2]);
	return readFrame(context);
}

// Count blocks of scale x scale drawable pixels that are not a single flat colour. A glyph
// rasterised at the authored size and then magnified fills every block uniformly, so detail
// inside the blocks is exactly the detail the finer raster added.
long countDetailedBlocks(const std::vector<unsigned char> &pixels, int w, int h, int block)
{
	long detailed = 0;
	for (int by = 0; by + block <= h; by += block)
		for (int bx = 0; bx + block <= w; bx += block)
		{
			const unsigned char *first = &pixels[(size_t(by) * w + bx) * 4];
			bool uniform = true;
			for (int y = 0; y < block && uniform; ++y)
				for (int x = 0; x < block; ++x)
				{
					const unsigned char *p = &pixels[(size_t(by + y) * w + bx + x) * 4];
					if (std::memcmp(p, first, 3) != 0) { uniform = false; break; }
				}
			if (!uniform) ++detailed;
		}
	return detailed;
}

long countLitPixels(const std::vector<unsigned char> &pixels, int w, int x0, int y0, int x1, int y1)
{
	long lit = 0;
	for (int y = y0; y < y1; ++y)
		for (int x = x0; x < x1; ++x)
			if (pixels[(size_t(y) * w + x) * 4] > 32) ++lit;
	return lit;
}

long countLitPixels(const std::vector<unsigned char> &pixels)
{
	long lit = 0;
	for (size_t i = 0; i + 3 < pixels.size(); i += 4)
		if (pixels[i] > 32) ++lit;
	return lit;
}

// Rasterising for the screen is only worth anything if every raster pixel lands on exactly
// one screen pixel. Hinting rounds each font size differently, so a raster squeezed into the
// rectangle the authored size measures loses or repeats pixel columns inside single glyphs.
// Rasterise the same string independently here and require the frame to match it.
void checkRasterIsPixelExact(const std::vector<unsigned char> &frame, int frameW, float scale,
	int offX, int offY, const char *text, int logicalX, int logicalY)
{
	const int size = static_cast<int>(std::lround(20 * scale));
	TTF_Font *reference = TTF_OpenFont("data/fonts/sans.ttf", size);
	require(reference != NULL, "Could not open the reference font at the scaled size");
	SDL_Color white{255, 255, 255, 255};
	SDL_Surface *rendered = TTF_RenderUTF8_Blended(reference, text, white);
	require(rendered != NULL, "Could not rasterise the reference string");
	SDL_Surface *ink = SDL_ConvertSurfaceFormat(rendered, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(rendered);
	require(ink != NULL, "Could not convert the reference string");

	// White on black composites to exactly the glyph coverage, so the frame's red channel
	// should reproduce the reference alpha.
	const int originX = offX + static_cast<int>(std::lround(logicalX * scale));
	const int originY = offY + static_cast<int>(std::lround(logicalY * scale));
	long compared = 0, mismatched = 0, worst = 0;
	for (int y = 0; y < ink->h; ++y)
		for (int x = 0; x < ink->w; ++x)
		{
			const unsigned char *ref = static_cast<const unsigned char *>(ink->pixels) + y * ink->pitch + x * 4;
			const unsigned char *got = &frame[(size_t(originY + y) * frameW + originX + x) * 4];
			const long delta = std::abs(int(got[0]) - int(ref[3]));
			++compared;
			if (delta > 24) { ++mismatched; worst = std::max(worst, delta); }
		}
	SDL_FreeSurface(ink);
	TTF_CloseFont(reference);

	require(compared > 0, "The reference string is empty");
	const double ratio = double(mismatched) / compared;
	require(ratio < 0.005,
		"The drawn glyphs do not match a raster made for this screen: "
		+ std::to_string(mismatched) + " of " + std::to_string(compared)
		+ " pixels differ (worst " + std::to_string(worst) + "). The raster is being stretched "
		"into the rectangle the authored size measures instead of drawn at its own size.");
}

// An overlay dialog composes itself on its own surface, which holds logical pixels and has
// no scaled blit to resample a finer raster with. Text drawn there has to keep arriving.
void checkOverlayText(Context &context, Font *font, float scale)
{
	const int x = 100, y = 200, w = 400, h = 60;
	DrawableSurface overlay(w, h);
	overlay.drawFilledRect(0, 0, w, h, Color::black);
	font->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 255, 255));
	overlay.drawString(4, 4, font, SAMPLES[0]);

	context.nextFrame();
	context.setClipRect();
	context.drawFilledRect(0, 0, context.surfaceW(), context.surfaceH(), Color::black);
	context.drawSurface(x, y, &overlay);
	const std::vector<unsigned char> frame = readFrame(context);
	const long lit = countLitPixels(frame, context.pixelW(),
		static_cast<int>(x * scale), static_cast<int>(y * scale),
		static_cast<int>((x + w) * scale), static_cast<int>((y + h) * scale));
	require(lit > 0, "Text drawn on an overlay surface never reached the screen");
}

void run(const std::string &outputDir)
{
	const int windowW = 1280, windowH = 960;
	// Open the window already scaled, the way a HiDPI desktop hands it over. The sharpness
	// check must not depend on a mode switch, so that the saved frames compare one build
	// against another rather than one context against its successor.
	GraphicContext::setRequestedUiScale(2.0f);
	Context context(windowW, windowH);
	require(context.surfaceW() == windowW / 2, "The interface surface did not halve");

	Toolkit::loadFont("data/fonts/sans.ttf", 20, "harness");
	Font *font = Toolkit::getFont("harness");
	require(font != NULL, "Could not load data/fonts/sans.ttf");

	const float scale = context.textRenderScale();
	require(scale > 1.05f, "The context does not scale text, so there is nothing to test");

	const Metrics scaled = measure(font);
	const std::vector<unsigned char> sharp = drawSample(context, font, 10, 10);
	saveFrame(sharp, context.pixelW(), context.pixelH(), outputDir.empty() ? "" : outputDir + "/text-scaled.bmp");
	require(countLitPixels(sharp) > 0, "Nothing was drawn on the scaled screen");

	const int block = static_cast<int>(std::lround(scale));
	require(std::fabs(scale - block) < 0.01f, "This display scales text by a fraction; the block test needs an integer");
	const long detailed = countDetailedBlocks(sharp, context.pixelW(), context.pixelH(), block);
	require(detailed > 0,
		"Every " + std::to_string(block) + "x" + std::to_string(block)
		+ " block of the frame is flat: the glyphs were magnified instead of rasterised for the screen");

	{
		float viewScale; int offX, offY;
		context.letterbox(viewScale, offX, offY);
		checkRasterIsPixelExact(sharp, context.pixelW(), scale, offX, offY, SAMPLES[2], 10, 10);
	}
	checkOverlayText(context, font, scale);

	// A scale the screen does not make an integer of, and a screen that cannot deliver the
	// requested resolution and therefore reduces it, resample glyphs just as a magnifying one
	// does. Neither has a pixel grid to check blocks on, so check the raster itself.
	for (auto attempt : {std::pair<float, const char *>{1.7f, "a fractional interface scale"},
		{1.0f, "a resolution the screen cannot deliver"}})
	{
		// Fullscreen keeps the desktop's size, so asking for more than it has is what reduces.
		const bool oversized = attempt.first == 1.0f;
		const int w = oversized ? windowW * 3 : windowW, h = oversized ? windowH * 3 : windowH;
		const Uint32 flags = GraphicContext::USEGPU | (oversized ? GraphicContext::FULLSCREEN : 0);
		GraphicContext::setRequestedUiScale(attempt.first);
		require(context.setRes(w, h, flags),
			std::string("Could not reopen the window for ") + attempt.second);
		// The geometry decides whether glyphs are resampled, not what the font code makes of it.
		float other; int offX, offY;
		context.letterbox(other, offX, offY);
		if (std::fabs(other - 1.0f) < 0.01f)
		{
			std::printf("SKIP %s: this display does not scale for it\n", attempt.second);
			continue;
		}
		// Drawn at the origin: any other position lands between device pixels once the scale
		// is not an integer, and no raster can be exact about where it is not aligned.
		const std::vector<unsigned char> frame = drawSample(context, font, 0, 0);
		checkRasterIsPixelExact(frame, context.pixelW(), other, offX, offY, SAMPLES[2], 0, 0);
		std::printf("     %s: scale %.3f, glyphs pixel-exact\n", attempt.second, other);
	}

	// Drop back to an unscaled interface. The reported sizes are what every layout in the
	// game is written against, so they have to survive the switch unchanged, and the frame
	// has to keep drawing through the replaced GL context.
	GraphicContext::setRequestedUiScale(1.0f);
	require(context.setRes(windowW, windowH, GraphicContext::USEGPU), "Could not reopen the window unscaled");
	require(context.textRenderScale() > 0.0f, "The context reports no text scale");
	require(context.surfaceW() == windowW, "The interface surface did not follow the window");

	const Metrics reference = measure(font);
	for (size_t i = 0; i < reference.widths.size(); ++i)
	{
		require(reference.widths[i] > 0 && reference.heights[i] > 0, "The font reports an empty string size");
		require(scaled.widths[i] == reference.widths[i],
			std::string("Text width moved with the interface scale: \"") + SAMPLES[i] + "\" "
			+ std::to_string(reference.widths[i]) + " -> " + std::to_string(scaled.widths[i]));
		require(scaled.heights[i] == reference.heights[i],
			std::string("Text height moved with the interface scale: \"") + SAMPLES[i] + "\"");
	}

	const std::vector<unsigned char> flat = drawSample(context, font, 10, 10);
	saveFrame(flat, context.pixelW(), context.pixelH(), outputDir.empty() ? "" : outputDir + "/text-unscaled.bmp");
	require(countLitPixels(flat) > 0, "Nothing was drawn after the interface scale changed");

	std::printf("PASS text raster: scale %.2f, metrics unchanged across %zu samples, glyphs pixel-exact, overlay text drawn, %ld detailed %dx%d blocks\n",
		scale, reference.widths.size(), detailed, block, block);
}
}

int main(int argc, char **argv)
{
#ifndef HAVE_OPENGL
	std::fprintf(stderr, "OpenGL support is required\n");
	return 1;
#else
	const std::string outputDir = argc > 1 ? argv[1] : "";
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	// Keep the run out of the player's own profile.
	if (!outputDir.empty()) SDL_setenv("GLOB2_USER_DIR", outputDir.c_str(), 0);
	Toolkit::init("glob2");
	try
	{
		run(outputDir);
	}
	catch (const std::exception &error)
	{
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		return 1;
	}
	return 0;
#endif
}
