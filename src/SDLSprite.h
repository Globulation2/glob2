/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

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

#ifndef __SDLSPRITE_H
#define __SDLSPRITE_H

#include <vector>
#include "GraphicContext.h"

class SDLGraphicContext;

// virtual class for handling Sprites
class SDLSprite:public Sprite
{
protected:
	// palette, inner class for legacy graphic
	// class for handling color lookup
	class Palette
	{
	public:
		Palette();
		void setColor(Uint8 r, Uint8 g, Uint8 b);
		Uint8 origR[256];
		Uint8 origG[256];
		Uint8 origB[256];
		Uint32 colors[256];
		Uint8 rTransformed, gTransformed, bTransformed;
	public:
		static void RGBtoHSV( float r, float g, float b, float *h, float *s, float *v );
		static void HSVtoRGB( float *r, float *g, float *b, float h, float s, float v );
	private:
		static float fmin(float f1, float f2, float f3);
		static float fmax(float f1, float f2, float f3);
	};

	Palette pal;

	friend class Palette;
	static SDL_Surface *getGlobalContainerGfxSurface(void);

protected:
	std::vector <SDL_Surface *> images;
	std::vector <SDL_Surface *> masks;
	std::vector <SDL_Surface *> paletizeds;
	Uint8 bcR, bcG, bcB;
	bool usebaseColor;

protected:
	friend class SDLGraphicContext;
	void loadFrame(SDL_RWops *frameStream, SDL_RWops *overlayStream=NULL, SDL_RWops *paletizedStream=NULL);

public:
	SDLSprite() { usebaseColor=false; }
	virtual ~SDLSprite();
	virtual void draw(SDL_Surface *dest, const SDL_Rect *clip, int x, int y, int index);
	virtual void enableBaseColor(Uint8 r, Uint8 g, Uint8 b) { bcR=r; bcG=g; bcB=b; usebaseColor=true; pal.setColor(r, g, b); }
	virtual void disableBaseColor(void) { usebaseColor=false; }
	virtual int getW(int index);
	virtual int getH(int index);
};

#endif

