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

#include "SDLSprite.h"
#include "SDLGraphicContext.h"
#include <math.h>
#include <Environment.h>
#include <SupportFunctions.h>

extern SDLGraphicContext *screen;

#define STATIC_PALETTE_SIZE 256
#define COLOR_ROTATION_COUNT 32
#define PAL_COLOR_MERGE_THRESHOLD 4

struct TransformedPalEntry
{
	Uint16 r, g, b, na;
};

struct TransformedPal
{
	// datas
	TransformedPalEntry colors[STATIC_PALETTE_SIZE];
	// lookup infos
	Uint8 rotr, rotg, rotb, rota;
};

struct OriginalPal
{
	// datas
	Uint8 r[STATIC_PALETTE_SIZE];
	Uint8 g[STATIC_PALETTE_SIZE];
	Uint8 b[STATIC_PALETTE_SIZE];
	Uint8 a[STATIC_PALETTE_SIZE];
};

//! This class contains all statically allocated color for color shifting
struct StaticPalContainer
{
	OriginalPal originalPal;
	TransformedPal rotatedPal[COLOR_ROTATION_COUNT];
	
	unsigned allocatedCount;
	unsigned rotatedCount;
	
	//! Constructor
	StaticPalContainer();
	
	//! Allocate a color, return the index
	unsigned allocate(Uint8 r, Uint8 g, Uint8 b, Uint8 a);
};

StaticPalContainer::StaticPalContainer()
{
	allocatedCount = 0;
	rotatedCount = 0;
}

unsigned StaticPalContainer::allocate(Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	unsigned i = 0;
	unsigned len = 1000000000;
	unsigned nearestIdx = 0;
	unsigned nlen;
	int dr, dg, db, da;
	
	while (i<allocatedCount)
	{
		dr = (r-originalPal.r[i]);
		dg = (g-originalPal.g[i]);
		db = (b-originalPal.b[i]);
		da = (a-originalPal.a[i]);
		
		nlen=dr*dr + dg*dg + db*db + da*da;
		if (nlen < PAL_COLOR_MERGE_THRESHOLD)
		{
			return i;
		}
		else if (nlen < len)
		{
			nearestIdx = i;
			len = nlen;
		}
		i++;
	}
	if (i<STATIC_PALETTE_SIZE)
	{
		originalPal.r[i]=r;
		originalPal.g[i]=g;
		originalPal.b[i]=b;
		originalPal.a[i]=a;
		allocatedCount++;
		return i;
	}
	return nearestIdx;
}

// TODO : the rotation part and the render part

SDLSprite::Palette::Palette()
{
	FILE *palFP=GAG::fileManager->openFP("data/pal.txt", "rb");
	assert (palFP);
	if (palFP)
	{
		char temp[256];
		int i;
		int r, g, b;
		fgets(temp, 256, palFP);
		// angel > Y a surement une meilleur methode mais je la connais pas...
#		ifdef WIN32
			assert(strcmp(temp, "GIMP Palette\r\n")==0);
#		else
			assert(strcmp(temp, "GIMP Palette\n")==0);
#		endif
		for (i=0; i<256; i++)
		{
			fgets(temp, 256, palFP);
			sscanf(temp, "%d %d %d", &r, &g, &b);
			origR[i]=r;
			origG[i]=g;
			origB[i]=b;
		}
		fclose(palFP);
	}
	rTransformed=0;
	gTransformed=255;
	bTransformed=0;
}

void SDLSprite::Palette::setColor(Uint8 r, Uint8 g, Uint8 b)
{
	if ((r!=rTransformed) || (g!=gTransformed) || (b!=bTransformed))
	{
		float hue, lum, sat;
		float baseHue;
		float hueDec;
		float nR, nG, nB;
		int i;
		GAG::RGBtoHSV(51.0f/255.0f, 255.0f/255.0f, 153.0f/255.0f, &baseHue, &sat, &lum);
		GAG::RGBtoHSV( ((float)r)/255, ((float)g)/255, ((float)b)/255, &hue, &sat, &lum);
		hueDec=hue-baseHue;
		for (i=0; i<256; i++)
		{
			GAG::RGBtoHSV( ((float)origR[i])/255, ((float)origG[i])/255, ((float)origB[i])/255, &hue, &sat, &lum);
			GAG::HSVtoRGB(&nR, &nG, &nB, hue+hueDec, sat, lum);
			colors[i]=SDL_MapRGB(SDLSprite::getGlobalContainerGfxSurface()->format, (Uint32)(255*nR), (Uint32)(255*nG), (Uint32)(255*nB));
		}
		rTransformed=r;
		gTransformed=g;
		bTransformed=b;
	}
}

SDL_Surface *SDLSprite::getGlobalContainerGfxSurface(void)
{
	assert(screen);
/*	SDLGraphicContext *SDLgc=dynamic_cast<SDLGraphicContext *>(screen);
	return SDLgc->surface;*/
	return screen->surface;
}

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

	if (images[index])
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

	if ((paletizeds[index]) && (paletizeds[index]->format->BitsPerPixel==8) && (dest->format->BitsPerPixel==32))
	{
		int dx, dy;
		int sy;
		Uint8 color;
		// as all this is a hack to support legacy gfx, 0 is the colorkey
		Uint8 key=0;
		Uint8 *sPtr;
		Uint32 *dPtr;

		sy=src.y;
		for (dy=r.y; dy<r.y+r.h; dy++)
		{
			sPtr=((Uint8 *)paletizeds[index]->pixels)+sy*paletizeds[index]->pitch+src.x;
			dPtr=((Uint32 *)(dest->pixels))+dy*(dest->pitch>>2)+r.x;
			dx=w;
			do
			{
				color=*sPtr;
				if (color!=key)
					*dPtr=pal.colors[color];
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
	for (std::vector <SDL_Surface *>::iterator paletizedsIt=paletizeds.begin(); paletizedsIt!=paletizeds.end(); ++paletizedsIt)
	{
		if (*paletizedsIt)
			SDL_FreeSurface((*paletizedsIt));
	}
}

void SDLSprite::loadFrame(SDL_RWops *frameStream, SDL_RWops *overlayStream, SDL_RWops *paletizedStream)
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

	if (paletizedStream)
	{
		SDL_Surface *sprite;
		sprite=IMG_Load_RW(paletizedStream, 0);
		paletizeds.push_back(sprite);
	}
	else
		paletizeds.push_back(NULL);
}

int SDLSprite::getW(int index)
{
	assert(index>=0);
	assert(index<(int)images.size());
	if (images[index])
		return images[index]->w;
	else if (masks[index])
		return masks[index]->w;
	else if (paletizeds[index])
		return paletizeds[index]->w;
	else
		return 0;
}

int SDLSprite::getH(int index)
{
	assert(index>=0);
	assert(index<(int)images.size());
	if (images[index])
		return images[index]->h;
	else if (masks[index])
		return masks[index]->h;
	else if (paletizeds[index])
		return paletizeds[index]->h;
	else
		return 0;
}

