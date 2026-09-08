// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicContextPrivate.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace GAGCore;
static void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
class Context : public GraphicContext
{
	std::vector<Color> presentedPixels;
	int presentedWidth = 0, presentedHeight = 0;
	void swapBuffers() override
	{
		if (pollingEvents) ++cachedPresentations;
#ifdef HAVE_OPENGL
		// Read the exact frame submitted to the window system. Post-swap GL_FRONT
		// readback is not reliable on Mesa/Xvfb (it can return an all-black image).
		SDL_GL_GetDrawableSize(window, &presentedWidth, &presentedHeight);
		presentedPixels.resize(presentedWidth * presentedHeight);
		GLint previous;
		glGetIntegerv(GL_READ_BUFFER, &previous);
		glReadBuffer(GL_BACK);
		glReadPixels(0, 0, presentedWidth, presentedHeight, GL_RGBA, GL_UNSIGNED_BYTE, presentedPixels.data());
		glReadBuffer(previous);
#endif
		GraphicContext::swapBuffers();
	}
public:
	int frames = 0;
	int cachedPresentations = 0;
	Context(bool gpu) : GraphicContext(640, 480, RESIZABLE | (gpu ? USEGPU : 0), "Glob2 resize regression") { setMinRes(640, 480); }
	void nextFrame() override { ++frames; GraphicContext::nextFrame(); }
	void resize(int w, int h)
	{
		SDL_SetWindowSize(window, w, h); SDL_Delay(60); SDL_PumpEvents();
		int actualW, actualH;
		SDL_GetWindowSize(window, &actualW, &actualH);
		require(actualW == std::max(w, minW) && actualH == std::max(h, minH),
			"Window manager constrained test dimensions; use a desktop at least 1100x850");
	}
	void applyResize() { updateWindowSize(); }
	void expose(bool otherWindow = false)
	{
		SDL_Event event{}; event.type = SDL_WINDOWEVENT;
		event.window.event = SDL_WINDOWEVENT_EXPOSED;
		event.window.windowID = SDL_GetWindowID(window) + (otherWindow ? 1 : 0);
		pollingEvents = true;
		watchWindow(this, &event);
		pollingEvents = false;
	}
	void recursiveExpose() { presenting = true; expose(); require(presenting, "Reentrant guard lost"); presenting = false; }
	SDL_GLContext current() { return context; }
	bool cached() { return lastFrame || frameTexture; }
	Color pixel(int x, int y)
	{
		Color c;
		if (getOptionFlags() & USEGPU)
		{
#ifdef HAVE_OPENGL
			require(x >= 0 && x < presentedWidth && y >= 0 && y < presentedHeight, "Readback outside presented frame");
			c = presentedPixels[(presentedHeight-y-1)*presentedWidth+x];
#endif
		}
		else
		{
			auto *surface = SDL_GetWindowSurface(window);
			require(surface && x >= 0 && x < surface->w && y >= 0 && y < surface->h, "Readback outside software window surface");
			Uint32 p = 0;
			const int bytes = surface->format->BytesPerPixel;
			memcpy(&p, static_cast<char*>(surface->pixels)+y*surface->pitch+x*bytes, bytes);
			SDL_GetRGBA(p, surface->format, &c.r, &c.g, &c.b, &c.a);
		}
		return c;
	}
};
int main(int argc, char **argv)
{
	const bool gpu = argc > 1 && std::string(argv[1]) == "gl";
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	try
	{
		Context gfx(gpu);
		SDL_version version;
		SDL_GetVersion(&version);
		std::printf("SDL %d.%d.%d; video driver %s\n", version.major, version.minor, version.patch, SDL_GetCurrentVideoDriver());
#ifdef HAVE_OPENGL
		if (gpu) std::printf("OpenGL %s; renderer %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
#endif
		const auto originalContext = gfx.current();
		gfx.expose(); // No complete frame yet.
		gfx.setClipRect();
		gfx.drawFilledRect(0, 0, 640, 480, Color(255, 0, 0));
		gfx.drawFilledRect(0, 0, 320, 240, Color(0, 255, 0));
		gfx.nextFrame();
		require(gfx.cached(), "Frame cache missing");
		// An incomplete normal frame must never replace the cache.
		gfx.drawFilledRect(0, 0, 640, 480, Color(0, 0, 255));
		gfx.resize(960, 720);
		gfx.recursiveExpose();
		std::thread foreign([&] { gfx.expose(); }); foreign.join();
		gfx.expose(true);
#ifdef HAVE_OPENGL
		GLint viewport[4], scissor[4], binding;
		if (gpu)
		{
			gfx.setClipRect(10, 20, 70, 80);
			glGetIntegerv(GL_VIEWPORT, viewport);
			glGetIntegerv(GL_SCISSOR_BOX, scissor);
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
		}
#endif
		gfx.expose();
#ifdef HAVE_OPENGL
		if (gpu)
		{
			GLint actual[4], texture;
			glGetIntegerv(GL_VIEWPORT, actual);
			require(memcmp(viewport, actual, sizeof(actual)) == 0, "Expose changed viewport state");
			glGetIntegerv(GL_SCISSOR_BOX, actual);
			require(memcmp(scissor, actual, sizeof(actual)) == 0, "Expose changed clipping state");
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
			require(texture == binding, "Expose changed texture binding");
		}
#endif
		require(gfx.frames == 1, "Expose advanced the normal frame");
		Color top = gfx.pixel(100, 100), bottom = gfx.pixel(700, 600);
		std::printf("cache samples: %d,%d,%d / %d,%d,%d\n", top.r,top.g,top.b,bottom.r,bottom.g,bottom.b);
		require(top.g > 240 && top.r < 10 && bottom.r > 240 && bottom.g < 10, "Cached frame corrupted, flipped, or overwritten");
#ifdef HAVE_OPENGL
		if (gpu) require(glGetError() == GL_NO_ERROR, "GL error after cached presentation");
#endif
		for (auto size : {std::pair{800, 600}, {1024, 768}, {640, 480}})
		{
			gfx.resize(size.first, size.second);
			SDL_Event event;
			while (GraphicContext::pollEvent(&event)) {}
			require(gfx.getW() == size.first && gfx.getH() == size.second, "Logical size did not follow window");
			require(gfx.current() == originalContext, "Resize recreated GL context");
			Sint32 x = size.first-1, y = size.second-1;
			gfx.windowToLogical(x, y);
			require(x == size.first-1 && y == size.second-1, "Input no longer matches logical size");
			gfx.setClipRect();
			gfx.drawFilledRect(0, 0, gfx.getW(), gfx.getH(), Color(255, 0, 0));
			gfx.nextFrame();
			gfx.expose();
			require(gfx.pixel(size.first-5, size.second-5).r > 240, "New frame does not cover resized window");
		}
		gfx.resize(300, 200); gfx.applyResize();
		require(gfx.getW() >= 640 && gfx.getH() >= 480, "Minimum size not enforced");
		gfx.setRes(800, 600, gpu ? GraphicContext::USEGPU : 0);
		require(!gfx.cached(), "Window recreation retained old frame cache");
		gfx.expose();
		gfx.setClipRect();
		gfx.drawFilledRect(0, 0, 800, 600, Color(0, 255, 0));
		gfx.nextFrame(); gfx.expose();
		require(gfx.pixel(100, 100).g > 240, "Rendering failed after window recreation");
		std::printf("PASS %s: cache, callback guards, reflow, context lifetime, input, minimum size, recreation\n", gpu ? "GL" : "software");
		if (argc > 2 && std::string(argv[2]) == "interactive")
		{
			gfx.setRes(800, 600, GraphicContext::RESIZABLE | (gpu ? GraphicContext::USEGPU : 0));
			std::puts("Drag window edges, maximize/restore, then press Escape to finish.");
			bool done = false;
			while (!done)
			{
				const int frames = gfx.frames, cached = gfx.cachedPresentations;
				SDL_Event event;
				while (GraphicContext::pollEvent(&event))
					if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) done = true;
				require(gfx.frames == frames, "Native event pump advanced normal rendering");
				if (gfx.cachedPresentations != cached)
					std::printf("Event pump: %d cached presentations, normal frame %d unchanged; logical size %dx%d\n",
						gfx.cachedPresentations-cached, frames, gfx.getW(), gfx.getH());
				gfx.setClipRect();
				gfx.drawFilledRect(0, 0, gfx.getW(), gfx.getH(), Color(160, 20, 20));
				gfx.drawFilledRect(0, 0, gfx.getW()/2, gfx.getH()/2, Color(20, 180, 20));
				gfx.drawFilledRect((frames*5) % gfx.getW(), 0, 16, gfx.getH(), Color(255, 255, 255));
				gfx.nextFrame();
				std::fflush(stdout);
				SDL_Delay(30);
			}
		}
	}
	catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
	return 0; // SDL renames main to SDL_main on Windows; implicit main return does not apply.
}
