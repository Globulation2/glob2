/*
 * Globulation 2 some usefull and stupid utilities
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "Utilities.h"

int sign(int s)
{
	if (s==0)
		return 0;
	else if (s<0)
		return -1;
	else
		return 1;
}

int distSquare(int x1, int y1, int x2, int y2)
{
	int dx=x2-x1;
	int dy=y2-y1;
	return (dx*dx+dy*dy);
}

Uint32 randa=1;
Uint32 randb=1;
Uint32 randc=0;

/*Uint32 syncRand(void)
{
	randValue+=3753454343u;
	randValue%=2657467897u;
	return randValue;
}*/

Uint32 syncRand(void)
{
	randa=randa<<3;
	randa+=0x1377;
	randa^=0xF088;
	
	randb+=0xFB34;
	randb^=0x78F4;
	
	randc=randc+randc+randc;
	randc^=0xEAC7;
	
	return ( randc^randa^randb );
}

void setSyncRandSeed()
{
	randa=0x1AE7;
	randb=0xBC24;
	randc=0xD3F5;
}
void setSyncRandSeedA(Uint32 seed)
{
	randa=seed;
}
void setSyncRandSeedB(Uint32 seed)
{
	randb=seed;
}
void setSyncRandSeedC(Uint32 seed)
{
	randc=seed;
}

Uint32 getSyncRandSeedA(void)
{
	return randa;
}
Uint32 getSyncRandSeedB(void)
{
	return randb;
}
Uint32 getSyncRandSeedC(void)
{
	return randc;
}

namespace Utilities
{
	bool ptInRect(int x, int y, SDL_Rect *r)
	{
		return ( (x>=r->x) && (y>=r->y) && (x<=r->x+r->w) && (y<=r->y+r->h) );
	}

	void rectClipRect(int &x, int &y, int &w, int &h, SDL_Rect &r)
	{
		if (x<r.x)
		{
			w-=r.x-x;
			x=r.x;
		}
		if (y<r.y)
		{
			h-=r.y-y;
			y=r.y;
		}
		if (w+x>r.x+r.w)
			w=r.x+r.w-x;
		if (h+y>r.y+r.h)
			h=r.y+r.h-y;
	}

	void rectExtendRect(SDL_Rect *rs, SDL_Rect *rd)
	{
		if (rs->x+rs->w > rd->x+rd->w)
			rd->w = rs->w +rs->x-rd->x;
		if (rs->y+rs->h > rd->y+rd->h)
			rd->h = rs->h +rs->y-rd->y;

		if (rs->x < rd->x)
		{
			rd->w+= rd->x-rs->x;
			rd->x = rs->x;
		}

		if (rs->y < rd->y)
		{
			rd->h+= rd->y-rs->y;
			rd->y = rs->y;
		}
	}

	void rectExtendRect(int xs, int ys, int ws, int hs, int *xd, int *yd, int *wd, int *hd)
	{
		if (xs+ws > *xd+*wd)
			*wd = ws +xs-*xd;
		if (ys+hs > *yd+*hd)
			*hd = hs +ys-*yd;

		if (xs < *xd)
		{
			*wd+= *xd-xs;
			*xd = xs;
		}

		if (ys < *yd)
		{
			*hd+= *yd-ys;
			*yd = ys;
		}
	}
	
	void sdcRects(SDL_Rect *source, SDL_Rect *destination, SDL_Rect clipping)
	{
		//sdc= Source-Destination-Clipping
		//Use if destination have the same size than source & cliping on destination
		int dx=clipping.x-destination->x;
		int dy=clipping.y-destination->y;

		int sw=source->w;
		int sh=source->h;

		if (dx>0)
		{
			source->x+=dx;
			destination->x+=dx;

			sw-=dx;
			destination->w-=dx;
		}
		if (dy>0)
		{
			source->y+=dy;
			destination->y+=dy;

			sh-=dy;
			destination->h-=dy;
		}

		int dwx=(destination->x+destination->w)-(clipping.x+clipping.w);
		int dhy=(destination->y+destination->h)-(clipping.y+clipping.h);

		if (dwx>0)
		{
			sw-=dwx;
			destination->w-=dwx;
		}
		if (dhy>0)
		{
			sh-=dhy;
			destination->h-=dhy;
		}

		if (sw>0)
			source->w=(Uint16)sw;
		else
			source->w=0;

		if (sh>0)
			source->h=(Uint16)sh;
		else
			source->h=0;

	}
	
}


