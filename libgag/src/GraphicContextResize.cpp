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
		SDL_FreeSurface(frameCache.surface);
		#ifdef HAVE_OPENGL
		if (frameCache.texture) glDeleteTextures(1, &frameCache.texture);
		#endif
		frameCache = FrameCache{};
	}

	void GraphicContext::reportFrameCacheFailure(const char *reason)
	{
		frameCache.valid = false;
		if (!frameCache.failureReported)
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot cache completed frame: %s", reason);
		frameCache.failureReported = true;
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
			if (!frameCache.texture) glGenTextures(1, &frameCache.texture);
			if (!frameCache.texture)
			{
				reportFrameCacheFailure("OpenGL texture creation failed");
				glPopAttrib();
				return;
			}
			glBindTexture(GL_TEXTURE_2D, frameCache.texture);
			int tw = 1, th = 1;
			while (tw < w) tw *= 2;
			while (th < h) th *= 2;
			if (tw > frameCache.maximumTextureSize || th > frameCache.maximumTextureSize)
			{
				reportFrameCacheFailure("frame exceeds the OpenGL texture size limit");
				glPopAttrib();
				return;
			}
			if (tw != frameCache.textureWidth || th != frameCache.textureHeight)
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tw, th, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
				// Query the allocation rather than clearing unrelated renderer GL errors.
				GLint allocatedWidth = 0, allocatedHeight = 0;
				glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &allocatedWidth);
				glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &allocatedHeight);
				if (allocatedWidth != tw || allocatedHeight != th)
				{
					reportFrameCacheFailure("OpenGL texture allocation failed");
					glPopAttrib();
					return;
				}
				frameCache.textureWidth = tw;
				frameCache.textureHeight = th;
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				// Windows' GDI renderer exposes GL 1.1, which lacks CLAMP_TO_EDGE.
				// Sampling pixel centers below keeps filtering away from the border.
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			}
			glReadBuffer(GL_BACK);
			glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
			frameCache.width = w;
			frameCache.height = h;
			frameCache.valid = true;
			frameCache.failureReported = false;
			glPopAttrib();
			return;
		}
		#endif
		if (!frameCache.surface || frameCache.surface->w != getW() || frameCache.surface->h != getH())
		{
			SDL_FreeSurface(frameCache.surface);
			frameCache.surface = SDL_CreateRGBSurfaceWithFormat(0, getW(), getH(), 32, sdlsurface->format->format);
			if (!frameCache.surface)
			{
				reportFrameCacheFailure(SDL_GetError());
				return;
			}
			SDL_SetSurfaceBlendMode(frameCache.surface, SDL_BLENDMODE_NONE);
		}
		SDL_SetSurfaceBlendMode(sdlsurface, SDL_BLENDMODE_NONE);
		if (SDL_BlitSurface(sdlsurface, nullptr, frameCache.surface, nullptr) != 0)
		{
			reportFrameCacheFailure(SDL_GetError());
			return;
		}
		frameCache.valid = true;
		frameCache.failureReported = false;
	}

	void GraphicContext::swapBuffers()
	{
		SDL_GL_SwapWindow(window);
	}

	void GraphicContext::presentLastFrame()
	{
		if (!frameCache.valid || presenting || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return;
		FlagScope scope(presenting);
		#ifdef HAVE_OPENGL
		if (optionFlags & USEGPU)
		{
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
			glBindTexture(GL_TEXTURE_2D, frameCache.texture);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glClearColor(0, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			const float scale = std::min(float(w) / frameCache.width, float(h) / frameCache.height);
			const int width = int(frameCache.width * scale + 0.5f), height = int(frameCache.height * scale + 0.5f);
			glViewport((w - width) / 2, (h - height) / 2, width, height);
			glMatrixMode(GL_PROJECTION);
			glPushMatrix();
			glLoadIdentity();
			glMatrixMode(GL_MODELVIEW);
			glPushMatrix();
			glLoadIdentity();
			glMatrixMode(GL_TEXTURE);
			glPushMatrix();
			glLoadIdentity();
			// Sample pixel centers to avoid filtering into the unused power-of-two padding.
			const float left = 0.5f / frameCache.textureWidth, right = (frameCache.width - 0.5f) / frameCache.textureWidth;
			const float bottom = 0.5f / frameCache.textureHeight, top = (frameCache.height - 0.5f) / frameCache.textureHeight;
			glBegin(GL_QUADS);
			glTexCoord2f(left, bottom); glVertex2f(-1, -1);
			glTexCoord2f(right, bottom); glVertex2f(1, -1);
			glTexCoord2f(right, top); glVertex2f(1, 1);
			glTexCoord2f(left, top); glVertex2f(-1, 1);
			glEnd();
			glPopMatrix();
			glMatrixMode(GL_MODELVIEW);
			glPopMatrix();
			glMatrixMode(GL_PROJECTION);
			glPopMatrix();
			glPopAttrib();
			swapBuffers();
			return;
		}
		#endif
		SDL_Surface *target = SDL_GetWindowSurface(window);
		if (!target || target->w <= 0 || target->h <= 0) return;
		const float scale = std::min(float(target->w) / frameCache.surface->w, float(target->h) / frameCache.surface->h);
		SDL_Rect dst{0, 0, int(frameCache.surface->w * scale + 0.5f), int(frameCache.surface->h * scale + 0.5f)};
		dst.x = (target->w - dst.w) / 2;
		dst.y = (target->h - dst.h) / 2;
		// An opaque, full-window copy already overwrites every pixel.
		if (dst.w != target->w || dst.h != target->h)
			SDL_FillRect(target, nullptr, SDL_MapRGB(target->format, 0, 0, 0));
		if (dst.w == frameCache.surface->w && dst.h == frameCache.surface->h)
			SDL_BlitSurface(frameCache.surface, nullptr, target, &dst);
		else
			SDL_BlitScaled(frameCache.surface, nullptr, target, &dst);
		SDL_UpdateWindowSurface(window);
	}
}
