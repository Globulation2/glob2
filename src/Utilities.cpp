// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
#include <sstream>

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

void setSyncRandSeed()
{
	///Sets the default seed
	randomGenerator.seed();
}
void setSyncRandSeed(Uint32 seed)
{
	randomGenerator.seed(seed);
}

void setRandomSyncRandSeed()
{
	randomGenerator.seed(time(NULL));
}

std::string getSyncRandState()
{
	std::ostringstream stream;
	stream<<randomGenerator;
	return stream.str();
}

bool setSyncRandState(const std::string& state)
{
	// Boost's extractor consumes trailing whitespace after every state word.
	// Supplying a terminator keeps its final std::ws from turning EOF into a
	// parse failure. Parse into a temporary so malformed state cannot partly
	// replace the live synchronized generator.
	std::istringstream stream(state+"\n");
	boost::mt19937 restored;
	stream>>restored;
	if(stream.fail())
		return false;
	randomGenerator=restored;
	return true;
}

namespace Utilities
{
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
	
	int strmlen(const char *s, int max)
	{
		for (int i=0; i<max; i++)
			if (*(s+i)==0)
				return i+1;
		return max;
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
	
	std::string stripPrefix(const std::string& s, const std::string& prefix)
	{
		if (s.compare(0, prefix.size(), prefix) == 0)
			return s.substr(prefix.size());
		return s;
	}

	std::string stripSuffix(const std::string& s, const std::string& suffix)
	{
		if (s.size() >= suffix.size()
			&& s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
			return s.substr(0, s.size() - suffix.size());
		return s;
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
}


