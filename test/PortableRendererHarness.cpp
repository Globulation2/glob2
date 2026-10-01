// SPDX-License-Identifier: GPL-3.0-or-later
#include <Environment.h>
#include "Glob2Test.h"
#include <string>
#include <memory>
#include <utility>
#include <cmath>
#include <cstdlib>
#include <GraphicContext.h>
#include <RenderBackend.h>
#include <ScreenStack.h>
#include <InterfacePresentation.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#ifdef HAVE_OPENGL
#include <SDL3/SDL_opengl.h>
#endif

using namespace GAGCore;
namespace
{
class Context : public GraphicContext
{
  public:
	Context() : GraphicContext(320, 240, PORTABLEGPU | RESIZABLE, "Glob2 portable renderer test") {}
	SDL_Surface *capture() { return renderer->capture(); }
	void resetTextures() { renderer->reset(); }
	void resizeWindow(int width, int height)
	{
		SDL_SetWindowSize(window, width, height);
		updateWindowSize();
	}
	Uint32 windowID() const { return SDL_GetWindowID(window); }
};
void require(bool condition, const char *message)
{
	GLOB2_REQUIRE(condition, message);
}
void expect(SDL_Surface *pixels, int x, int y, int r, int g, int b)
{
	Uint32 value;
	std::memcpy(&value, static_cast<char *>(pixels->pixels) + y * pixels->pitch + x * 4, 4);
	Uint8 red, green, blue;
	SDL_GetRGB(value, SDL_GetPixelFormatDetails(pixels->format), SDL_GetSurfacePalette(pixels), &red, &green, &blue);
	if (std::abs(int(red) - r) > 3 || std::abs(int(green) - g) > 3 || std::abs(int(blue) - b) > 3)
	{
		std::fprintf(stderr, "pixel %d,%d: %d,%d,%d expected %d,%d,%d\n", x, y, red, green, blue, r,
					 g, b);
		throw std::runtime_error("Unexpected renderer pixel");
	}
}
void verifyUITransform(unsigned flags)
{
	GraphicContext context(320, 240, flags, "Glob2 shared UI transform test");
	context.drawFilledRect(0, 0, 320, 240, Color(0, 0, 0));
	context.setClipRect(90, 40, 50, 50);
	SDL_Rect bounds{100, 50, 20, 20};
	context.setUITransform(2, 100, 50, &bounds);
	context.setClipRect(0, 0, 8, 8);
	context.drawFilledRect(0, 0, 30, 30, Color(255, 0, 0));
	context.setUITransform();
	int x, y, w, h;
	context.getClipRect(&x, &y, &w, &h);
	require(x == 90 && y == 40 && w == 50 && h == 50, "Transform must restore the caller's clip");
	context.setClipRect();
	context.drawFilledRect(10, 10, 4, 4, Color(0, 255, 0));
	SDL_Surface *pixels = context.getSDLSurface();
#ifdef HAVE_OPENGL
	if (flags & GraphicContext::USEGPU)
	{
		int width, height;
		SDL_GetWindowSizeInPixels(SDL_GetWindowFromID(context.windowID()), &width, &height);
		pixels = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels->pixels);
		// GL's first row is the bottom of the drawable.
		expect(pixels, 110 * width / 320, height - 1 - 60 * height / 240, 255, 0, 0);
		expect(pixels, 118 * width / 320, height - 1 - 60 * height / 240, 0, 0, 0);
		expect(pixels, 12 * width / 320, height - 1 - 12 * height / 240, 0, 255, 0);
		SDL_DestroySurface(pixels);
		return;
	}
#endif
	expect(pixels, 110, 60, 255, 0, 0);
	expect(pixels, 118, 60, 0, 0, 0);
	expect(pixels, 12, 12, 0, 255, 0);
}
// Check the production primitive, not an approximation of its geometry.
class BoundaryContext : public GraphicContext
{
  public:
	BoundaryContext(unsigned flags) : GraphicContext(320, 240, flags, "Zone boundary regression") {}
	SDL_Surface *read()
	{
		if (renderer)
			return renderer->capture();
#ifdef HAVE_OPENGL
		if (optionFlags & USEGPU)
		{
			int w, h;
			SDL_GetWindowSizeInPixels(SDL_GetWindowFromID(windowID()), &w, &h);
			auto *pixels = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
			glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels->pixels);
			auto *upright = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
			for (int y = 0; y < h; ++y)
				std::memcpy(static_cast<char *>(upright->pixels) + y * upright->pitch,
							static_cast<char *>(pixels->pixels) + (h - y - 1) * pixels->pitch,
							w * 4);
			SDL_DestroySurface(pixels);
			return upright;
		}
#endif
		return SDL_ConvertSurface(getSDLSurface(), SDL_PIXELFORMAT_RGBA32);
	}
};
void verifyMapBoundaries(unsigned flags)
{
	BoundaryContext context(flags);
	for (float zoom : {.25f, .33f, 1.f / 3, .5f, .75f, 1.f, 1.5f})
		for (float offset : {-.75f, -.25f, 0.f, .25f, .5f, .75f})
		{
			context.drawFilledRect(0, 0, 320, 240, Color(0, 0, 0));
			context.beginMapTransform(zoom, offset, offset, 0, 0, 320, 240);
			context.drawMapCopies(
				256, 256, int(std::ceil(320 / zoom)), int(std::ceil(240 / zoom)),
				[&]
				{
					for (int edge = 32; edge < 160; edge += 32)
					{
						context.drawMapBoundary(edge, 32, edge + 32, 32, Color(255, 255, 0));
						context.drawMapBoundary(edge, 160, edge + 32, 160, Color(255, 255, 0));
						context.drawMapBoundary(32, edge, 32, edge + 32, Color(255, 255, 0));
						context.drawMapBoundary(160, edge, 160, edge + 32, Color(255, 255, 0));
					}
					// Boundary drawing must preserve the transform for later map artwork.
					context.drawFilledRect(80, 80, 16, 16, Color(0, 255, 0));
				});
			context.endMapTransform();
			auto *pixels = context.read();
			const float density = pixels->w / 320.f;
			// The untransformed legacy software path draws only the primary
			// pass; transformed software and GPU paths own periodic copies.
			const int copies = flags == 0 && zoom == 1 && offset == 0 ? 1 : 3;
			for (int copy = 0; copy < copies; ++copy)
			{
				const int left = std::lround(((32 + copy * 256) * zoom + offset) * density);
				const int right = std::lround(((160 + copy * 256) * zoom + offset) * density);
				const int top = std::lround((32 * zoom + offset) * density);
				const int bottom = std::lround((160 * zoom + offset) * density);
				const int centerX = int(((88 + copy * 256) * zoom + offset) * density);
				const int centerY = int((88 * zoom + offset) * density);
				if (centerX < pixels->w && centerY < pixels->h)
					expect(pixels, centerX, centerY, 0, 255, 0);
				for (int x = left; x <= right && x < pixels->w; ++x)
				{
					expect(pixels, x, top, 255, 255, 0);
					if (bottom < pixels->h)
						expect(pixels, x, bottom, 255, 255, 0);
				}
				for (int y = top; y <= bottom && y < pixels->h; ++y)
				{
					if (left < pixels->w)
						expect(pixels, left, y, 255, 255, 0);
					if (right < pixels->w)
						expect(pixels, right, y, 255, 255, 0);
				}
			}
			if (const char *directory = SDL_getenv_unsafe("GLOB2_ZONE_EVIDENCE_DIR");
				directory && zoom == .33f && offset == .25f)
			{
				const auto path =
					std::string(directory) + "/borders-" + std::to_string(flags) + ".bmp";
				require(SDL_SaveBMP(pixels, path.c_str()), "Cannot save zone evidence");
			}
			SDL_DestroySurface(pixels);
			context.nextFrame();
		}
	std::printf(
		"PASS zone borders: backend %u, zoom/offset sweep, joined segments and wrapped copies\n",
		flags);
}} // namespace

TEST_SUITE("PortableRenderer")
{
TEST_CASE("zone boundaries; UI transforms and portable rendering paths [display]")
{
	verifyMapBoundaries(0);
	verifyMapBoundaries(GraphicContext::PORTABLEGPU);
	verifyUITransform(0);
#ifdef HAVE_OPENGL
	verifyMapBoundaries(GraphicContext::USEGPU);
	verifyUITransform(GraphicContext::USEGPU);
#endif
	{
		GraphicContext::setRequestedUiScale(2);
		GraphicContext scaled(640, 480, GraphicContext::RESIZABLE,
							  "Glob2 responsive scale test");
		scaled.setCompactWindowAllowed(true);
		scaled.refreshPresentation();
		scaled.setResponsiveViewport(true);
		require(scaled.getUiScale() == 2 && scaled.getW() == 320 && scaled.getH() == 240,
				"Responsive first launch must honor user scale below the legacy layout floor");
		require(presentationState.layout == PresentationLayout::Compact,
				"User scale must participate in fit");
		GraphicContext::setRequestedUiScale(1);
	}
	Context context;
	{
		context.drawFilledRect(0, 0, 320, 240, Color(0, 0, 0));
		SDL_Rect bounds{100, 50, 20, 20};
		context.setUITransform(2, 100, 50, &bounds);
		context.setClipRect(0, 0, 8, 8);
		context.drawFilledRect(0, 0, 30, 30, Color(255, 0, 0));
		context.setUITransform();
		context.setClipRect();
		context.drawFilledRect(10, 10, 4, 4, Color(0, 255, 0));
		auto *pixels = context.capture();
		const double density = pixels->w / 320.0;
		expect(pixels, int(110 * density), int(60 * density), 255, 0, 0);
		expect(pixels, int(118 * density), int(60 * density), 0, 0, 0);
		expect(pixels, int(12 * density), int(12 * density), 0, 255, 0);
		SDL_DestroySurface(pixels);
	}
	DrawableSurface sprite(16, 16);
	sprite.drawFilledRect(0, 0, 16, 16, Color(0, 255, 0));
	for (int pass = 0; pass < 3; ++pass)
	{
		context.setClipRect();
		context.drawFilledRect(0, 0, 320, 240, Color(0, 0, 0));
		context.drawFilledRect(10, 10, 20, 20, Color(255, 0, 0));
		context.setClipRect(15, 15, 5, 5);
		context.drawFilledRect(0, 0, 320, 240, Color(0, 0, 255));
		context.setClipRect();
		context.drawSurface(40, 40, 32, 32, &sprite);
		context.drawFilledRect(80, 80, 20, 20, Color(255, 255, 255, 128));
		auto *pixels = context.capture();
		require(pixels->w >= 320 && pixels->h >= 240, "Invalid drawable size");
		auto check = [&](int x, int y, int r, int g, int b)
		{ expect(pixels, x * pixels->w / 320, y * pixels->h / 240, r, g, b); };
		check(0, 0, 0, 0, 0);
		check(12, 12, 255, 0, 0);
		check(16, 16, 0, 0, 255);
		check(50, 50, pass == 2 ? 255 : 0, 255, 0);
		check(85, 85, 128, 128, 128);
		SDL_DestroySurface(pixels);
		context.nextFrame();
		if (pass == 0)
			context.resetTextures();
		if (pass == 1)
			sprite.drawFilledRect(0, 0, 16, 16, Color(255, 255, 0));
	}
	context.resizeWindow(640, 480);
	SDL_Event event{};
	while (SDL_PollEvent(&event))
		GraphicContext::translateMouseEvent(&event);
	for (auto type : {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP})
	{
		event = {};
		event.type = type;
		if (type == SDL_EVENT_MOUSE_MOTION)
		{
			event.motion.windowID = context.windowID();
			event.motion.x = 240;
			event.motion.y = 160;
		}
		else
		{
			event.button.windowID = context.windowID();
			event.button.x = 240;
			event.button.y = 160;
		}
		require(SDL_PushEvent(&event) == 1, "Could not enqueue resized-window input");
		bool observed = false;
		while (SDL_PollEvent(&event))
		{
			GraphicContext::translateMouseEvent(&event);
			if (event.type == type)
			{
				int x = type == SDL_EVENT_MOUSE_MOTION ? event.motion.x : event.button.x;
				int y = type == SDL_EVENT_MOUSE_MOTION ? event.motion.y : event.button.y;
				require(x == 240 && y == 160,
						"Desktop resize keeps logical input aligned with master");
				observed = true;
			}
		}
		require(observed, "Resized-window event was not delivered");
	}
	int x = 240, y = 160;
	GraphicContext::translateMouseCoordinates(x, y);
	require(x == 240 && y == 160, "Raw mouse coordinates must use the same logical mapping");
	{
		struct ResourceScreen : GAGGUI::Screen
		{
			Context &context;
			DrawableSurface &sprite;
			int draws = 0, red = 255, green = 255;
			ResourceScreen(Context &context, DrawableSurface &sprite)
				: context(context), sprite(sprite)
			{
			}
			void updateExecution(Uint32) override {}
			void drawExecution() override
			{
				context.setClipRect();
				context.drawFilledRect(0, 0, 320, 240, Color(0, 0, 0));
				context.drawSurface(40, 40, 32, 32, &sprite);
				auto *pixels = context.capture();
				expect(pixels, 50 * pixels->w / 320, 50 * pixels->h / 240, red, green, 0);
				SDL_DestroySurface(pixels);
				context.nextFrame();
				++draws;
			}
		};
		GAGGUI::ScreenStack stack(context);
		auto owned = std::make_unique<ResourceScreen>(context, sprite);
		auto *probe = owned.get();
		stack.push(std::move(owned));
		stack.frame(0, {});
		SDL_Event background{};
		background.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
		SDL_Event reset{};
		reset.type = SDL_EVENT_RENDER_DEVICE_RESET;
		stack.frame(40, {background, reset});
		require(probe->draws == 1, "No rendering while backgrounded with a lost device");
		// Change CPU pixels without the normal dirty notification: the
		// deferred reset must recreate the previously cached texture.
		auto *source = sprite.getSDLSurface();
		SDL_FillSurfaceRect(source, nullptr, SDL_MapSurfaceRGB(source, 255, 0, 0));
		probe->green = 0;
		SDL_Event foreground{};
		foreground.type = SDL_EVENT_DID_ENTER_FOREGROUND;
		stack.frame(100000, {foreground});
		require(probe->draws == 2, "Resource restoration precedes the first resumed draw");
		SDL_FillSurfaceRect(source, nullptr, SDL_MapSurfaceRGB(source, 0, 255, 0));
		probe->red = 0;
		probe->green = 255;
		reset.type = SDL_EVENT_LOW_MEMORY;
		stack.frame(100040, {reset});
		stack.frame(100080, {background});
		SDL_Event quit{};
		quit.type = SDL_EVENT_QUIT;
		stack.frame(100120, {quit});
		require(!stack.running(), "Quit must be honored while backgrounded");
	}
	GAGCore::setProcessEnvironment("GLOB2_MOBILE_UI", "1", 1);
	context.setResponsiveViewport(true);
	require(context.getW() == 640 && context.getH() == 480,
			"Responsive viewport must fill window points");
	context.resizeWindow(320, 568);
	require(context.getW() == 320 && context.getH() == 568,
			"Portrait resize must update logical dimensions");
	context.drawFilledRect(0, 0, 320, 568, Color(90, 30, 150));
	auto *portrait = context.capture();
	expect(portrait, portrait->w / 2, portrait->h - 2, 90, 30, 150);
	SDL_DestroySurface(portrait);
	context.setResponsiveViewport(true, 800, 600);
	require(context.getW() == 800 && context.getH() == 1420,
			"Portrait game must extend to the full window height");
	context.drawFilledRect(0, 0, 800, 1420, Color(90, 30, 150));
	auto *full = context.capture();
	expect(full, full->w / 2, 2, 90, 30, 150);
	expect(full, full->w / 2, full->h - 2, 90, 30, 150);
	SDL_DestroySurface(full);
	context.nextFrame();
	context.resizeWindow(568, 320);
	require(context.getW() == 1065 && context.getH() == 600,
			"Landscape game must extend to the full window width");
	context.drawFilledRect(0, 0, 1065, 600, Color(90, 30, 150));
	full = context.capture();
	expect(full, 2, full->h / 2, 90, 30, 150);
	expect(full, full->w - 2, full->h / 2, 90, 30, 150);
	SDL_DestroySurface(full);
	context.nextFrame();
	context.resizeWindow(320, 568);
	context.setResponsiveViewport(false);
	require(context.getW() == 320 && context.getH() == 240,
			"Legacy logical dimensions must be restored");
	auto *letterbox = context.capture();
	expect(letterbox, letterbox->w / 2, 2, 0, 0, 0);
	SDL_DestroySurface(letterbox);
	{
		struct ViewportScreen : GAGGUI::Screen
		{
			int changes = 0;
			bool usesResponsiveViewport() const override { return true; }
			std::pair<int, int> minimumViewportSize() const override { return {800, 600}; }
			void updateExecution(Uint32) override {}
			void drawExecution() override {}
			void viewportResized(int, int, int, int) override { ++changes; }
		};
		struct Child : GAGGUI::Screen
		{
			void updateExecution(Uint32) override { endExecute(0); }
			void drawExecution() override {}
		};
		GAGGUI::ScreenStack stack(context);
		auto parent = std::make_unique<ViewportScreen>();
		auto *probe = parent.get();
		stack.push(std::move(parent));
		stack.frame(0, {});
		require(context.getH() == 1420 && probe->changes == 1,
				"Entering gameplay must notify its expanded viewport");
		stack.push(std::make_unique<Child>());
		stack.frame(40, {});
		stack.frame(80, {});
		require(context.getH() == 1420 && probe->changes == 3,
				"A modal round trip must restore and notify the game viewport");
		SDL_SetWindowSize(SDL_GetWindowFromID(context.windowID()), 568, 320);
		SDL_Event resize{};
		resize.type = SDL_EVENT_WINDOW_RESIZED;
		resize.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
		stack.frame(120, {resize});
		require(context.getW() == 1065 && context.getH() == 600 && probe->changes == 4,
				"Rotation must notify the retained game");
	}
	GAGCore::setProcessEnvironment("GLOB2_MOBILE_UI", "", 1);
	std::puts("PASS portable renderer: clipping, texture scaling, alpha, device reset, dirty "
			  "textures, resized input");
}
}
