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

#ifndef __SDLGRAPHICCONTEXT_H
#define __SDLGRAPHICCONTEXT_H

#include <GAGSys.h>
#include "GraphicContext.h"

class SDLDrawableSurface: public virtual DrawableSurface
{
protected:
	friend class SDLSprite;
	SDL_Surface *surface;
	SDL_Rect clipRect;
	Uint32 flags;
	
public:
	SDLDrawableSurface();
	virtual ~SDLDrawableSurface() { if (surface) SDL_FreeSurface(surface); }
	virtual bool setRes(int w, int h, int depth=32, Uint32 flags=DEFAULT);
	virtual void setAlpha(bool usePerPixelAlpha=false, Uint8 alphaValue=ALPHA_OPAQUE);
	virtual int getW(void) { if(surface) return surface->w; else return 0;}
	virtual int getH(void) { if(surface) return surface->h; else return 0; }
	virtual int getDepth(void) { if(surface) return surface->format->BitsPerPixel; else return 0; }
	virtual int getFlags(void) { return flags; }
	virtual void setClipRect(int x, int y, int w, int h);
	virtual void setClipRect(void);
	virtual void loadImage(const char *name);
	virtual void drawSprite(int x, int y, Sprite *sprite, int index=0);
	virtual void drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawCircle(int x, int y, int ray, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawString(int x, int y, const Font *font, int i);
	virtual void drawString(int x, int y, const Font *font, const char *msg);
	virtual void drawString(int x, int y, int w, const Font *font, const char *msg);
	virtual void drawSurface(int x, int y, DrawableSurface *surface);
	virtual void updateRects(SDL_Rect *rects, int size) { }
	virtual void updateRect(int x, int y, int w, int h) { }
};

class SDLGraphicContext:  public SDLDrawableSurface, public virtual GraphicContext
{
public:
	SDLGraphicContext(void);
	virtual ~SDLGraphicContext(void);

	virtual bool setRes(int w, int h, int depth=32, Uint32 flags=DEFAULT);
	virtual void setCaption(const char *title, const char *icon) { SDL_WM_SetCaption(title, icon); }

	virtual void loadImage(const char *name);

	virtual void loadFont(const char *filename, unsigned size, const char *name);
	virtual DrawableSurface *createDrawableSurface(const char *name=NULL);

	virtual void nextFrame(void);
	virtual void updateRects(SDL_Rect *rects, int size);
	virtual void updateRect(int x, int y, int w, int h);
	
	virtual void printScreen(const char *filename);
};


#endif
