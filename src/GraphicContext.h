/*
 * GAG graphic context file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GRAPHICCONTEXT_H
#define __GRAPHICCONTEXT_H

#include "Header.h"
#include "Sprite.h"

class Font
{
public:
	virtual ~Font() { }
	virtual bool load(const char *filename)=0;
	virtual int getStringWidth(const char *string) const=0;
	virtual int getStringHeight(const char *string) const=0;
	virtual bool printable(char c)=0;
};

class GraphicContext
{
public:
	virtual ~GraphicContext(void) { }

	virtual bool setRes(int w, int h, int depth=16, Uint32 flags=SDL_SWSURFACE)=0;

	virtual void dbgprintf(const char *msg, ...)=0;

	virtual void setClipRect(int x, int y, int w, int h)=0;
	virtual void setClipRect(SDL_Rect *rect)=0;

	virtual int getW(void)=0;
	virtual int getH(void)=0;

	virtual void drawSprite(Sprite *sprite, int x, int y)=0;
	virtual void drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE)=0;
	virtual void drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE)=0;
	virtual void drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE)=0;
	virtual void drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE)=0;
	virtual void drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE)=0;
	virtual void drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE)=0;
	virtual void drawCircle(int x, int y, int ray, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE)=0;
	virtual void drawString(int x, int y, const Font *font, const char *msg, ...)=0;
	
	virtual void nextFrame(void)=0;
};


class SDLFont:public Font
{
public:
	virtual ~SDLFont() { }

protected:
	friend class SDLGraphicContext;

	virtual void drawString(SDL_Surface *Surface, int x, int y, const char *text, SDL_Rect *clip=NULL) const=0;
};

class SDLBitmapFont: public SDLFont
{
public:	
	SDLBitmapFont(const char *filename);
	virtual ~SDLBitmapFont();
	bool load(const char *filename);
	int getStringWidth(const char *string) const;
	int getStringHeight(const char *string) const;
	bool printable(char c);

protected:
	friend class SDLGraphicContext;

	void drawString(SDL_Surface *Surface, int x, int y, const char *text, SDL_Rect *clip=NULL) const;

	bool load(SDL_Surface *fontSurface);

	void init();
	// Returns the width of "text" in pixels
	int textWidth(const char *text, int min=0, int max=255) const;
	

	static const int startChar = 33;
	int lastChar;

protected:
	int height;
	SDL_Surface *picture;
	int *CharPos;

	int spacew;

	void getPixel(Sint32 x, Sint32 y, Uint8 *r, Uint8 *g, Uint8 *b);
	void setBackGround(Sint32 x, Sint32 y);
	bool isBackGround(Sint32 x, Sint32 y);
	Uint8 backgroundR;
	Uint8 backgroundG;
	Uint8 backgroundB;

	bool doStartNewChar(int x);
	int shorteringChar(int x);

};

class SDLGraphicContext: public GraphicContext
{
public:
	SDLGraphicContext(void);
	virtual ~SDLGraphicContext(void);

	virtual bool setRes(int w, int h, int depth=16, Uint32 flags=SDL_SWSURFACE);

	virtual void dbgprintf(const char *msg, ...);

	virtual void setClipRect(int x, int y, int w, int h);
	virtual void setClipRect(SDL_Rect *rect);
	
	virtual int getW(void) { return screen->w; }
	virtual int getH(void) { return screen->h; }

	virtual void drawSprite(Sprite *sprite, int x, int y);
	virtual void drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE);
	virtual void drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE);
	virtual void drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE);
	virtual void drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE);
	virtual void drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE);
	virtual void drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE);
	virtual void drawCircle(int x, int y, int ray, Uint8 r, Uint8 g, Uint8 b, Uint8 a=SDL_ALPHA_OPAQUE);
	virtual void drawString(int x, int y, const Font *font, const char *msg, ...);

	virtual void nextFrame(void);

public:
	SDL_Surface *screen;
	SDL_Rect clipRect;
};

class SDLOffScreenGraphicContext: public SDLGraphicContext
{
public:
	SDLOffScreenGraphicContext(int w, int h, bool usePerPixelAlpha, Uint8 alphaValue);
	virtual ~SDLOffScreenGraphicContext(void) { if (screen) SDL_FreeSurface(screen); }

	virtual bool setRes(int w, int h, int depth=16, Uint32 flags=SDL_SWSURFACE) { return false; }

	virtual void nextFrame(void) { }
};


#endif
