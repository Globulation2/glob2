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

SDLSprite::Palette::Palette()
{
	FILE *palFP=globalContainer->fileManager.openFP("data/pal.txt", "rb");
	assert (palFP);
	if (palFP)
	{
		char temp[256];
		int i;
		int r, g, b;
		fgets(temp, 256, palFP);
		assert(strcmp(temp, "GIMP Palette\n")==0);
		for (i=0; i<256; i++)
		{
			fgets(temp, 256, palFP);
			sscanf(temp, "%d %d %d", &r, &g, &b);
			origR[i]=r;
			origG[i]=g;
			origB[i]=b;
		}
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
		RGBtoHSV(51.0f/255.0f, 255.0f/255.0f, 153.0f/255.0f, &baseHue, &sat, &lum);
		RGBtoHSV( ((float)r)/255, ((float)g)/255, ((float)b)/255, &hue, &sat, &lum);
		hueDec=hue-baseHue;
		for (i=0; i<256; i++)
		{
			RGBtoHSV( ((float)origR[i])/255, ((float)origG[i])/255, ((float)origB[i])/255, &hue, &sat, &lum);
			HSVtoRGB(&nR, &nG, &nB, hue+hueDec, sat, lum);
			colors[i]=((Uint32)(255*nR)<<16)+((Uint32)(255*nG)<<8)+(Uint32)(255*nB);
		}
		rTransformed=r;
		gTransformed=g;
		bTransformed=b;
	}
}

void SDLSprite::Palette::RGBtoHSV( float r, float g, float b, float *h, float *s, float *v )
{
	float min, max, delta;
	min = fmin( r, g, b );
	max = fmax( r, g, b );
	*v = max;				// v
	delta = max - min;
	if( max != 0 )
		*s = delta / max;		// s
	else {
		// r = g = b = 0		// s = 0, v is undefined
		*s = 0;
		*h = -1;
		return;
	}
	if( r == max )
		*h = ( g - b ) / delta;		// between yellow & magenta
	else if( g == max )
		*h = 2 + ( b - r ) / delta;	// between cyan & yellow
	else
		*h = 4 + ( r - g ) / delta;	// between magenta & cyan
	*h *= 60;				// degrees
	if( *h < 0 )
		*h += 360;
}

void SDLSprite::Palette::HSVtoRGB( float *r, float *g, float *b, float h, float s, float v )
{
	int i;
	float f, p, q, t;
	if( s == 0 ) {
		// achromatic (grey)
		*r = *g = *b = v;
		return;
	}
	h /= 60;			// sector 0 to 5
	i = (int)floor( h );
	f = h - i;			// factorial part of h
	p = v * ( 1 - s );
	q = v * ( 1 - s * f );
	t = v * ( 1 - s * ( 1 - f ) );
	switch( i ) {
		case 0:
			*r = v;
			*g = t;
			*b = p;
			break;
		case 1:
			*r = q;
			*g = v;
			*b = p;
			break;
		case 2:
			*r = p;
			*g = v;
			*b = t;
			break;
		case 3:
			*r = p;
			*g = q;
			*b = v;
			break;
		case 4:
			*r = t;
			*g = p;
			*b = v;
			break;
		default:		// case 5:
			*r = v;
			*g = p;
			*b = q;
			break;
	}
}

float SDLSprite::Palette::fmin(float f1, float f2, float f3)
{
	if ((f1<=f2) && (f1<=f3))
		return f1;
	else if (f2<=f3)
		return f2;
	else
		return f3;
}

float SDLSprite::Palette::fmax(float f1, float f2, float f3)
{
	if ((f1>=f2) && (f1>=f3))
		return f1;
	else if (f2>=f3)
		return f2;
	else
		return f3;
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

