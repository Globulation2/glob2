// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real SDL presentation, GL clipping, input mapping and screen capture.
#include "GraphicContextPrivate.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace GAGCore;
namespace {
class Context : public GraphicContext
{
public:
	Context(bool gpu) : GraphicContext(640, 480, gpu ? USEGPU : 0, "Glob2 aspect regression") {}
	void resize(int w, int h)
	{
		SDL_SetWindowSize(window, w, h);
		SDL_Delay(100);
		SDL_PumpEvents();
		updateWindowSize();
		if (windowW != w || windowH != h) throw std::runtime_error("Window manager constrained test dimensions");
	}
	SDL_Surface* presented() { return SDL_GetWindowSurface(window); }
	int pixelWidth() const { return drawableW; }
	int pixelHeight() const { return drawableH; }
};

void require(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

Color pixel(SDL_Surface* surface, int x, int y)
{
	Uint32 value = 0;
	std::memcpy(&value, static_cast<char*>(surface->pixels) + y*surface->pitch + x*surface->format->BytesPerPixel, surface->format->BytesPerPixel);
	Color color;
	SDL_GetRGB(value, surface->format, &color.r, &color.g, &color.b);
	return color;
}

bool red(Color c) { return c.r > 240 && c.g < 10 && c.b < 10; }
bool green(Color c) { return c.g > 240 && c.r < 10 && c.b < 10; }

void checkBounds(SDL_Surface* surface, bool wantGreen, int x, int y, int w, int h, int tolerance=1)
{
	int left=surface->w, top=surface->h, right=-1, bottom=-1;
	for (int py=0; py<surface->h; ++py)
		for (int px=0; px<surface->w; ++px)
			if (wantGreen ? green(pixel(surface, px, py)) : red(pixel(surface, px, py)))
			{
				left=std::min(left, px); top=std::min(top, py);
				right=std::max(right, px); bottom=std::max(bottom, py);
			}
	bool matches=std::abs(left-x)<=tolerance && std::abs(top-y)<=tolerance && std::abs(right-(x+w-1))<=tolerance && std::abs(bottom-(y+h-1))<=tolerance;
	if (!matches) std::fprintf(stderr, "%s bounds %d,%d..%d,%d expected %d,%d %dx%d\n", wantGreen ? "green" : "red",left,top,right,bottom,x,y,w,h);
	require(matches,
		wantGreen ? "Clipped rectangle is misplaced or distorted" : "Presented game rectangle is misplaced or distorted");
}

void run(Context& context, bool gpu, int w, int h)
{
	context.resize(w, h);
	// Present once so the window system applies the new backing-buffer size.
	context.nextFrame();
	context.setClipRect();
	context.drawFilledRect(0, 0, 640, 480, Color(255, 0, 0));
	context.setClipRect(200, 160, 80, 60);
	context.drawFilledRect(-10, -10, 660, 500, Color(0, 255, 0));
	context.setClipRect();

	DrawableSurface captured(640, 480);
	captured.drawSurface(0, 0, &context);
	for (auto point : {std::pair{0, 0}, {639, 479}, {10, 240}, {320, 10}})
	{
		Color c=pixel(captured.getSDLSurface(), point.first, point.second);
		if (!red(c)) std::fprintf(stderr, "%dx%d capture (%d,%d) = %d,%d,%d\n", w,h,point.first,point.second,c.r,c.g,c.b);
		require(red(c), "Screen capture includes bars or loses logical edges");
	}
	// A drawable pixel can span more than one logical pixel when downscaled.
	int captureTolerance=std::max(1, int(std::ceil(std::max(640.f/w, 480.f/h))));
	checkBounds(captured.getSDLSurface(), true, 200, 160, 80, 60, captureTolerance);

	SDL_Surface* frame = nullptr;
	std::vector<Uint8> pixels;
	if (gpu)
	{
#ifdef HAVE_OPENGL
		int dw=context.pixelWidth(), dh=context.pixelHeight();
		pixels.resize(4*dw*dh);
		glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		for (int row=0; row<dh/2; ++row)
			for (int col=0; col<dw*4; ++col)
				std::swap(pixels[row*dw*4+col], pixels[(dh-1-row)*dw*4+col]);
		frame=SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), dw, dh, 32, dw*4, SDL_PIXELFORMAT_RGBA32);
		require(glGetError()==GL_NO_ERROR, "OpenGL reported an error");
#endif
	}
	else
	{
		context.nextFrame();
		frame=context.presented();
	}
	require(frame != nullptr, "No presented frame");
	float scale=std::min(frame->w/640.f, frame->h/480.f);
	int vw=std::lround(640*scale), vh=std::lround(480*scale);
	int ox=(frame->w-vw)/2, oy=(frame->h-vh)/2;
	checkBounds(frame, false, ox, oy, vw, vh);
	checkBounds(frame, true, ox+std::lround(200*scale), oy+std::lround(160*scale), std::lround(80*scale), std::lround(60*scale));
	if (ox>1) require(pixel(frame, 0, frame->h/2).r==0, "Side bar was not cleared");
	if (oy>1) require(pixel(frame, frame->w/2, 0).r==0, "Top bar was not cleared");
	if (gpu) SDL_FreeSurface(frame);

	float pointScale=std::min(w/640.f, h/480.f);
	for (auto point : {std::pair{20, 20}, {240, 190}, {620, 460}})
	{
		int px=std::lround((w-640*pointScale)/2+point.first*pointScale);
		int py=std::lround((h-480*pointScale)/2+point.second*pointScale);
		for (Uint32 type : {SDL_MOUSEMOTION, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP})
		{
			SDL_Event event{}; event.type=type;
			if (type==SDL_MOUSEMOTION) { event.motion.x=px; event.motion.y=py; }
			else { event.button.x=px; event.button.y=py; }
			GraphicContext::translateMouseEvent(&event);
			int x=type==SDL_MOUSEMOTION ? event.motion.x : event.button.x;
			int y=type==SDL_MOUSEMOTION ? event.motion.y : event.button.y;
			require(std::abs(x-point.first)<=2 && std::abs(y-point.second)<=2, "Mouse event misses rendered target");
		}
		GraphicContext::translateMouseCoordinates(px, py);
		require(std::abs(px-point.first)<=2 && std::abs(py-point.second)<=2, "Polled mouse misses rendered target");
	}
	std::printf("PASS %s %dx%d: presentation, clipping, capture, mouse events and polling\n", gpu ? "GL" : "software", w, h);
}
}

int main(int argc, char** argv)
{
	bool gpu=argc==2 && std::string(argv[1])=="gl";
#ifndef HAVE_OPENGL
	if (gpu) { std::fprintf(stderr, "OpenGL support is required\n"); return 1; }
#endif
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	try
	{
		Context context(gpu);
		for (auto size : {std::pair{640, 480}, {1280, 800}, {800, 1280}, {853, 641}, {480, 270}})
			run(context, gpu, size.first, size.second);
	}
	catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
	return 0;
}
