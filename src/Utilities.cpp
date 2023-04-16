/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifdef _MSC_VER
#include <io.h> // for _open, _write
#include <BaseTsd.h> // for ssize_t
using size_t = SIZE_T;
using ssize_t = SSIZE_T;
#else
#include <unistd.h>
#endif
#include <math.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <Stream.h>
#include <ctime>

#include "Utilities.h"
#include "Game.h"

#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif


//Mersenne twister implementation
boost::mt19937 randomGenerator;

int distSquare(int x1, int y1, int x2, int y2)
{
	int dx=x2-x1;
	int dy=y2-y1;
	return (dx*dx+dy*dy);
}

void testRand()
{
	for (int m=1; m>0; m=m<<1)
	{
		int a=0;
		int b=0;
		for (int i=0; i<1000; i++)
			if (syncRand()&m)
				a++;
			else
				b++;
		printf("&[%x]=(%d, %d).\n", m, a, b);
	}
}

void setSyncRandSeed()
{
	///Sets the default seed
	randomGenerator.seed();
	//printf("ini rand=(%d, %d, %d).\n", randa, randb, randc);
}
void setSyncRandSeed(Uint32 seed)
{
	randomGenerator.seed(seed);
}

void setRandomSyncRandSeed()
{
	randomGenerator.seed(time(NULL));
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
		if (w<0)
			w=0;
		if (h<0)
			h=0;
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

	void RGBtoHSV( float r, float g, float b, float *h, float *s, float *v )
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

	void HSVtoRGB( float *r, float *g, float *b, float h, float s, float v )
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

	float fmin(float f1, float f2, float f3)
	{
		if ((f1<=f2) && (f1<=f3))
			return f1;
		else if (f2<=f3)
			return f2;
		else
			return f3;
	}

	float fmax(float f1, float f2, float f3)
	{
		if ((f1>=f2) && (f1>=f3))
			return f1;
		else if (f2>=f3)
			return f2;
		else
			return f3;
	}

	void computeMinimapData(int resolution, int mW, int mH, int *maxSize, int *sizeX, int *sizeY, int *decX, int *decY)
	{
		assert(mW>0);
		assert(mH>0);
		// get data
		if (mW>mH)
		{
			*maxSize=mW;
			*sizeX=resolution;
			*decX=0;
			*sizeY=(mH*resolution)/mW;
			*decY=(resolution-*sizeY)>>1;
		}
		else
		{
			*maxSize=mH;
			*sizeX=(mW*resolution)/mH;
			*decX=(resolution-*sizeX)>>1;
			*sizeY=resolution;
			*decY=0;
		}
	}
	
	Sint32 log2(Sint32 a)
	{
		assert(a);
		assert(a>0);
		Sint32 m=1;
		for (int i=0; i<32; i++)
			if (m==a)
				return i;
			else
				m=m<<1;
		assert(false);
		//failsafe relase case
		m=1;
		for (int i=0; i<32; i++)
			if (m>=a)
				return i;
			else
				m=m<<1;
		return 32;
	}
	
	Sint32 power2(Sint32 a)
	{
		assert(a>=0);
		assert(a<32);
		return 1<<a;
	}
	
	int strnlen(const char *s, int max)
	{
		for (int i=0; i<max; i++)
			if (*(s+i)==0)
				return i;
		return max;
	}
	
	int strmlen(const char *s, int max)
	{
		for (int i=0; i<max; i++)
			if (*(s+i)==0)
				return i+1;
		return max;
	}
	
	void stringIP(char *s, int n, Uint32 nip)
	{
		Uint32 ip=SDL_SwapBE32(nip);
		snprintf(s, n, "%d.%d.%d.%d", ((ip>>24)&0xFF), ((ip>>16)&0xFF), ((ip>>8)&0xFF), (ip&0xFF));
		s[n-1]=0;
	}
	
	char staticStringIP[8][128];
	int staticCounter;
	char *stringIP(Uint32 nip)
	{
		staticCounter=(staticCounter+1)&0x7;
		Uint32 ip=SDL_SwapBE32(nip);
		snprintf(staticStringIP[staticCounter], 128, "%d.%d.%d.%d", ((ip>>24)&0xFF), ((ip>>16)&0xFF), ((ip>>8)&0xFF), (ip&0xFF));
		staticStringIP[staticCounter][127]=0;
		return staticStringIP[staticCounter];
	}
	char *stringIP(Uint32 host, Uint16 port)
	{
		staticCounter=(staticCounter+1)&0x7;
		Uint32 ip=SDL_SwapBE32(host);
		snprintf(staticStringIP[staticCounter], 128, "%d.%d.%d.%d:%d", ((ip>>24)&0xFF), ((ip>>16)&0xFF), ((ip>>8)&0xFF), (ip&0xFF), SDL_SwapBE16(port));
		staticStringIP[staticCounter][127]=0;
		return staticStringIP[staticCounter];
	}
	
	char *stringIP(IPaddress nip)
	{
		staticCounter=(staticCounter+1)&0x7;
		Uint32 ip=SDL_SwapBE32(nip.host);
		snprintf(staticStringIP[staticCounter], 128, "%d.%d.%d.%d:%d", ((ip>>24)&0xFF), ((ip>>16)&0xFF), ((ip>>8)&0xFF), (ip&0xFF), SDL_SwapBE16(nip.port));
		staticStringIP[staticCounter][127]=0;
		return staticStringIP[staticCounter];
	}

	char *gets(char *dest, int size, GAGCore::InputStream *stream)
	{
		int i;
		for (i=0;i<size-1;i++)
		{
			char c;
			stream->read(&c, 1, "");
			if (stream->isEndOfStream())
				return NULL;
			switch (c)
			{
			case '\n':
			case '\r':
			case 0:
				dest[i]=0;
				return dest;
			default:
				dest[i]=c;
			}
		}
		dest[i]=0;
		return dest;
	}

	void streamprintf(GAGCore::OutputStream *stream, const char *format, ...)
	{
		char buffer[256];
		va_list arglist;
		va_start(arglist, format);
		vsnprintf(buffer, 256, format, arglist);
		stream->write(buffer, strlen(buffer), buffer);
		va_end(arglist);
	}
	
	int staticTokenize(const char *s, int n, char token[32][256])
	{
		int tokenNumber=0;
		for (int i=0; i<32; i++)
			token[i][0]=0;
		int tokenCharIndex=0;
		bool wasSpace=true;
		for (int i=0; i<n; i++)
		{
			char c=s[i];
			bool space=(c==' ')||(c=='=');
			if (space)
			{
				if (!wasSpace)
				{
					token[tokenNumber++][tokenCharIndex]=0;
					if (tokenNumber<32)
						tokenCharIndex=0;
					else
						break;
				}
			}
			else if (c==0 || c=='\n')
			{
				token[tokenNumber][tokenCharIndex]=0;
				if (tokenCharIndex>0)
					tokenNumber++;
				break;
			}
			else
			{
				if (tokenCharIndex<255)
					token[tokenNumber][tokenCharIndex++]=c;
			}
			wasSpace=space;
		}
		return tokenNumber;
	}
	
	void read(int fd, void *buf, size_t count)
	{
		char *ptr = (char *)buf;
		while (count)
		{
			ssize_t len = ::read(fd, ptr, count);
			if (len < 0)
			{
				throw Exception::FileDescriptorError(errno);
			}
			else if (len == 0)
			{
				throw Exception::FileDescriptorDisconnected();
			}
			else
			{
				ptr += len;
				count -= len;
			}
		}
	}
	
	void write(int fd, const void *buf, size_t count)
	{
		const char *ptr = (const char *)buf;
		while (count)
		{
			ssize_t len = ::write(fd, ptr, count);
			if (len < 0)
			{
				throw Exception::FileDescriptorError(errno);
			}
			else if (len == 0)
			{
				throw Exception::FileDescriptorDisconnected();
			}
			else
			{
				ptr += len;
				count -= len;
			}
		}
	}
}


