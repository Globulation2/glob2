/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�e
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __GRAPHICCONTEXT_H
#define __GRAPHICCONTEXT_H

#include "GAGSys.h"

class Font;
class Sprite;

class DrawableSurface
{
public:
	enum GraphicContextType
	{
		GC_SDL=0,
		GC_GL=1
	};

	enum Alpha
	{
		ALPHA_OPAQUE=255
	};

	enum ResolutionFlags
	{
		DEFAULT=0,
		DOUBLEBUF=1,
		FULLSCREEN=2,
		HWACCELERATED=4,
		RESIZABLE=8
	};

public:
	virtual ~DrawableSurface(void) { }
	virtual bool setRes(int w, int h, int depth=32, Uint32 flags=DEFAULT)=0;
	virtual void setAlpha(bool usePerPixelAlpha=false, Uint8 alphaValue=ALPHA_OPAQUE)=0;
	virtual int getW(void)=0;
	virtual int getH(void)=0;
	virtual int getDepth(void)=0;
	virtual int getFlags(void)=0;
	virtual void setClipRect(int x, int y, int w, int h)=0;
	virtual void setClipRect(void)=0;
	virtual void loadImage(const char *name)=0;
	virtual void drawSprite(int x, int y, Sprite *sprite, int index=0)=0;
	virtual void drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE)=0;
	virtual void drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE)=0;
	virtual void drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE)=0;
	virtual void drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE)=0;
	virtual void drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE)=0;
	virtual void drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE)=0;
	virtual void drawCircle(int x, int y, int ray, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE)=0;
	virtual void drawString(int x, int y, const Font *font, int i)=0;
	virtual void drawString(int x, int y, const Font *font, const char *msg)=0;
	virtual void drawString(int x, int y, int w, const Font *font, const char *msg)=0;
	virtual void drawSurface(int x, int y, DrawableSurface *surface)=0;
	virtual void updateRects(SDL_Rect *rects, int size)=0;
	virtual void updateRect(int x, int y, int w, int h)=0;
};

class GraphicContext:public virtual DrawableSurface
{
private:
	SDL_Rect **modes;

public:
	virtual ~GraphicContext(void);

	//! this must be called before any Drawable Surface method.
	virtual bool setRes(int w, int h, int depth=32, Uint32 flags=DEFAULT)=0;
	virtual void beginVideoModeListing(void);
	virtual bool getNextVideoMode(int *w, int *h);
	virtual void setCaption(const char *title, const char *icon)=0;

	virtual void loadSprite(const char *filename, const char *name)=0;

	virtual void loadFont(const char *filename, unsigned size, const char *name)=0;

	virtual DrawableSurface *createDrawableSurface(const char *name=NULL)=0;

	virtual void nextFrame(void)=0;
	virtual void printScreen(const char *filename)=0;

	static GraphicContext *createGraphicContext(GraphicContextType type);
};

class Font
{
public:
	enum Style
	{
		STYLE_NORMAL = 0x00,
		STYLE_BOLD = 0x01,
		STYLE_ITALIC = 0x02,
		STYLE_UNDERLINE = 0x04,
	};

public:
	virtual ~Font() { }

	// width and height
	virtual int getStringWidth(const char *string) const=0;
	virtual int getStringWidth(const char *string, int len) const;
	virtual int getStringWidth(const int i) const;
	virtual int getStringHeight(const char *string) const=0;
	virtual int getStringHeight(const char *string, int len) const;
	virtual int getStringHeight(const int i) const;
	virtual bool printable(char c) const=0;

	// Style and color
	virtual void setColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a = DrawableSurface::ALPHA_OPAQUE) { }
	virtual void pushColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a = DrawableSurface::ALPHA_OPAQUE) { }
	virtual void popColor(void) { }
	virtual void getColor(Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a) const { }

	virtual void setStyle(unsigned style) { }
	virtual void pushStyle(unsigned style) { }
	virtual void popStyle(void) { }
	virtual unsigned getStyle(void) const { return 0; }
};


class Sprite
{
public:
	virtual ~Sprite() { }
	virtual void setBaseColor(Uint8 r, Uint8 g, Uint8 b)=0;
	virtual int getW(int index)=0;
	virtual int getH(int index)=0;
};


#endif
