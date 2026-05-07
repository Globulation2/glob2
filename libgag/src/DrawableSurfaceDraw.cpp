// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GraphicContextPrivate.h"
#include <algorithm>
#include <cstdlib>

namespace GAGCore
{
	void DrawableSurface::drawPixel(int x, int y, const Color& color)
	{
		// clip
		if ((x<clipRect.x) || (x>=clipRect.x+clipRect.w) || (y<clipRect.y) || (y>=clipRect.y+clipRect.h))
			return;

		// draw
		if (color.a == Color::ALPHA_OPAQUE)
		{
			*(((Uint32 *)sdlsurface->pixels) + y*(sdlsurface->pitch>>2) + x) = color.pack();
		}
		else
		{
			Uint32 a = color.a;
			Uint32 na = 255 - a;
			Uint32 colorValue = color.applyAlpha(Color::ALPHA_OPAQUE).pack();
			Uint32 colorPreMult0 = (colorValue & 0x00FF00FF) * a;
			Uint32 colorPreMult1 = ((colorValue >> 8) & 0x00FF00FF) * a;

			Uint32 *mem = ((Uint32 *)sdlsurface->pixels) + y*(sdlsurface->pitch>>2) + x;

			Uint32 surfaceValue = *mem;
			Uint32 surfacePreMult0 = (surfaceValue & 0x00FF00FF) * na;
			Uint32 surfacePreMult1 = ((surfaceValue >> 8) & 0x00FF00FF) * na;

			surfacePreMult0 += colorPreMult0;
			surfacePreMult1 += colorPreMult1;

			*mem = ((surfacePreMult0 >> 8) & 0x00FF00FF) | (surfacePreMult1 & 0xFF00FF00);
		}
		dirty = true;
	}

	void DrawableSurface::drawPixel(float x, float y, const Color& color)
	{
		drawPixel(static_cast<int>(x), static_cast<int>(y), color);
	}

	// compat
	void DrawableSurface::drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawPixel(x, y, Color(r, g, b, a));
	}

	void DrawableSurface::drawRect(int x, int y, int w, int h, const Color& color)
	{
		_drawHorzLine(x, y, w, color);
		_drawHorzLine(x, y+h-1, w, color);
		_drawVertLine(x, y, h, color);
		_drawVertLine(x+w-1, y, h, color);
	}

	void DrawableSurface::drawRect(float x, float y, float w, float h, const Color& color)
	{
		drawRect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), color);
	}

	// compat
	void DrawableSurface::drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawRect(x, y, w, h, Color(r, g, b, a));
	}

	void DrawableSurface::drawFilledRect(int x, int y, int w, int h, const Color& color)
	{
		// clip
		if (x < clipRect.x)
		{
			w -= clipRect.x - x;
			x = clipRect.x;
		}
		if (y < 0)
		{
			h -= clipRect.y - y;
			y = clipRect.y;
		}
		if (x + w >= clipRect.x + clipRect.w)
		{
			w = clipRect.x + clipRect.w - x;
		}
		if (y + h >= clipRect.y + clipRect.h)
		{
			h = clipRect.y + clipRect.h - y;
		}
		if ((w <= 0) || (h <= 0))
			return;

		// draw
		if (color.a == Color::ALPHA_OPAQUE)
		{
			Uint32 colorValue = color.pack();
			for (int dy = y; dy < y + h; dy++)
			{
				Uint32 *mem = ((Uint32 *)sdlsurface->pixels) + dy*(sdlsurface->pitch>>2) + x;
				int dw = w;
				do
				{
					*mem++ = colorValue;
				}
				while (--dw);
			}
		}
		else
		{
			Uint32 a = color.a;
			Uint32 na = 255 - a;
			Uint32 colorValue = color.applyAlpha(Color::ALPHA_OPAQUE).pack();
			Uint32 colorPreMult0 = (colorValue & 0x00FF00FF) * a;
			Uint32 colorPreMult1 = ((colorValue >> 8) & 0x00FF00FF) * a;

			for (int dy = y; dy < y + h; dy++)
			{
				Uint32 *mem = ((Uint32 *)sdlsurface->pixels) + dy*(sdlsurface->pitch>>2) + x;
				int dw = w;
				do
				{
					Uint32 surfaceValue = *mem;
					Uint32 surfacePreMult0 = (surfaceValue & 0x00FF00FF) * na;
					Uint32 surfacePreMult1 = ((surfaceValue >> 8) & 0x00FF00FF) * na;
					surfacePreMult0 += colorPreMult0;
					surfacePreMult1 += colorPreMult1;
					*mem++ = ((surfacePreMult0 >> 8) & 0x00FF00FF) | (surfacePreMult1 & 0xFF00FF00);
				}
				while (--dw);
			}
		}
		dirty = true;
	}

	void DrawableSurface::drawFilledRect(float x, float y, float w, float h, const Color& color)
	{
		drawFilledRect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), color);
	}

	void DrawableSurface::drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawFilledRect(x, y, w, h, Color(r, g, b, a));
	}

	void DrawableSurface::_drawVertLine(int x, int y, int l, const Color& color)
	{
		// clip
		// be sure we have to draw something
		if ((x < clipRect.x) || (x >= clipRect.x + clipRect.w))
			return;

		// set l positiv
		if (l < 0)
		{
			y += l;
			l = -l;
		}

		// clip on y at top
		if (y < clipRect.y)
		{
			l -= clipRect.y - y;
			y = clipRect.y;
		}

		// clip on y at bottom
		if (y + l >= clipRect.y + clipRect.h)
		{
			l = clipRect.y + clipRect.h - y;
		}

		// again, be sure we have to draw something
		if (l <= 0)
			return;

		// draw
		int increment = sdlsurface->pitch >> 2;
		Uint32 *mem = ((Uint32 *)sdlsurface->pixels) + y*increment + x;
		if (color.a == Color::ALPHA_OPAQUE)
		{
			Uint32 colorValue = color.pack();

			do
			{
				*mem = colorValue;
				mem += increment;
			}
			while (--l);
		}
		else
		{
			Uint32 a = color.a;
			Uint32 na = 255 - a;
			Uint32 colorValue = color.applyAlpha(Color::ALPHA_OPAQUE).pack();
			Uint32 colorPreMult0 = (colorValue & 0x00FF00FF) * a;
			Uint32 colorPreMult1 = ((colorValue >> 8) & 0x00FF00FF) * a;

			do
			{
				Uint32 surfaceValue = *mem;
				Uint32 surfacePreMult0 = (surfaceValue & 0x00FF00FF) * na;
				Uint32 surfacePreMult1 = ((surfaceValue >> 8) & 0x00FF00FF) * na;
				surfacePreMult0 += colorPreMult0;
				surfacePreMult1 += colorPreMult1;
				*mem = ((surfacePreMult0 >> 8) & 0x00FF00FF) | (surfacePreMult1 & 0xFF00FF00);
				mem += increment;
			}
			while (--l);
		}
		dirty = true;
	}

	void DrawableSurface::_drawHorzLine(int x, int y, int l, const Color& color)
	{
		// clip
		// be sure we have to draw something
		if ((y < clipRect.y) || (y >= clipRect.y + clipRect.h))
			return;

		// set l positiv
		if (l < 0)
		{
			x += l;
			l = -l;
		}

		// clip on x at left
		if (x < clipRect.x)
		{
			l -= clipRect.x - x;
			x = clipRect.x;
		}

		// clip on x at right
		if ( x + l >= clipRect.x + clipRect.w)
		{
			l = clipRect.x + clipRect.w - x;
		}

		// again, be sure we have to draw something
		if (l <= 0)
			return;

		// draw
		Uint32 *mem = ((Uint32 *)sdlsurface->pixels) + y*(sdlsurface->pitch >> 2) + x;
		if (color.a == Color::ALPHA_OPAQUE)
		{
			Uint32 colorValue = color.pack();

			do
			{
				*mem++ = colorValue;
			}
			while (--l);
		}
		else
		{
			Uint32 a = color.a;
			Uint32 na = 255 - a;
			Uint32 colorValue = color.applyAlpha(Color::ALPHA_OPAQUE).pack();
			Uint32 colorPreMult0 = (colorValue & 0x00FF00FF) * a;
			Uint32 colorPreMult1 = ((colorValue >> 8) & 0x00FF00FF) * a;

			do
			{
				Uint32 surfaceValue = *mem;
				Uint32 surfacePreMult0 = (surfaceValue & 0x00FF00FF) * na;
				Uint32 surfacePreMult1 = ((surfaceValue >> 8) & 0x00FF00FF) * na;
				surfacePreMult0 += colorPreMult0;
				surfacePreMult1 += colorPreMult1;
				*mem++ = ((surfacePreMult0 >> 8) & 0x00FF00FF) | (surfacePreMult1 & 0xFF00FF00);
			}
			while (--l);
		}
		dirty = true;
	}

	void DrawableSurface::drawLine(int x1, int y1, int x2, int y2, const Color& _color)
	{
		// we want to modify the color
		Color color = _color;

		// compute deltas
		int dx = x2 - x1;
		if (dx == 0)
		{
			_drawVertLine(x1, y1, y2-y1, color);
			return;
		}
		int dy = y2 - y1;
		if (dy == 0)
		{
			_drawHorzLine(x1, y1, x2-x1, color);
			return;
		}

		// clip
		int test = 1;
		// Y clipping
		if (dy < 0)
		{
			test = -test;
			std::swap(x1, x2);
			std::swap(y1, y2);
			dx = -dx;
			dy = -dy;
		}

		// the 2 points are Y-sorted. (y1 <= y2)
		if (y2 < clipRect.y)
			return;
		if (y1 >= clipRect.y + clipRect.h)
			return;
		if (y1 < clipRect.y)
		{
			x1 = x2 - ( (y2 - clipRect.y)*(x2-x1) ) / (y2-y1);
			y1 = clipRect.y;
		}
		if (y1 == y2)
		{
			_drawHorzLine(x1, y1, x2-x1, color);
			return;
		}
		if (y2 >= clipRect.y + clipRect.h)
		{
			x2 = x1 - ( (y1 - (clipRect.y + clipRect.h))*(x1-x2) ) / (y1-y2);
			y2 = (clipRect.y + clipRect.h - 1);
		}
		if (x1 == x2)
		{
			_drawVertLine(x1, y1, y2-y1, color);
			return;
		}

		// X clipping
		if (dx < 0)
		{
			test = -test;
			std::swap(x1, x2);
			std::swap(y1, y2);
			dx = -dx;
			dy = -dy;
		}
		// the 2 points are X-sorted. (x1 <= x2)
		if (x2 < clipRect.x)
			return;
		if (x1 >= clipRect.x + clipRect.w)
			return;
		if (x1 < clipRect.x)
		{
			y1 = y2 - ( (x2 - clipRect.x)*(y2-y1) ) / (x2-x1);
			x1 = clipRect.x;
		}
		if (x1 == x2)
		{
			_drawVertLine(x1, y1, y2-y1, color);
			return;
		}
		if (x2 >= clipRect.x + clipRect.w)
		{
			y2 = y1 - ( (x1 - (clipRect.x + clipRect.w))*(y1-y2) ) / (x1-x2);
			x2 = (clipRect.x + clipRect.w - 1);
		}

		// last return case
		if (x1 >= (clipRect.x + clipRect.w) || y1 >= (clipRect.y + clipRect.h) || (x2 < clipRect.x) || (y2 < clipRect.y))
			return;

		// recompute deltas after clipping
		dx = x2-x1;
		dy = y2-y1;

		// setup variable to draw alpha in the right direction
		#define Sgn(x) (x>0 ? (x == 0 ? 0 : 1) : (x==0 ? 0 : -1))
		Sint32 littleincx;
		Sint32 littleincy;
		Sint32 bigincx;
		Sint32 bigincy;
		Sint32 alphadecx;
		Sint32 alphadecy;
		if (abs(dx) > abs(dy))
		{
			littleincx = 1;
			littleincy = 0;
			bigincx = 1;
			bigincy = Sgn(dy);
			alphadecx = 0;
			alphadecy = Sgn(dy);
		}
		else
		{
			// we swap x and y meaning
			test = -test;
			std::swap(dx, dy);
			littleincx = 0;
			littleincy = 1;
			bigincx = Sgn(dx);
			bigincy = 1;
			alphadecx = 1;
			alphadecy = 0;
		}

		if (dx < 0)
		{
			dx = -dx;
			littleincx = 0;
			littleincy = -littleincy;
			bigincx = -bigincx;
			bigincy = -bigincy;
			alphadecy = -alphadecy;
		}

		// compute initial position
		int px, py;
		px = x1;
		py = y1;

		// variable initialisation for bresenham algo
		if (dx == 0)
			return;
		if (dy == 0)
			return;
		const int FIXED = 8;
		const int I = 255; // number of degree of alpha
		const int Ibits = 8;
		int m = (abs(dy) << (Ibits+FIXED)) / abs(dx);
		int w = (I << FIXED) - m;
		int e = 1 << (FIXED-1);

		// first point
		color.a = I - (e >> FIXED);
		drawPixel(px, py, color);

		// main loop
		int x = dx+1;
		if (x <= 0)
			return;
		while (--x)
		{
			if (e < w)
			{
				px+=littleincx;
				py+=littleincy;
				e+= m;
			}
			else
			{
				px+=bigincx;
				py+=bigincy;
				e-= w;
			}
			color.a = I - (e >> FIXED);
			drawPixel(px, py, color);
			color.a = e >> FIXED;
			drawPixel(px + alphadecx, py + alphadecy, color);
		}
	}

	void DrawableSurface::drawLine(float x1, float y1, float x2, float y2, const Color& color)
	{
		drawRect(static_cast<int>(x1), static_cast<int>(y1), static_cast<int>(x2), static_cast<int>(y2), color);
	}

	void DrawableSurface::drawVertLine(int x, int y, int l, const Color& color)
	{
		 _drawVertLine(x, y, l, color);
	}

	void DrawableSurface::drawHorzLine(int x, int y, int l, const Color& color)
	{
		_drawHorzLine(x, y, l, color);
	}
}
