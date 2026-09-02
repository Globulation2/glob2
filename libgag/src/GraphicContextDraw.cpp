// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <cmath>
#include <algorithm>
#include "GraphicContextPrivate.h"

namespace GAGCore
{
	// GL rasterises lines at a width in drawable pixels, which the viewport
	// transform does not scale the way it scales filled geometry.
	void GraphicContext::setScaledLineWidth(float width)
	{
		#ifdef HAVE_OPENGL
		glLineWidth(width * drawableScale());
		#endif
	}

	void GraphicContext::setClipRect(int x, int y, int w, int h)
	{
		DrawableSurface::setClipRect(x, y, w, h);
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			glState.doScissor(true);
			// glScissor operates in drawable pixels; scale from logical coordinates
			int sx = clipRect.x, sy = getH() - clipRect.y - clipRect.h, sw = clipRect.w, sh = clipRect.h;
			if (drawableW && (drawableW != getW() || drawableH != getH()))
			{
				sx = sx * drawableW / getW();
				sy = sy * drawableH / getH();
				sw = sw * drawableW / getW();
				sh = sh * drawableH / getH();
			}
			glScissor(sx, sy, sw, sh);
		}
		#endif
	}

	void GraphicContext::setClipRect(void)
	{
		DrawableSurface::setClipRect();
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
			glState.doScissor(false);
		#endif
	}

	// drawing, reimplementation for GL

	void GraphicContext::drawPixel(int x, int y, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			GraphicContext::drawPixel(static_cast<float>(x), static_cast<float>(y), color);
		else
		#endif
			DrawableSurface::drawPixel(x, y, color);
	}

	void GraphicContext::drawPixel(float x, float y, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawFilledRect(x, y, 1.0f, 1.0f, color);
		else
		#endif
			DrawableSurface::drawPixel(static_cast<int>(x), static_cast<int>(y), color);
	}


	void GraphicContext::drawRect(int x, int y, int w, int h, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			GraphicContext::drawRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), color);
		else
		#endif
			DrawableSurface::drawRect(x, y, w, h, color);
	}

	void GraphicContext::drawRect(float x, float y, float w, float h, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
		{
			// state change
			glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glState.doBlend(true);
			glState.doTexture(false);
			setScaledLineWidth(1.0f);

			// draw
			glBegin(GL_LINES);
			if (color.a < 255)
				glColor4ub(color.r, color.g, color.b, color.a);
			else
				glColor3ub(color.r, color.g, color.b);
			glVertex2f(x, y);     glVertex2f(x+w, y);
			glVertex2f(x+w, y);   glVertex2f(x+w, y+h);
			glVertex2f(x+w, y+h); glVertex2f(x, y+h);
			glVertex2f(x, y+h);   glVertex2f(x, y);
			glEnd();
		}
		else
		#endif
			DrawableSurface::drawRect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), color);
	}


	void GraphicContext::drawFilledRect(int x, int y, int w, int h, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			GraphicContext::drawFilledRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), color);
		else
		#endif
			DrawableSurface::drawFilledRect(x, y, w, h, color);
	}

	void GraphicContext::drawFilledRect(float x, float y, float w, float h, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
		{
			// state change
			if (color.a < 255)
				glState.doBlend(true);
			else
				glState.doBlend(false);
			glState.doTexture(false);

			// draw
			glBegin(GL_QUADS);
			if (color.a < 255)
				glColor4ub(color.r, color.g, color.b, color.a);
			else
				glColor3ub(color.r, color.g, color.b);
			glVertex2f(x, y);
			glVertex2f(x+w, y);
			glVertex2f(x+w, y+h);
			glVertex2f(x, y+h);
			glEnd();
		}
		else
		#endif
			DrawableSurface::drawFilledRect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), color);
	}


	void GraphicContext::drawLine(int x1, int y1, int x2, int y2, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			GraphicContext::drawLine(static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(x2), static_cast<float>(y2), color);
		else
		#endif
			DrawableSurface::drawLine(x1, y1, x2, y2, color);
	}

	void GraphicContext::drawLine(float x1, float y1, float x2, float y2, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
		{
			// state change
			glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glState.doBlend(true);
			glState.doTexture(false);

			// Drivers only antialias 1 px lines, so a scaled line is built from
			// several 1 px smooth lines spread across the scaled thickness.
			const float scale = drawableScale();
			const int passes = std::max(1, static_cast<int>(std::ceil(scale)));
			float nx = 0.0f, ny = 0.0f;
			if (passes > 1)
			{
				const float ddx = x2 - x1, ddy = y2 - y1;
				const float len = std::sqrt(ddx * ddx + ddy * ddy);
				if (len > 0.0f)
				{
					nx = -ddy / len;
					ny = ddx / len;
				}
			}
			glLineWidth(1.0f);

			// draw
			glBegin(GL_LINES);
			if (color.a < 255)
			{
				// the passes overlap, so each carries the alpha that composes back to the requested one
				const float a = 1.0f - std::pow(1.0f - color.a / 255.0f, 1.0f / passes);
				glColor4ub(color.r, color.g, color.b, static_cast<Uint8>(a * 255.0f + 0.5f));
			}
			else
				glColor3ub(color.r, color.g, color.b);
			for (int i = 0; i < passes; ++i)
			{
				// offsets in window pixels from -(scale-1)/2 to +(scale-1)/2, mapped back to logical units
				const float off = passes > 1 ? ((scale - 1.0f) * (static_cast<float>(i) / (passes - 1) - 0.5f)) / scale : 0.0f;
				glVertex2f(x1 + nx * off, y1 + ny * off);
				glVertex2f(x2 + nx * off, y2 + ny * off);
			}
			glEnd();
		}
		else
		#endif
			DrawableSurface::drawLine(static_cast<int>(x1), static_cast<int>(y1), static_cast<int>(x2), static_cast<int>(y2), color);
	}


	void GraphicContext::drawCircle(int x, int y, int radius, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawCircle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(radius), color);
		else
		#endif
			DrawableSurface::drawCircle(x, y, radius, color);
	}

	void GraphicContext::drawCircle(float x, float y, float radius, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
		{
			glState.doBlend(true);
			glState.doTexture(false);
			setScaledLineWidth(2.0f);

			double tot = radius;
			double fx = x;
			double fy = y;
			double fray = radius;

			glBegin(GL_LINES);
			if (color.a < 255)
				glColor4ub(color.r, color.g, color.b, color.a);
			else
				glColor3ub(color.r, color.g, color.b);
			for (int i=0; i<tot; i++)
			{
				double angle0 = (2*M_PI*(double)i)/((double)tot);
				double angle1 = (2*M_PI*(double)(i+1))/((double)tot);
				glVertex2d(fx+fray*sin(angle0), fy+fray*cos(angle0));
				glVertex2d(fx+fray*sin(angle1), fy+fray*cos(angle1));
			}
			glEnd();
			setScaledLineWidth(1.0f);
		}
		else
		#endif
			DrawableSurface::drawCircle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(radius), color);
	}

	// compat... this is there because it sems gcc is not able to do function overloading with several levels of inheritance
	void GraphicContext::drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawPixel(x, y, Color(r, g, b, a));
	}

	void GraphicContext::drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawRect(x, y, w, h, Color(r, g, b, a));
	}

	void GraphicContext::drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawFilledRect(x, y, w, h, Color(r, g, b, a));
	}

	void GraphicContext::drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawLine(x1, y1, x2, y2, Color(r, g, b, a));
	}

	// Axis-aligned 1 px lines are drawn as exact 1x l quads in GL mode: a GL_LINES
	// primitive keeps an integer device width, so at a fractional fullscreen scale
	// adjacent lines (charts, sliders, training bars) leave gaps between them.
	void GraphicContext::drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawFilledRect(x, y, 1, l, Color(r, g, b, a));
		else
		#endif
			 _drawVertLine(x, y, l, Color(r, g, b, a));
	}

	void GraphicContext::drawVertLine(int x, int y, int l, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawFilledRect(x, y, 1, l, color);
		else
		#endif
			 _drawVertLine(x, y, l, color);
	}

	void GraphicContext::drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawFilledRect(x, y, l, 1, Color(r, g, b, a));
		else
		#endif
			_drawHorzLine(x, y, l, Color(r, g, b, a));
	}

	void GraphicContext::drawHorzLine(int x, int y, int l, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawFilledRect(x, y, l, 1, color);
		else
		#endif
			_drawHorzLine(x, y, l, color);
	}

	void GraphicContext::drawCircle(int x, int y, int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawCircle(x, y, radius, Color(r, g, b, a));
	}
}
