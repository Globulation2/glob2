// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GraphicContextPrivate.h"
#include <math.h>

namespace GAGCore
{
	void GraphicContext::setClipRect(int x, int y, int w, int h)
	{
		DrawableSurface::setClipRect(x, y, w, h);
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			glState.doScissor(true);
			// glScissor operates in window pixels; scale from logical coordinates when fullscreen scaling is active
			int sx = clipRect.x, sy = getH() - clipRect.y - clipRect.h, sw = clipRect.w, sh = clipRect.h;
			if (isScalingActive())
			{
				sx = sx * windowW / getW();
				sy = sy * windowH / getH();
				sw = sw * windowW / getW();
				sh = sh * windowH / getH();
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

			// GL line width is fixed in framebuffer pixels and doesn't grow
			// with the viewport the way filled geometry does, so fullscreen
			// scaling leaves hairline UI elements (progress bar markers, etc.)
			// looking thinner than the rest of the scaled-up interface.
			if (isScalingActive())
			{
				float scale = static_cast<float>(windowW) / getW();
				float scaleH = static_cast<float>(windowH) / getH();
				if (scaleH > scale)
					scale = scaleH;
				glLineWidth(scale);
			}

			// draw
			glBegin(GL_LINES);
			if (color.a < 255)
				glColor4ub(color.r, color.g, color.b, color.a);
			else
				glColor3ub(color.r, color.g, color.b);
			glVertex2f(x1, y1);
			glVertex2f(x2, y2);
			glEnd();

			if (isScalingActive())
				glLineWidth(1);
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
			glLineWidth(2);

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
			glLineWidth(1);
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

	void GraphicContext::drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawLine(x, y, x, y+l, Color(r, g, b, a));
		else
		#endif
			 _drawVertLine(x, y, l, Color(r, g, b, a));
	}

	void GraphicContext::drawVertLine(int x, int y, int l, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawLine(x, y, x, y+l, color);
		else
		#endif
			 _drawVertLine(x, y, l, color);
	}

	void GraphicContext::drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawLine(x, y, x+l, y, Color(r, g, b, a));
		else
		#endif
			_drawHorzLine(x, y, l, Color(r, g, b, a));
	}

	void GraphicContext::drawHorzLine(int x, int y, int l, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawLine(x, y, x+l, y, color);
		else
		#endif
			_drawHorzLine(x, y, l, color);
	}

	void GraphicContext::drawCircle(int x, int y, int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawCircle(x, y, radius, Color(r, g, b, a));
	}
}
