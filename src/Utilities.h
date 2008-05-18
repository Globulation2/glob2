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

#ifndef __UTILITIES_H_GZ
#define __UTILITIES_H_GZ

#include <stdlib.h>
#include <errno.h>

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_net.h>
#else
#include <Types.h>
#endif

#include <boost/random/mersenne_twister.hpp>

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

//Mersenne twister implementation
extern boost::mt19937 randomGenerator;

inline Uint32 syncRand(void)
{
	return randomGenerator();
}

///The actual random seeds are stored in GameHeader, which automatically randomizes them
void setSyncRandSeed();
void setSyncRandSeed(Uint32 seed);
void setRandomSyncRandSeed();

int distSquare(int x1, int y1, int x2, int y2);
#define SIGN(s) ((s) == 0 ? 0 : ((s)>0 ? 1 : -1) )

class Game;

namespace Utilities
{
	// rectangle
	//! return true if (x,y) is in r
	bool ptInRect(int x, int y, SDL_Rect *r);
	// FIXME : please Luc document this :
	void rectClipRect(int &x, int &y, int &w, int &h, SDL_Rect &r);
	void rectExtendRect(SDL_Rect *rs, SDL_Rect *rd);
	void rectExtendRect(int xs, int ys, int ws, int hs, int *xd, int *yd, int *wd, int *hd);
	void sdcRects(SDL_Rect *source, SDL_Rect *destination, SDL_Rect clipping);

	// color space conversion
	//! do a color space conversion from RGB to HSV
	void RGBtoHSV( float r, float g, float b, float *h, float *s, float *v );
	//! do a color space conversion from HSV to RGB
	void HSVtoRGB( float *r, float *g, float *b, float h, float s, float v );
	// color space conversion support functions
	//! return min of f1, f2 and f3
	float fmin(float f1, float f2, float f3);
	//! return max of f1, f2 and f3
	float fmax(float f1, float f2, float f3);

	//! Data for computing minimap
	/**
		Inputs
		\param resolution Resolution of minimap (in pixels)
		\param mW Width of the map (in case)
		\param mH Height of the map (in case)
		Outputs
		\param maxSize Max of mw and mH (in case)
		\param sizeX Width of the minimap (in pixels)
		\param sizeY Height of the minimap (in pixels)
		\param decX Displacement of the beginning of the minimap on x (in pixels)
		\param decY Displacement of the beginning of the minimap on y (in pixels)
	*/
	void computeMinimapData(int resolution, int mW, int mH, int *maxSize, int *sizeX, int *sizeY, int *decX, int *decY);
	
	Sint32 log2(Sint32 a);
	Sint32 power2(Sint32 a);
	
	//! return the length of the string. Maximum return value is "max".
	int strnlen(const char *s, int max);
	//! return the memory size of a string. Maximum return value is "max".
	int strmlen(const char *s, int max);
	
	void stringIP(char *s, int n, Uint32 ip);
	char *stringIP(Uint32 ip);
	char *stringIP(Uint32 host, Uint16 port);
	char *stringIP(IPaddress ip);

	//! read a string from a stream
	char *gets(char *dest, int size, GAGCore::InputStream *stream);
	void streamprintf(GAGCore::OutputStream *stream, const char *format, ...);
	
	//! tokenize the string into 32 static char[256] strings. Returns the number of tokens. All tokens are valids
	int staticTokenize(const char *s, int n, char token[32][256]);
	
	//! helpers for fixed points manipulation
	inline int intToFixed(int i, const int precision = 256) {return  i * precision; }
	inline int fixedToInt(int f, const int precision = 256) {return  f / precision; }
	
	
	// sockets helpers
	namespace Exception
	{
		struct FileDescriptor
		{
		
		};
	
		//! An error (not normal disconnection) occcured while reading or writing on the file descriptor.
		struct FileDescriptorError : public FileDescriptor
		{
			FileDescriptorError(int errNumber) : errNumber(errNumber) {}
			int errNumber;
		};
		
		//! The socket was file descriptor by remote peer, this may happen if file descriptor is a socket for instance.
		struct FileDescriptorDisconnected : public FileDescriptor
		{
		};
	}
	
	
	/*! Read data on a file descriptor
		\param fd source file descriptor,
		\param buf destination pointer where to put the data,
		\param count exact amout to read. The function only returns when this amount of data has been written or if an exception has been raised.
	*/
	void read(int fd, void *buf, size_t count);
	
	/*! Write data on a file descriptor
		\param fd destination file descriptor,
		\param buf source pointer where to get the data,
		\param count exact amout to write. The function only returns when this amount of data has been written or if an exception has been raised.
	*/
	void write(int fd, const void *buf, size_t count);
};

#endif

