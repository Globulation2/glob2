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

#ifndef __SDLSPRITE_H
#define __SDLSPRITE_H

#include <vector>
#include "GraphicContext.h"

class SDLGraphicContext;

//! class for handling Sprites with SDL
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
	};
	
	//! This represents one pixel of a palettized image
	struct PalImageEntry
	{
		//! The index in the palette
		Uint8 index;
		//! The alpha component
		Uint8 alpha;
	};
	
	//! This represents an image paletised with exteranl global palette and alpha support
	struct PalImage
	{
		//! The dimension of the image
		int w, h;
		//! The pixel array
		PalImageEntry *data;
		
		PalImage(int w, int h);
		~PalImage();
	};

	Palette pal;

	friend class Palette;
	static SDL_Surface *getGlobalContainerGfxSurface(void);

protected:
	std::vector <SDL_Surface *> images;
	std::vector <SDL_Surface *> masks;
	std::vector <SDL_Surface *> paletizeds;
	std::vector <PalImage *> rotated;
	Uint8 bcR, bcG, bcB;

protected:
	friend class SDLGraphicContext;
	void loadFrame(SDL_RWops *frameStream, SDL_RWops *overlayStream=NULL, SDL_RWops *paletizedStream=NULL, SDL_RWops *rotatedStream=NULL);

public:
	SDLSprite() { }
	virtual ~SDLSprite();
	
	//! Draw the sprite frame index at pos (x,y) on an SDL Surface with the clipping rect clip
	virtual void draw(SDL_Surface *dest, const SDL_Rect *clip, int x, int y, int index);
	
	//! Set the (r,g,b) color to a sprite's base color. All loadFrame must have been done at this point
	virtual void setBaseColor(Uint8 r, Uint8 g, Uint8 b);
	
	//! Return the width of index frame of the sprite
	virtual int getW(int index);
	//! //! Return the height of index frame of the sprite
	virtual int getH(int index);
};

#endif

