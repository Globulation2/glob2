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

#include "SDLGraphicContext.h"
#include "SDLSprite.h"
#include "SDLFont.h"
#include <SupportFunctions.h>
#include <Toolkit.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <sstream>

// here begin the SDL Drawable Surface part
SDLGraphicContext *screen=NULL;

SDLDrawableSurface::SDLDrawableSurface()
{
	surface=NULL;
	clipRect.x=0;
	clipRect.y=0;
	clipRect.w=0;
	clipRect.h=0;
	flags=0;
}

void SDLDrawableSurface::loadImage(const char *name)
{
	if (name)
	{
		SDL_RWops *imageStream;
		if ((imageStream=Toolkit::getFileManager()->open(name, "rb", false))!=NULL)
		{
			SDL_Surface *temp;
			temp=IMG_Load_RW(imageStream, 0);
			if (temp)
			{
				if (surface)
					SDL_FreeSurface(surface);
				surface=SDL_DisplayFormatAlpha(temp);
				SDL_FreeSurface(temp);
			}
			SDL_RWclose(imageStream);
		}
	}
}

bool SDLDrawableSurface::setRes(int w, int h, int depth, Uint32 flags)
{
	if (surface)
		SDL_FreeSurface(surface);

	this->flags=flags;
	Uint32 sdlFlags=0;
	if (flags&HWACCELERATED)
		sdlFlags|=SDL_HWSURFACE;
	else
		sdlFlags|=SDL_SWSURFACE;

	SDL_Surface *tempScreen=SDL_CreateRGBSurface(sdlFlags, w, h, depth, 0xFF, 0xFF00, 0xFF0000, 0xFF000000);

	surface=SDL_DisplayFormat(tempScreen);
	SDL_FreeSurface(tempScreen);
	setClipRect();
	return true;
}

void SDLDrawableSurface::setAlpha(bool usePerPixelAlpha, Uint8 alphaValue)
{
	if (!surface)
		return;

	SDL_Surface *tempScreen;
	SDL_SetAlpha(surface, SDL_SRCALPHA, alphaValue);

	if (usePerPixelAlpha)
		tempScreen=SDL_DisplayFormatAlpha(surface);
	else
		tempScreen=SDL_DisplayFormat(surface);

	SDL_FreeSurface(surface);

	surface=tempScreen;
}

void SDLDrawableSurface::setClipRect(int x, int y, int w, int h)
{
	clipRect.x=x;
	clipRect.y=y;
	clipRect.w=w;
	clipRect.h=h;

	if (!surface)
		return;

	SDL_SetClipRect(surface, &clipRect);
}

void SDLDrawableSurface::setClipRect(void)
{
	clipRect.x=0;
	clipRect.y=0;
	clipRect.w=surface->w;
	clipRect.h=surface->h;

	if (!surface)
		return;

	SDL_SetClipRect(surface, &clipRect);
}

void SDLDrawableSurface::drawSprite(int x, int y, Sprite *sprite, int index)
{
	if (!surface)
		return;
	((SDLSprite *)sprite)->draw(surface, &clipRect, x, y, index);
}

void SDLDrawableSurface::drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	if (!surface)
		return;

	if ((x<clipRect.x) || (x>=clipRect.x+clipRect.w) || (y<clipRect.y) || (y>=clipRect.y+clipRect.h))
		return;

 	/* Set the pixel */
	assert(surface);
	SDL_LockSurface(surface);
    switch(surface->format->BitsPerPixel)
	{
        case 8:
		{
            *(((Uint8 *)surface->pixels)+y*surface->pitch+x) = (Uint8)SDL_MapRGB(surface->format, r, g, b);
		}
		break;
        case 16:
		{
            Uint16 *mem=((Uint16 *)surface->pixels)+y*(surface->pitch>>1)+x;
			Uint8 dr, dg, db;
			Uint8 na=255-a;
			SDL_GetRGB(*mem, surface->format, &dr, &dg, &db);
			r=(a*r+na*dr)>>8;
			g=(a*g+na*dg)>>8;
			b=(a*b+na*db)>>8;
            *mem = (Uint16)SDL_MapRGB(surface->format, r, g, b);
		}
		break;
        case 24:
		{
			Uint8 *bits=((Uint8 *)surface->pixels+y*surface->pitch+x);
			Uint32 pixel=SDL_MapRGB(surface->format, r, g, b);
			{ /* Format/endian independent */
                Uint8 nr, ng, nb;

                nr = (pixel>>surface->format->Rshift)&0xFF;
                ng = (pixel>>surface->format->Gshift)&0xFF;
                nb = (pixel>>surface->format->Bshift)&0xFF;
                *((bits)+surface->format->Rshift/8) = nr;
                *((bits)+surface->format->Gshift/8) = ng;
                *((bits)+surface->format->Bshift/8) = nb;
            }
		}
		break;
        case 32:
		{
			Uint32 *mem=((Uint32 *)surface->pixels)+y*(surface->pitch>>2)+x;
			Uint8 dr, dg, db;
			Uint8 na=255-a;
			SDL_GetRGB(*mem, surface->format, &dr, &dg, &db);
			r=(a*r+na*dr)>>8;
			g=(a*g+na*dg)>>8;
			b=(a*b+na*db)>>8;
            *mem = (Uint32)SDL_MapRGB(surface->format, r, g, b);
		}
        break;
    }
	SDL_UnlockSurface(surface);
}

void SDLDrawableSurface::drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	if (!surface)
		return;

	drawHorzLine(x, y, w, r, g, b, a);
	drawHorzLine(x, y+h-1, w, r, g, b, a);
	drawVertLine(x, y, h, r, g, b, a);
	drawVertLine(x+w-1, y, h, r, g, b, a);
}

void SDLDrawableSurface::drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	if (!surface)
		return;

	if ((a==ALPHA_OPAQUE) || (surface->format->BitsPerPixel==8))
	{
		SDL_Rect rect;
		rect.x=x;
		rect.y=y;
		rect.w=w;
		rect.h=h;
		SDL_FillRect(surface, &rect, SDL_MapRGB(surface->format, r, g, b));
	}
	else
	{
		// do clipping
		if (x<0)
		{
			w+=x;
			x=0;
		}
		if (y<0)
		{
			h+=y;
			y=0;
		}
		if (x+w>=surface->w)
		{
			w=surface->w-x;
		}
		if (y+h>=surface->h)
		{
			h=surface->h-y;
		}
		if ((w<=0) || (h<=0))
			return;

		int dw, dy;

		// pre-multiply
		unsigned pr, pg, pb;
		pr=r*a;
		pg=r*g;
		pb=r*b;
		Uint8 dr, dg, db;

		// draw
		SDL_LockSurface(surface);
		switch(surface->format->BitsPerPixel)
		{
			case 16:
			{
				Uint8 na=255-a;
				dy = y;
				while (dy<y+h)
				{
					Uint16 *mem=((Uint16 *)surface->pixels)+dy*(surface->pitch>>1)+x;

					dw=w;
					while (dw)
					{
						SDL_GetRGB(*mem, surface->format, &dr, &dg, &db);
						r=(pr+na*dr)>>8;
						g=(pg+na*dg)>>8;
						b=(pb+na*db)>>8;
						*mem = (Uint16)SDL_MapRGB(surface->format, r, g, b);
						mem++;
						--dw;
					}
					dy++;
				}
			}
			break;
			case 24:
			{
				Uint8 na=255-a;
				dy = y;
				while (dy<y+h)
				{
					Uint8 *bits=((Uint8 *)surface->pixels+dy*surface->pitch+x);

					dw=w;
					while (dw)
					{
						dr = *((bits)+surface->format->Rshift/8);
						dg = *((bits)+surface->format->Gshift/8);
						db = *((bits)+surface->format->Bshift/8);

						dr=(pr+na*dr)>>8;
						dg=(pg+na*dg)>>8;
						db=(pb+na*db)>>8;

						*((bits)+surface->format->Rshift/8) = dr;
						*((bits)+surface->format->Gshift/8) = dg;
						*((bits)+surface->format->Bshift/8) = db;

						bits+=3;
						--dw;
					}
					dy++;
				}
			}
			break;
			case 32:
			{
				dy = y;
				Uint32 na=255-a;

				Uint32 val[3];
				val[0]=r;
				val[1]=g;
				val[2]=b;

				Uint32 idx[3];
				idx[surface->format->Rshift/8]=0;
				idx[surface->format->Gshift/8]=1;
				idx[surface->format->Bshift/8]=2;

				Uint32 prb=(val[idx[0]]|(val[idx[2]]<<16))*a;
				Uint32 pga=(val[idx[1]]|0x00FF0000)*a;
				while (dy<y+h)
				{
					Uint32 *mem=((Uint32 *)surface->pixels)+dy*(surface->pitch>>2)+x;

					dw=w;
					while (dw)
					{
						Uint32 val=*mem;
						Uint32 nrb=(val&0x00FF00FF)*na;
						Uint32 nga=((val>>8)&0x00FF00FF)*na;
						nrb+=prb;
						nga+=pga;
						*mem=((nrb>>8)&0x00FF00FF)|(nga&0xFF00FF00);
						mem++;
						--dw;
					}
					dy++;
				}
			}
			break;
		}
		SDL_UnlockSurface(surface);
	}
}

void SDLDrawableSurface::drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	if (!surface)
		return;

	Uint32 pixel;

	// clip on x
	if ((x<clipRect.x) || (x >= (clipRect.x+clipRect.w)))
		return;

	// set l positiv
	if (l<0)
	{
		y+=l;
		l=-l;
	}

	// clip on y at top
	if (y<clipRect.y)
	{
		l-=(clipRect.y-y);
		y=clipRect.y;
	}

	// clip on y at bottom
	if ((y+l) >= (clipRect.y+clipRect.h))
	{
		l=clipRect.y+clipRect.h-y;
	}

	// ignore wrong case
	if (l<=0)
		return;

	pixel = SDL_MapRGB(surface->format, r, g, b);

	SDL_LockSurface(surface);
	/* Set the pixels */
    switch(surface->format->BitsPerPixel)
	{
        case 8:
		{
			Uint8 *bits = ((Uint8 *)surface->pixels)+y*surface->pitch+x;
			for (int n=l-1; n>=0; --n)
			{
				*bits = (Uint8)pixel;
				bits+=surface->pitch;
			}
		}
		break;
        case 16:
		{
			int increment=(surface->pitch>>1);
			Uint16 *bits = ((Uint16 *)surface->pixels)+y*increment+x;
			for (int n=l-1; n>=0; --n)
			{
				*bits = (Uint16)pixel;
				bits+=increment;
			}
		}
		break;
        case 24:
		{
			 /* Format/endian independent */
			Uint8 *bits = ((Uint8 *)surface->pixels)+y*surface->pitch+x*3;
			Uint8 nr, ng, nb;
			for (int n=l-1; n>=0; --n)
			{
				nr = (pixel>>surface->format->Rshift)&0xFF;
				ng = (pixel>>surface->format->Gshift)&0xFF;
				nb = (pixel>>surface->format->Bshift)&0xFF;
				*((bits)+surface->format->Rshift/8) = nr;
				*((bits)+surface->format->Gshift/8) = ng;
				*((bits)+surface->format->Bshift/8) = nb;
				bits+=surface->pitch;
            }
		}
		break;
        case 32:
		{
			int increment=(surface->pitch>>2);
			Uint32 *bits = ((Uint32 *)surface->pixels)+y*increment+x;
			for (int n=l-1; n>=0; --n)
			{
                *((Uint32 *)(bits)) = (Uint32)pixel;
				bits+=increment;
			}
		}
		break;
		default:
			break;
    }
	SDL_UnlockSurface(surface);
}

void SDLDrawableSurface::drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	if (!surface)
		return;

	Uint32 pixel;

	// clip on y
	if ((y<clipRect.y) || (y >= (clipRect.y+clipRect.h)))
		return;

	// set l positiv
	if (l<0)
	{
		x+=l;
		l=-l;
	}

	// clip on x at left
	if (x<clipRect.x)
	{
		l-=(clipRect.x-x);
		x=clipRect.x;
	}

	// clip on x at right
	if ((x+l) >= (clipRect.x+clipRect.w))
	{
		l=clipRect.x+clipRect.w-x;
	}

	// ignore wrong case
	if (l<=0)
		return;

	pixel = SDL_MapRGB(surface->format, r, g, b);

	SDL_LockSurface(surface);
    /* Set the pixels */
    switch(surface->format->BitsPerPixel)
	{
        case 8:
		{
			Uint8 *bits= ((Uint8 *)surface->pixels)+y*surface->pitch+x;
			for (int n=l-1; n>=0; --n)
			{
                *bits++ = (Uint8)pixel;
			}
		}
		break;
        case 16:
		{
			Uint16 *bits= ((Uint16 *)surface->pixels)+y*(surface->pitch>>1)+x;
			for (int n=l-1; n>=0; --n)
			{
                *bits++ = (Uint16)pixel;
			}
		}
		break;
        case 24:
		{
			 /* Format/endian independent */
            Uint8 nr, ng, nb;
			Uint8 *bits= ((Uint8 *)surface->pixels)+y*surface->pitch+x*3;
			for (int n=l-1; n>=0; --n)
			{
                nr = (pixel>>surface->format->Rshift)&0xFF;
                ng = (pixel>>surface->format->Gshift)&0xFF;
                nb = (pixel>>surface->format->Bshift)&0xFF;
                *((bits)+surface->format->Rshift/8) = nr;
                *((bits)+surface->format->Gshift/8) = ng;
                *((bits)+surface->format->Bshift/8) = nb;
				bits+=3;
            }
		}
		break;
        case 32:
		{
			Uint32 *bits= ((Uint32 *)surface->pixels)+y*(surface->pitch>>2)+x;
			for (int n=l-1; n>=0; --n)
			{
                *bits++ = (Uint32)pixel;
			}

		}
		break;
		default:
			break;
    }
	SDL_UnlockSurface(surface);
}

void SDLDrawableSurface::drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	if (!surface)
		return;

	// from leto/calodox. 1999, Bresenham anti-aliased line code
	const int FIXED=8;
	const int I=255; /* nombre de degres different d'importance ligne/fond */
	const int Ibits=8;
	#define Swap(a,b) swap=a; a=b; b=swap;
	#define Abs(x) (x>0 ? x : -x)
	#define Sgn(x) (x>0 ? (x == 0 ? 0 : 1) : (x==0 ? 0 : -1))
    long dx, dy;
    long m,w;
    long e;
	Sint32 littleincx;
	Sint32 littleincy;
	Sint32 bigincx;
	Sint32 bigincy;
	Sint32 alphadecx;
	Sint32 alphadecy;

    long swap;
    int test=1;
	int x;

    /* calcul des deltas */
    dx= x2 - x1;
    if ( dx==0)
	{
		drawVertLine(x1,y1,y2-y1,r,g,b,a);
		return;
    }
    dy= y2 - y1;
    if ( dy==0)
	{
		drawHorzLine(x1,y1,x2-x1,r,g,b,a);
		return;
    }

    /* Y clipping */
    if (dy<0)
	{
		test = -test;
		Swap(x1,x2);
		Swap(y1,y2);
		dx=-dx;
		dy=-dy;
	}
    /* the 2 points are Y-sorted. (y1<=y2) */
    if (y2 < clipRect.y)
		return;
    if (y1 >= clipRect.y+clipRect.h)
		return;
    if (y1 < clipRect.y)
	{
		x1=x2-( (y2-clipRect.y)*(x2-x1) ) / (y2-y1);
		y1=clipRect.y;
    }
	if (y1==y2)
	{
		drawHorzLine(x1,y1,x2-x1,r,g,b,a);
		return;
    }
    if (y2 >= clipRect.y+clipRect.h)
	{
		x2=x1-( (y1-(clipRect.y+clipRect.h))*(x1-x2) ) / (y1-y2);
		y2=(clipRect.y+clipRect.h-1);
    }
	if ( x1==x2)
	{
		drawVertLine(x1,y1,y2-y1,r,g,b,a);
		return;
    }

	/* X clipping */
    if (dx<0)
	{
		test = -test;
		Swap(x1,x2);
		Swap(y1,y2);
		dx=-dx;
		dy=-dy;
    }
    /* the 2 points are X-sorted. (x1<=x2) */
    if (x2 < clipRect.x)
		return;
    if (x1 >= clipRect.x+clipRect.w)
		return;
    if (x1 < clipRect.x)
	{
		y1=y2-( (x2-clipRect.x)*(y2-y1) ) / (x2-x1);
		x1=clipRect.x;
    }
	if ( x1==x2)
	{
		drawVertLine(x1,y1,y2-y1,r,g,b,a);
		return;
    }
    if (x2 >= clipRect.x+clipRect.w)
	{
		y2=y1-( (x1-(clipRect.x+clipRect.w))*(y1-y2) ) / (x1-x2);
		x2=(clipRect.x+clipRect.w-1);
    }

	// last return case
	if (x1>=(clipRect.x+clipRect.w) || y1>=(clipRect.y+clipRect.h) || (x2<clipRect.x) || (y2<clipRect.y))
		return;

    dx = x2-x1;
    dy = y2-y1;
	/* prepare les variables pour dessiner la ligne
       dans la bonne direction */

    if (Abs(dx) > Abs(dy))
	{
		littleincx = 1;
		littleincy = 0;
		bigincx = 1;
		bigincy = Sgn(dy);
		alphadecx = 0;
		alphadecy = Sgn(dy);
    }
	else
	{
		// we swap x and y meaning
		test = -test;
		Swap(dx,dy);
		littleincx = 0;
		littleincy = 1;
		bigincx = Sgn(dx);
		bigincy = 1;
		alphadecx = 1;
		alphadecy = 0;
    }

    if (dx<0)
	{
		dx= -dx;
		littleincx=0;
		littleincy=-littleincy;
		bigincx =-bigincx;
		bigincy =-bigincy;
		alphadecy=-alphadecy;
    }

    /* calcul de la position initiale */
    int px,py;
	px=x1;
	py=y1;

	/* initialisation des variables pour l'algo de bresenham */
    if (dx==0)
		return;
	if (dy==0)
		return;
    m = (Abs(dy)<< (Ibits+FIXED)) / Abs(dx);
    w = (I <<FIXED)-m;
    e = 1<<(FIXED-1);

    /* premier point */
	drawPixel(px,py,r,g,b,(Uint8)(I-(e>>FIXED)));

    /* main loop */
    x=dx+1;
    if (x<=0)
		return;

	while (--x)
	{
		if (e < w)
		{
			px+=littleincx;
			py+=littleincy;
			e+= m;
		}
		else
		{
			px+=bigincx;
			py+=bigincy;
			e-= w;
		}
		drawPixel(px,py,r,g,b,(Uint8)(I-(e>>FIXED)));
		drawPixel(px+alphadecx,py+alphadecy,r,g,b,(Uint8)(e>>FIXED));
	}
}

void SDLDrawableSurface::drawCircle(int x, int y, int ray, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	if (!surface)
		return;

	// LINE version
	/*#ifndef M_PI
	#define M_PI 3.1415927
	#endif
	float pos=0.0f;
	float inc=(2*M_PI)/(float)ray;
	float lpx=x+(float)ray;
	float lpy=y;
	float px, py;

	for (int i=0; i<ray; i++)
	{
		pos+=inc;
		px=x+cos(pos)*(float)ray;
		py=y+sin(pos)*(float)ray;
		drawLine((int)lpx, (int)lpy, (int)px, (int)py, r, g, b, a);
		lpx=px;
		lpy=py;
	}*/
	// alpha VERISON
	/*
	int dx, dy, d;
	dx=0;
	dy=ray;
	d=0;
	do
	{
		// d is positive when point is outside
		int decAA, ad, alpha, oneMinusAlpha;
		decAA=0;
		ad=0;
		if (d>0)
		{
			// point trop dehors
			ad=d;
			decAA=-1;
		}
		else if (d<0)
		{
			// point trop dedans
			ad=-d;
			decAA=1;
		}
		oneMinusAlpha=((ad<<8)/(ray<<1));
		alpha=255-alpha;
		drawPixel(x+dx, y+dy, r, g, b, alpha);
		drawPixel(x+dx, y-dy, r, g, b, alpha);
		drawPixel(x-dx, y+dy, r, g, b, alpha);
		drawPixel(x-dx, y-dy, r, g, b, alpha);
		drawPixel(x+dy, y+dx, r, g, b, alpha);
		drawPixel(x+dy, y-dx, r, g, b, alpha);
		drawPixel(x-dy, y+dx, r, g, b, alpha);
		drawPixel(x-dy, y-dx, r, g, b, alpha);
		if (d)
		{
			drawPixel(x+dx, y+dy+decAA, r, g, b, oneMinusAlpha);
			drawPixel(x+dx, y-dy-decAA, r, g, b, oneMinusAlpha);
			drawPixel(x-dx, y+dy+decAA, r, g, b, oneMinusAlpha);
			drawPixel(x-dx, y-dy-decAA, r, g, b, oneMinusAlpha);
			drawPixel(x+dy+decAA, y+dx, r, g, b, oneMinusAlpha);
			drawPixel(x+dy+decAA, y-dx, r, g, b, oneMinusAlpha);
			drawPixel(x-dy-decAA, y+dx, r, g, b, oneMinusAlpha);
			drawPixel(x-dy-decAA, y-dx, r, g, b, oneMinusAlpha);
		}
		dx++;
		if (d>=0)
		{
			dy--;
			d += ((dx-dy)<<1)+2;			
		}
		else
		{
			d +=(dx<<1) +1;
		}
	}
	while (dx<=dy);
	*/
	int newAlpha=a>>2;
	int dx, dy, d;
	int rdx, rdy;
	int i;
	for (i=0; i<3; i++)
	{
		dx=0;
		dy=(ray<<1)+i;
		d=0;
	
		do
		{
			rdx=(dx>>1);
			rdy=(dy>>1);
			drawPixel(x+rdx, y+rdy, r, g, b, newAlpha);
			drawPixel(x+rdx, y-rdy, r, g, b, newAlpha);
			drawPixel(x-rdx, y+rdy, r, g, b, newAlpha);
			drawPixel(x-rdx, y-rdy, r, g, b, newAlpha);
			drawPixel(x+rdy, y+rdx, r, g, b, newAlpha);
			drawPixel(x+rdy, y-rdx, r, g, b, newAlpha);
			drawPixel(x-rdy, y+rdx, r, g, b, newAlpha);
			drawPixel(x-rdy, y-rdx, r, g, b, newAlpha);
			dx++;
			if (d>=0)
			{
				dy--;
				d += ((dx-dy)<<1)+2;			
			}
			else
			{
				d +=(dx<<1) +1;
			}
		}
		while (dx<=dy);
	}
}

// usefull macro to replace some char (like newline) with \0 in string
#define FILTER_OUT_CHAR(s, c) { char *_c; if ( (_c=(strchr(s, c)))!=NULL) *_c=0; }

void SDLDrawableSurface::drawString(int x, int y, const Font *font, int i)
{
	std::stringstream str;
	str << i;
	return this->drawString(x, y, 0, font, str.str().c_str());
}

void SDLDrawableSurface::drawString(int x, int y, const Font *font, const char *msg)
{
	return this->drawString(x, y, 0, font, msg);
}

void SDLDrawableSurface::drawString(int x, int y, int w, const Font *font, const char *msg)
{
	if (!surface)
		return;

	std::string output(msg);
	FILTER_OUT_CHAR(output.c_str(), '\n');
	FILTER_OUT_CHAR(output.c_str(), '\r');
	((const SDLFont *)font)->drawString(surface, x, y, w, output.c_str(), &clipRect);
}

void SDLDrawableSurface::drawSurface(int x, int y, DrawableSurface *surface)
{
	if (!surface)
		return;

	SDLDrawableSurface *sdlsurface=dynamic_cast<SDLDrawableSurface *>(surface);
	if ((sdlsurface) && (sdlsurface->surface))
	{
		SDL_Rect r;

		r.x=x;
		r.y=y;
		r.w=surface->getW();
		r.h=surface->getH();

		SDL_BlitSurface(sdlsurface->surface, NULL, this->surface, &r);
	}
}


// here begin the SDL Graphic Context part

SDLGraphicContext::SDLGraphicContext(void)
{
	surface=NULL;

	// Load the SDL library
	if ( SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO)<0 )
	{
		fprintf(stderr, "Toolkit : Initialisation Error : %s\n", SDL_GetError());
		exit(1);
	}
	else
	{
		fprintf(stderr, "Toolkit : Initialized : Graphic Context created\n");
	}

	atexit(SDL_Quit);
	screen=this;

	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
	SDL_EnableUNICODE(1);
	
	TTF_Init();
}

SDLGraphicContext::~SDLGraphicContext(void)
{
	fprintf(stderr, "Toolkit : Graphic Context destroyed\n");
	
	TTF_Quit();
}

bool SDLGraphicContext::setRes(int w, int h, int depth, Uint32 flags)
{
	Uint32 sdlFlags=SDL_DOUBLEBUF;

	this->flags=flags;
	if (flags&NO_DOUBLEBUF)
		sdlFlags=0;
	if (flags&FULLSCREEN)
		sdlFlags|=SDL_FULLSCREEN;
	if (flags&HWACCELERATED)
		sdlFlags|=SDL_HWSURFACE;
	if (flags&RESIZABLE)
		sdlFlags|=SDL_RESIZABLE;

	surface = SDL_SetVideoMode(w, h, depth, sdlFlags);

	if (!surface)
	{
		fprintf(stderr, "Toolkit : %s\n", SDL_GetError());
		return false;
	}
	else
	{
		setClipRect();
		if (flags&FULLSCREEN)
			fprintf(stderr, "Toolkit : Screen set to %dx%d at %d bpp in fullscreen\n", w, h, depth);
		else
			fprintf(stderr, "Toolkit : Screen set to %dx%d at %d bpp in window\n", w, h, depth);
		return true;
	}
}


void SDLGraphicContext::nextFrame(void)
{
	if (surface)
		SDL_Flip(surface);
}

void SDLGraphicContext::updateRects(SDL_Rect *rects, int size)
{
	if (surface)
		SDL_UpdateRects(surface, size, rects);
};

void SDLGraphicContext::updateRect(int x, int y, int w, int h)
{
	if (surface)
		SDL_UpdateRect(surface, x, y, w, h);
}

void SDLGraphicContext::loadImage(const char *name)
{
	if ((name) && (surface))
	{
		SDL_RWops *imageStream;
		if ((imageStream=Toolkit::getFileManager()->open(name, "rb", false))!=NULL)
		{
			SDL_Surface *temp;
			temp=IMG_Load_RW(imageStream, 0);
			if (temp)
			{
				SDL_Rect dRect;
				dRect.x=(surface->w-temp->w)>>1;
				dRect.y=(surface->h-temp->h)>>1;
				dRect.w=temp->w;
				dRect.h=temp->h;
				SDL_BlitSurface(temp, NULL, surface, &dRect);
				SDL_FreeSurface(temp);
			}
			SDL_RWclose(imageStream);
		}
	}
}

SDL_RWops *SDLGraphicContext::tryOpenImage(const char *name, int number, ImageType type)
{
	SDL_RWops *imageStream;
	char temp[1024];

	if (type==OVERLAY)
	{
		snprintf(temp, 1024,"%s%dm.png", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
#ifdef LOAD_ALL_IMAGE_TYPE
		snprintf(temp, 1024,"%s%dm.bmp", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dm.jpg", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dm.jpeg", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dm.pnm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dm.xpm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dm.lbm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dm.pcx", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dm.gif", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dm.tga", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
#endif
	}
	else if (type==NORMAL)
	{
		snprintf(temp, 1024,"%s%d.png", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
#ifdef LOAD_ALL_IMAGE_TYPE
		snprintf(temp, 1024,"%s%d.bmp", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%d.jpg", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%d.jpeg", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%d.pnm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%d.xpm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%d.lbm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%d.pcx", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%d.gif", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%d.tga", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
#endif
	}
	else if (type==PALETTE)
	{
		snprintf(temp, 1024,"%s%dp.png", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
#ifdef LOAD_ALL_IMAGE_TYPE
		snprintf(temp, 1024,"%s%dp.bmp", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dp.jpg", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dp.jpeg", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dp.pnm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dp.xpm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dp.lbm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dp.pcx", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dp.gif", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dp.tga", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
#endif
	}
	else if (type==ROTATED)
	{
		snprintf(temp, 1024,"%s%dr.png", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
#ifdef LOAD_ALL_IMAGE_TYPE
		snprintf(temp, 1024,"%s%dr.bmp", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dr.jpg", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dr.jpeg", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dr.pnm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dr.xpm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dr.lbm", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dr.pcx", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dr.gif", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
		snprintf(temp, 1024,"%s%dr.tga", name, number);
		if ((imageStream=Toolkit::getFileManager()->open(temp, "rb", false))!=NULL)
			return imageStream;
#endif
	}
	else
	{
		fprintf(stderr, "Toolkit : Passing wrong image type to SDLGraphicContext::tryOpenImage\n");
	}
	return NULL;
}

void SDLGraphicContext::loadSprite(const char *filename, const char *name)
{
	SDL_RWops *frameStream;
	SDL_RWops *overlayStream;
	SDL_RWops *paletizedStream;
	SDL_RWops *rotatedStream;
	int i=0;

	SDLSprite *sprite=new SDLSprite;

	while (true)
	{
		frameStream=tryOpenImage(filename, i, NORMAL);
		overlayStream=tryOpenImage(filename, i, OVERLAY);
		paletizedStream=tryOpenImage(filename, i, PALETTE);
		rotatedStream=tryOpenImage(filename, i, ROTATED);

		if (!((frameStream) || (overlayStream) || (paletizedStream) || (rotatedStream)))
			break;

		sprite->loadFrame(frameStream, overlayStream, paletizedStream, rotatedStream);

		if (frameStream)
			SDL_RWclose(frameStream);
		if (overlayStream)
			SDL_RWclose(overlayStream);
		if (paletizedStream)
			SDL_RWclose(paletizedStream);
		if (rotatedStream)
			SDL_RWclose(rotatedStream);
		i++;
	}
	Toolkit::spriteMap[std::string(name)] = sprite;
}

void SDLGraphicContext::loadFont(const char *filename, unsigned size, const char *name)
{
	Font *rf = NULL;
	
	SDLTTFont *ttf=new SDLTTFont();
	if (ttf->load(filename, size))
		rf = ttf;
	else
		delete ttf;
	
	if (!rf)
	{
		SDLBitmapFont *font=new SDLBitmapFont();
		if (font->load(filename))
			rf = font;
		else
			delete font;
	}

	if (rf)
	{
		Toolkit::fontMap[std::string(name)] = rf;
	}
	else
	{
		fprintf(stderr, "GAG : Can't load font %s from %s\n", name, filename);
	}
}

DrawableSurface *SDLGraphicContext::createDrawableSurface(const char *name)
{
	DrawableSurface *ds=new SDLDrawableSurface();
	ds->loadImage(name);
	return ds;
}


void SDLGraphicContext::printScreen(const char *filename)
{
	if (surface)
		SDL_SaveBMP(surface, filename);
}
