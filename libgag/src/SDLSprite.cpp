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
#include <Toolkit.h>
#include <SupportFunctions.h>
#include <assert.h>

extern SDLGraphicContext *screen;

#define STATIC_PALETTE_SIZE 256
#define COLOR_ROTATION_COUNT 32
#define PAL_COLOR_MERGE_THRESHOLD 2

struct TransformedPalEntry
{
	Uint16 r, g, b, pad;
};

struct TransformedPal
{
	// datas
	TransformedPalEntry colors[STATIC_PALETTE_SIZE];
	// lookup infos
	Uint8 rotr, rotg, rotb;
};

struct OriginalPal
{
	// datas
	Uint8 r[STATIC_PALETTE_SIZE];
	Uint8 g[STATIC_PALETTE_SIZE];
	Uint8 b[STATIC_PALETTE_SIZE];
};

//! This class contains all statically allocated color for color shifting
struct StaticPalContainer
{
	//! The original palette, used for new rotation
	OriginalPal originalPal;
	
	//! Teh array of transformed palette, one for each color
	TransformedPal rotatedPal[COLOR_ROTATION_COUNT];
	
	//! The number of color allocated in the palette (0..255)
	unsigned allocatedCount;
	//! the number of palette allocated (0..COLOR_ROTATION_COUNT)
	unsigned rotatedCount;
	//! the position of the next free entry, wrap
	unsigned rotatedNextFree;
	//! The active pallette (0..rotatedCount)
	unsigned activePalette;
	
	
	//! the minimal distance of two colors in the set
	unsigned minDist;
	//! the two index of the color which has the minimal distance
	unsigned minDistIdx1, minDistIdx2;
	
	//! Constructor
	StaticPalContainer();
	
	//! Allocate a color, return the index
	unsigned allocate(Uint8 r, Uint8 g, Uint8 b);
	
	//! Activate a color, if needed, do the rotation
	void setColor(Uint8 r, Uint8 g, Uint8 b);
} palContainer;

StaticPalContainer::StaticPalContainer()
{
	allocatedCount = 0;
	rotatedCount = 0;
	rotatedNextFree = 0;
	activePalette = 0;
	minDist = 
	minDistIdx1 = minDistIdx2 = 0;
}

unsigned StaticPalContainer::allocate(Uint8 r, Uint8 g, Uint8 b)
{
	unsigned i = 0;
	unsigned len = 1000000000;
	unsigned nearestIdx = 0;
	int dr, dg, db;
	unsigned nlen;
	
	while (i<allocatedCount)
	{
		dr = (r-originalPal.r[i]);
		dg = (g-originalPal.g[i]);
		db = (b-originalPal.b[i]);
		
		// NOTE : this metric could be improved
		nlen=dr*dr + dg*dg + db*db;
		
		if (nlen <= PAL_COLOR_MERGE_THRESHOLD)
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
		allocatedCount++;
		return i;
	}
	return nearestIdx;
	
	
	/*
	This algo doesn't work because we can't change index after one image has been loaded
	unsigned i;
	unsigned localMinDist = 1000000000;
	unsigned localMinDistIdx = 0;
	
	for (i=0; i<allocatedCount; i++)
	{
		dr = (r-originalPal.r[i]);
		dg = (g-originalPal.g[i]);
		db = (b-originalPal.b[i]);
		
		// this distance metric could be improved
		nlen=dr*dr + dg*dg + db*db;
		
		if (nlen<localMinDist)
		{
			localMinDistIdx = i;
			localMinDist = nlen;
		}
	}
	// localMinDistIdx has the index of the nearer color
	
	if (allocatedCount<COLOR_ROTATION_COUNT)
	{
		// we have enough room
		originalPal.r[allocatedCount]=r;
		originalPal.g[allocatedCount]=g;
		originalPal.b[allocatedCount]=b;
		allocatedCount++;
	}
	else
	{
		// we do not have enough room
		if (localMinDist < minDist)
		{
			// we have found a color which is nearer than the replacing threshold
			return localMinDistIdx;
		}
		else
		{
			// we will replace another color
			originalPal.r[minDistIdx1
		}
	}
	*/
}

void StaticPalContainer::setColor(Uint8 r, Uint8 g, Uint8 b)
{
	unsigned i;
	for (i=0; i<rotatedCount; i++)
	{
		if ((rotatedPal[i].rotr == r) && (rotatedPal[i].rotg == g) && (rotatedPal[i].rotb == b))
		{
			// we have found a previous one
			activePalette = i;
			return;
		}
	}
	
	// we have not found a previous one, we need to allocate a new one
	if (rotatedCount < COLOR_ROTATION_COUNT)
		rotatedCount++;
	
	// activate the palette
	activePalette = rotatedNextFree;

	// do the transformation
	float hue, lum, sat;
	float baseHue;
	float hueDec;
	float nR, nG, nB;
	GAG::RGBtoHSV(51.0f/255.0f, 255.0f/255.0f, 153.0f/255.0f, &baseHue, &sat, &lum);
	GAG::RGBtoHSV( ((float)r)/255, ((float)g)/255, ((float)b)/255, &hue, &sat, &lum);
	hueDec=hue-baseHue;
	for (i=0; i<256; i++)
	{
		GAG::RGBtoHSV( ((float)originalPal.r[i])/255, ((float)originalPal.g[i])/255, ((float)originalPal.b[i])/255, &hue, &sat, &lum);
		GAG::HSVtoRGB(&nR, &nG, &nB, hue+hueDec, sat, lum);
		rotatedPal[rotatedNextFree].colors[i].r=(Uint32)(255*nR);
		rotatedPal[rotatedNextFree].colors[i].g=(Uint32)(255*nG);
		rotatedPal[rotatedNextFree].colors[i].b=(Uint32)(255*nB);
		rotatedPal[rotatedNextFree].colors[i].pad=0;
	}

	// save the color
	rotatedPal[rotatedNextFree].rotr=r;
	rotatedPal[rotatedNextFree].rotg=g;
	rotatedPal[rotatedNextFree].rotb=b;

	// round robin on next free pointer, wrap
	if (rotatedNextFree != COLOR_ROTATION_COUNT-1)
	{
		rotatedNextFree++;
	}
	else
	{
		rotatedNextFree=0;
	}
}


SDLSprite::Palette::Palette()
{
	FILE *palFP=Toolkit::getFileManager()->openFP("data/pal.txt", "rb");
	assert (palFP);
	if (palFP)
	{
		char temp[256];
		int i;
		int r, g, b;
		fgets(temp, 256, palFP);
		// angel > Y a surement une meilleur methode mais je la connais pas...
		// Minas > C'est du binaire... Donc y'a pas de cas particulier ici!
		assert(strcmp(temp, "GIMP Palette\n")==0);
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

// Paletized image, used for rotation
SDLSprite::PalImage::PalImage(int w, int h)
{
	this->w=w;
	this->h=h;
	data=new PalImageEntry[w*h];
}

SDLSprite::PalImage::~PalImage()
{
	if (data)
		delete[] data;
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
	if ((index<0) || (index>=(int)images.size()))
		fprintf(stderr, "GAG : Can load index %d of %u\n", index, images.size());
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

					dVal=(dVal&0xFF000000)|((dR<<Rshift)|(dG<<Gshift)|(dB<<Bshift));
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
	
	if (rotated[index])
	{
		int dx, dy;
		int sy;
		Uint32 *dPtr;
		
		TransformedPalEntry color;
		TransformedPalEntry *colors=palContainer.rotatedPal[palContainer.activePalette].colors;
		PalImageEntry *sPtr;
		PalImageEntry entry;
		
		Uint32 a, na;
		Uint32 dR, dG, dB;
		Uint32 Rshift, Gshift, Bshift;
		Uint32 dVal;

		Rshift=dest->format->Rshift;
		Gshift=dest->format->Gshift;
		Bshift=dest->format->Bshift;
		
		sy=src.y;
		for (dy=r.y; dy<r.y+r.h; dy++)
		{
			sPtr=rotated[index]->data+sy*rotated[index]->w+src.x;
			dPtr=((Uint32 *)(dest->pixels))+dy*(dest->pitch>>2)+r.x;
			dx=w;
			do
			{
				// get the values
				entry=*sPtr;
				dVal=*dPtr;
				
				// get the color
				color=colors[entry.index];
				
				// get the alpha
				a=entry.alpha;
				na=255-a;
				
				dR=(dVal>>Rshift)&0xFF;
				dG=(dVal>>Gshift)&0xFF;
				dB=(dVal>>Bshift)&0xFF;
				
				dR=((a*color.r)+(na*dR))>>8;
				dG=((a*color.g)+(na*dG))>>8;
				dB=((a*color.b)+(na*dB))>>8;
				
				dVal=(dVal&0xFF000000)|((dR<<Rshift)|(dG<<Gshift)|(dB<<Bshift));
				//dVal=entry.index*a;
				*dPtr=dVal;
				
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

void SDLSprite::setBaseColor(Uint8 r, Uint8 g, Uint8 b)
{
	bcR=r;
	bcG=g;
	bcB=b;
	pal.setColor(r, g, b);
	palContainer.setColor(r, g, b);
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
	for (std::vector <PalImage *>::iterator rotatedIt=rotated.begin(); rotatedIt!=rotated.end(); ++rotatedIt)
	{
		if (*rotatedIt)
			delete (*rotatedIt);
	}
}

void SDLSprite::loadFrame(SDL_RWops *frameStream, SDL_RWops *overlayStream, SDL_RWops *paletizedStream, SDL_RWops *rotatedStream)
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
		
	if (rotatedStream)
	{
		SDL_Surface *sprite;
		sprite=IMG_Load_RW(rotatedStream, 0);
		if (sprite->format->BitsPerPixel==32)
		{
			PalImage *image=new PalImage(sprite->w, sprite->h);
			
			Uint32 *ptr=(Uint32 *)(sprite->pixels);
			for (int i=0; i<sprite->w*sprite->h; i++)
			{
				Uint8 r, g, b, a;
				SDL_GetRGBA(*ptr, sprite->format, &r, &g, &b, &a);
				image->data[i].index=palContainer.allocate(r, g, b);
				image->data[i].alpha=a;
				ptr++;
			}
			rotated.push_back(image);
		}
		else
		{
			fprintf(stderr, "GAG : Warning, rotated image is in wrong for (%d) bpp istead of 32\n", sprite->format->BitsPerPixel);
			rotated.push_back(NULL);
		}
	}
	else
		rotated.push_back(NULL);
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
	else if (rotated[index])
		return rotated[index]->w;
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
	else if (rotated[index])
		return rotated[index]->h;
	else
		return 0;
}

