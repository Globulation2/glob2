/*
 * GAG SDL graphic context file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __SDLGRAPHICCONTEXT_H
#define __SDLGRAPHICCONTEXT_H

#include "Header.h"
#include "GraphicContext.h"

class SDLDrawableSurface: public virtual DrawableSurface
{
protected:
	SDL_Surface *surface;
	SDL_Rect clipRect;

public:
	SDLDrawableSurface() { surface=NULL; }
	virtual ~SDLDrawableSurface() { if (surface) SDL_FreeSurface(surface); }
	virtual bool setRes(int w, int h, int depth=32, Uint32 flags=DEFAULT);
	virtual void setAlpha(bool usePerPixelAlpha=false, Uint8 alphaValue=ALPHA_OPAQUE);
	/*virtual*/ int getW(void) { return surface->w; }
	/*virtual*/ int getH(void) { return surface->h; }
	virtual void setClipRect(int x, int y, int w, int h);
	virtual void setClipRect(void);
	virtual void drawSprite(int x, int y, Sprite *sprite, int index=0);
	virtual void drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawCircle(int x, int y, int ray, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
	virtual void drawString(int x, int y, const Font *font, const char *msg, ...);
	virtual void drawSurface(int x, int y, DrawableSurface *surface);
	virtual void updateRects(SDL_Rect *rects, int size) { }
	virtual void updateRect(int x, int y, int w, int h) { }
};

class SDLGraphicContext:  public SDLDrawableSurface, public virtual GraphicContext
{
private:
	SDL_RWops *tryOpenImage(const char *name, int number, bool overlay);

public:
	SDLGraphicContext(void);
	virtual ~SDLGraphicContext(void);

	virtual bool setRes(int w, int h, int depth=32, Uint32 flags=DEFAULT);
	virtual void setCaption(const char *title, const char *icon) { SDL_WM_SetCaption(title, icon); }

	virtual void dbgprintf(const char *msg, ...);

	virtual Sprite *loadSprite(const char *name);
	virtual Font *loadFont(const char *name);
	virtual DrawableSurface *createDrawableSurface(void);

	virtual void nextFrame(void);
	virtual void updateRects(SDL_Rect *rects, int size);
	virtual void updateRect(int x, int y, int w, int h);
};


#endif
