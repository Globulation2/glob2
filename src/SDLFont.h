/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __SDLFONT_H
#define __SDLFONT_H

#include "Header.h"
#include "GraphicContext.h"
#include <stack>

class SDLFont:public Font
{
public:
	virtual ~SDLFont() { }

protected:
	friend class SDLDrawableSurface;

	//! draw a string until we find a \0
	virtual void drawString(SDL_Surface *Surface, int x, int y, int w, const char *text, SDL_Rect *clip=NULL) const=0;
};

class SDLBitmapFont: public SDLFont
{
public:
	SDLBitmapFont();
	SDLBitmapFont(const char *filename);
	virtual ~SDLBitmapFont();
	bool load(const char *filename);
	int getStringWidth(const char *string) const;
	int getStringHeight(const char *string) const;
	bool printable(char c) const;

protected:
	friend class SDLDrawableSurface;

	void drawString(SDL_Surface *Surface, int x, int y, int w, const char *text, SDL_Rect *clip=NULL) const;

	bool load(SDL_Surface *fontSurface);

	void init();
	// Returns the width of "text" in pixels
	int textWidth(const char *text, int min=0, int max=255) const;
	

	enum {
		startChar = 33
	};
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

class SDLTTFont:public SDLFont
{
public:
	SDLTTFont();
	SDLTTFont(const char *filename, unsigned size);
	virtual ~SDLTTFont();
	bool load(const char *filename, unsigned size);
	
	int getStringWidth(const char *string) const;
	int getStringHeight(const char *string) const;
	bool printable(char c) const;
	
	virtual void setColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a = DrawableSurface::ALPHA_OPAQUE);
	virtual void pushColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a = DrawableSurface::ALPHA_OPAQUE);
	virtual void popColor(void);
	virtual void getColor(Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a) const;
	
	virtual void setStyle(unsigned style);
	virtual void pushStyle(unsigned style);
	virtual void popStyle(void);
	virtual unsigned getStyle(void) const;
	
protected:
	friend class SDLDrawableSurface;
	
	void drawString(SDL_Surface *Surface, int x, int y, int w, const char *text, SDL_Rect *clip=NULL) const;
	
protected:
	TTF_Font *font;
	
	std::stack<unsigned> styleStack;
	std::stack<SDL_Color> colorStack;
};

#endif
