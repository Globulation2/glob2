// SPDX-License-Identifier: GPL-3.0-or-later

#include "GraphicContextPrivate.h"
#include <algorithm>
#include <cassert>

namespace GAGCore
{
	namespace
	{
		struct FlagScope
		{
			bool &flag;
			bool previous;
			explicit FlagScope(bool &flag) : flag(flag), previous(flag) { flag = true; }
			~FlagScope() { flag = previous; }
		};
	}

	int GraphicContext::pollEvent(SDL_Event *event)
	{
		if (!_gc) return SDL_PollEvent(event);
		assert(SDL_ThreadID() == _gc->eventThread);
		int result;
		{
			FlagScope scope(_gc->pollingEvents);
			result = SDL_PollEvent(event);
		}
		// Read the final OS size, not each intermediate size left in the event queue.
		_gc->updateWindowSize();
		return result;
	}

	int SDLCALL GraphicContext::watchWindow(void *userdata, SDL_Event *event)
	{
		auto *gfx = static_cast<GraphicContext *>(userdata);
		// SDL also invokes watchers for events pushed by other threads.
		if (SDL_ThreadID() != gfx->eventThread) return 1;
		if (gfx->pollingEvents && !gfx->presenting && event->type == SDL_WINDOWEVENT
			&& event->window.windowID == SDL_GetWindowID(gfx->window)
			&& event->window.event == SDL_WINDOWEVENT_EXPOSED)
			gfx->presentLastFrame();
		return 1;
	}

	void GraphicContext::releaseFrameCache()
	{
		SDL_FreeSurface(lastFrame);
		lastFrame = nullptr;
		#ifdef HAVE_OPENGL
		if (frameTexture) glDeleteTextures(1, &frameTexture);
		#endif
		frameTexture = 0;
		frameW = frameH = textureW = textureH = 0;
	}

	void GraphicContext::cacheFrame()
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & USEGPU)
		{
			int w, h;
			SDL_GL_GetDrawableSize(window, &w, &h);
			if (w <= 0 || h <= 0) return;
			// Copy-before-swap works with the legacy GL renderer and needs no FBO extension.
			glPushAttrib(GL_TEXTURE_BIT | GL_PIXEL_MODE_BIT);
			if (!frameTexture) glGenTextures(1, &frameTexture);
			glBindTexture(GL_TEXTURE_2D, frameTexture);
			int tw = 1, th = 1;
			while (tw < w) tw *= 2;
			while (th < h) th *= 2;
			GLint maximum;
			glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
			if (tw > maximum || th > maximum)
			{
				frameW = frameH = 0;
				glPopAttrib();
				return;
			}
			if (tw != textureW || th != textureH)
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tw, th, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
				textureW = tw; textureH = th;
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
			glReadBuffer(GL_BACK);
			glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
			frameW = w; frameH = h;
			glPopAttrib();
			return;
		}
		#endif
		if (!lastFrame || lastFrame->w != getW() || lastFrame->h != getH())
		{
			SDL_FreeSurface(lastFrame);
			lastFrame = SDL_CreateRGBSurfaceWithFormat(0, getW(), getH(), 32, sdlsurface->format->format);
		}
		if (lastFrame)
		{
			SDL_SetSurfaceBlendMode(lastFrame, SDL_BLENDMODE_NONE);
			SDL_SetSurfaceBlendMode(sdlsurface, SDL_BLENDMODE_NONE);
			SDL_BlitSurface(sdlsurface, nullptr, lastFrame, nullptr);
		}
	}

	void GraphicContext::presentLastFrame()
	{
		if (presenting || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return;
		FlagScope scope(presenting);
		#ifdef HAVE_OPENGL
		if (optionFlags & USEGPU)
		{
			if (!frameTexture || frameW <= 0 || frameH <= 0) return;
			int w, h;
			SDL_GL_GetDrawableSize(window, &w, &h);
			if (w <= 0 || h <= 0) return;
			// Restore actual GL state so the engine's GLState cache remains valid.
			glPushAttrib(GL_ALL_ATTRIB_BITS);
			glDisable(GL_SCISSOR_TEST);
			glDisable(GL_BLEND);
			glDisable(GL_DEPTH_TEST);
			if (glState.isTextureSRectangle) glDisable(GL_TEXTURE_RECTANGLE_NV);
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, frameTexture);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glClearColor(0, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			const float scale = std::min(float(w) / frameW, float(h) / frameH);
			const int width = int(frameW * scale + 0.5f), height = int(frameH * scale + 0.5f);
			glViewport((w - width) / 2, (h - height) / 2, width, height);
			glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
			glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
			glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
			// Sample pixel centers to avoid filtering into the unused power-of-two padding.
			const float left = 0.5f / textureW, right = (frameW - 0.5f) / textureW;
			const float bottom = 0.5f / textureH, top = (frameH - 0.5f) / textureH;
			glBegin(GL_QUADS);
			glTexCoord2f(left, bottom); glVertex2f(-1, -1);
			glTexCoord2f(right, bottom); glVertex2f(1, -1);
			glTexCoord2f(right, top); glVertex2f(1, 1);
			glTexCoord2f(left, top); glVertex2f(-1, 1);
			glEnd();
			glPopMatrix();
			glMatrixMode(GL_MODELVIEW); glPopMatrix();
			glMatrixMode(GL_PROJECTION); glPopMatrix();
			glPopAttrib();
			SDL_GL_SwapWindow(window);
			return;
		}
		#endif
		if (!lastFrame) return;
		SDL_Surface *target = SDL_GetWindowSurface(window);
		if (!target || target->w <= 0 || target->h <= 0) return;
		const float scale = std::min(float(target->w) / lastFrame->w, float(target->h) / lastFrame->h);
		SDL_Rect dst{0, 0, int(lastFrame->w * scale + 0.5f), int(lastFrame->h * scale + 0.5f)};
		dst.x = (target->w - dst.w) / 2; dst.y = (target->h - dst.h) / 2;
		SDL_FillRect(target, nullptr, SDL_MapRGB(target->format, 0, 0, 0));
		SDL_BlitScaled(lastFrame, nullptr, target, &dst);
		SDL_UpdateWindowSurface(window);
	}
}
