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

#include "SDLSprite.h"
#include <math.h>
#include "GlobalContainer.h"


void SDLSprite::draw(SDL_Surface *dest, const SDL_Rect *clip, int x, int y, int index)
{
	assert(index>=0);
	assert(index<(int)images.size());

	SDL_Rect oldr, r;
	SDL_Rect newr=*clip;
	SDL_Rect src;
	int w, h;
	int diff;

	SDL_GetClipRect(dest, &oldr);
	SDL_SetClipRect(dest, &newr);

	w=getW(index);
	h=getH(index);

	src.x=0;
	src.y=0;
	if (x<newr.x)
	{
		diff=newr.x-x;
		w-=diff;
		src.x+=diff;
		x=newr.x;
	}
	if (y<newr.y)
	{
		diff=newr.y-y;
		h-=diff;
		src.y+=diff;
		y=newr.y;
	}

	if (x+w>newr.x+newr.w)
	{
		diff=(x+w)-(newr.x+newr.w);
		w-=diff;
	}
	if (y+h>newr.y+newr.h)
	{
		diff=(y+h)-(newr.y+newr.h);
		h-=diff;
	}

	if ((w<=0) || (h<=0))
		return;

	src.w=w;
	src.h=h;
	r.x=x;
	r.y=y;
	r.w=w;
	r.h=h;

	SDL_BlitSurface(images[index], &src, dest, &r);
	SDL_LockSurface(dest);
	if ((masks[index]) && (masks[index]->format->BitsPerPixel==8) && (dest->format->BitsPerPixel==32))
	{
		int dx, dy;
		int sy;
		Uint8 *sPtr;
		Uint32 *dPtr;
		Uint32 dVal;
		Uint32 sVal;
		Uint32 sValM;
		Uint32 dR, dG, dB;
		Uint32 Rshift, Gshift, Bshift;

		// TODO : use more infos from destination and implement 16 bpp version
		Rshift=dest->format->Rshift;
		Gshift=dest->format->Gshift;
		Bshift=dest->format->Bshift;
		sy=src.y;
		for (dy=r.y; dy<r.y+r.h; dy++)
		{
			sPtr=((Uint8 *)masks[index]->pixels)+sy*masks[index]->pitch+src.x;
			dPtr=((Uint32 *)(dest->pixels))+dy*(dest->pitch>>2)+r.x;
			dx=w;
			do
			{
				sVal=(Uint32)*sPtr;
				if (sVal!=0)
				{
					sValM=255-sVal;
					dVal=*dPtr;

					dR=(dVal>>Rshift)&0xFF;
					dG=(dVal>>Gshift)&0xFF;
					dB=(dVal>>Bshift)&0xFF;
					dR=((sVal*bcR)+(sValM*dR))>>8;
					dG=((sVal*bcG)+(sValM*dG))>>8;
					dB=((sVal*bcB)+(sValM*dB))>>8;

					dVal=(dVal&0xFF000000)|((dR<<Rshift)+(dG<<Gshift)+(dB<<Bshift));
					*dPtr=dVal;
				}
				dPtr++;
				sPtr++;
			}
			while (--dx);
			sy++;
		}
	}
	SDL_UnlockSurface(dest);
	SDL_SetClipRect(dest, &oldr);
}


SDLSprite::~SDLSprite()
{
	for (std::vector <SDL_Surface *>::iterator imagesIt=images.begin(); imagesIt!=images.end(); ++imagesIt)
	{
		if (*imagesIt)
			SDL_FreeSurface((*imagesIt));
	}
	for (std::vector <SDL_Surface *>::iterator masksIt=masks.begin(); masksIt!=masks.end(); ++masksIt)
	{
		if (*masksIt)
			SDL_FreeSurface((*masksIt));
	}
}

void SDLSprite::loadFrame(SDL_RWops *frameStream, SDL_RWops *overlayStream)
{
	if (frameStream)
	{
		SDL_Surface *temp, *sprite;
		temp=IMG_Load_RW(frameStream, 0);
		sprite=SDL_DisplayFormatAlpha(temp);
		SDL_FreeSurface(temp);
		images.push_back(sprite);
	}
	else
		images.push_back(NULL);


	if (overlayStream)
	{
		SDL_Surface *sprite;
		sprite=IMG_Load_RW(overlayStream, 0);
		masks.push_back(sprite);
	}
	else
		masks.push_back(NULL);
}

int SDLSprite::getW(int index)
{
	assert(index>=0);
	assert(index<(int)images.size());
	if (images[index])
		return images[index]->w;
	else
		return 0;
}

int SDLSprite::getH(int index)
{
	assert(index>=0);
	assert(index<(int)images.size());
	if (images[index])
		return images[index]->h;
	else
		return 0;
}

