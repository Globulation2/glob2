/*
 * GAG SDL font file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __SDLFONT_H
#define __SDLFONT_H

#include "Header.h"
#include "GraphicContext.h"

class SDLFont:public Font
{
public:
	virtual ~SDLFont() { }

protected:
	friend class SDLDrawableSurface;

	//! draw a string until we fint a \0, \r or \n in text
	virtual void drawString(SDL_Surface *Surface, int x, int y, const char *text, SDL_Rect *clip=NULL) const=0;
	virtual bool load(const char *filename)=0;	
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
	bool printable(char c);

protected:
	friend class SDLDrawableSurface;

	void drawString(SDL_Surface *Surface, int x, int y, const char *text, SDL_Rect *clip=NULL) const;

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

#endif
