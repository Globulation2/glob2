/*
 * GAG sprite file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
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
	std::vector <SDL_Surface *> images;
	std::vector <SDL_Surface *> masks;
	Uint8 bcR, bcG, bcB;
	bool usebaseColor;

protected:
	friend class SDLGraphicContext;
	void loadFrame(SDL_RWops *frameStream, SDL_RWops *overlayStream);

public:
	SDLSprite() { usebaseColor=false; }
	virtual ~SDLSprite();
	virtual void draw(SDL_Surface *dest, const SDL_Rect *clip, int x, int y, int index);
	virtual void enableBaseColor(Uint8 r, Uint8 g, Uint8 b) { bcR=r; bcG=g; bcB=b; usebaseColor=true; }
	virtual void disableBaseColor(void) { usebaseColor=false; }
	virtual int getW(int index);
	virtual int getH(int index);
};

#endif
 
