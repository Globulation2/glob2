// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Internal header shared between graphic_context*.cpp and drawable_surface*.cpp.
// Not part of the public libgag API.

#pragma once

#include <GraphicContext.h>
#include <SDL.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_OPENGL
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/glu.h>
#define GL_TEXTURE_RECTANGLE_NV GL_TEXTURE_RECTANGLE_EXT
#else
#include <epoxy/gl.h>
#ifdef _WIN32
#include <epoxy/wgl.h>
#else
#include <epoxy/glx.h>
#endif
#endif // defined(__APPLE__)
#endif // HAVE_OPENGL

#ifdef HAVE_OPENGL
#define GL_GLEXT_PROTOTYPES
	#if defined(__APPLE__) || defined(OPENGL_HEADER_DIRECTORY_OPENGL)
		#include <OpenGL/gl.h>
		#include <OpenGL/glext.h>
		#include <OpenGL/glu.h>
		#define GL_TEXTURE_RECTANGLE_NV GL_TEXTURE_RECTANGLE_EXT
	#else
		#include <GL/gl.h>
		#include <GL/glu.h>
	#endif
#endif

#ifdef WIN32
	#include <GL/glext.h>
#endif

namespace GAGCore
{
	// The active graphic context. Set by GraphicContext::setRes.
	extern GraphicContext *_gc;
	// SDL pixel format used for GL uploads. Configured by GraphicContext::setRes.
	extern SDL_PixelFormat _glFormat;
	// EXPERIMENTAL is a bit buggy and "not EXPERIMENTAL" is bugfree but slow
	// when rendering clouds or other density layers (GraphicContext::drawAlphaMap).
	extern const bool EXPERIMENTAL;

#ifdef HAVE_OPENGL
	// Cache for GL state, call gl only if necessary. GL optimisations.
	struct GLState
	{
		static const bool verbose = false;
		bool _doBlend;
		bool _doTexture;
		bool _doScissor;
		GLint _texture;
		GLenum _sfactor, _dfactor;
		bool isTextureSRectangle;
		bool useATIWorkaround;
		unsigned alocatedTextureCount;

		GLState(void)
		{
			resetCache();
			isTextureSRectangle = false;
			useATIWorkaround = false;
			alocatedTextureCount = 0;
		}

		void resetCache(void)
		{
			_doBlend = false;
			_doTexture = false;
			_doScissor = false;
			_texture = -1;
			_sfactor = 0xffffffff;
			_dfactor = 0xffffffff;
		}

		void checkExtensions(void);
		bool doBlend(bool on);
		bool doTexture(bool on);
		void setTexture(int tex);
		bool doScissor(bool on);
		void blendFunc(GLenum sfactor, GLenum dfactor);
	};

	extern GLState glState;
#endif // HAVE_OPENGL
}
