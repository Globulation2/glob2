/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __GAGSYS_H
#define __GAGSYS_H

#ifndef MAX_SINT32
#define MAX_SINT32 0x7FFFFFFF
#endif

#ifdef WIN32
#include <windows.h>
#include <sys/types.h>
#include <sys/stat.h>
#	include <SDL.h>
#	include <SDL_endian.h>
#	include <SDL_image.h>
#	include <assert.h>
#	define snprintf _snprintf
#	define vsnprintf _vsnprintf
#   define S_IFDIR _S_IFDIR
#	pragma warning (disable : 4786)
#	pragma warning (disable : 4250)
#else // Unix ??? autre ??? TODO a preciser...
	#include <SDL/SDL.h>
	#include <SDL/SDL_endian.h>
	#include <SDL/SDL_image.h>
#endif

// usefull macros
#ifndef MAX
#define MAX(a, b) ((a)>(b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) ((a)<(b) ? (a) : (b))
#endif

#ifndef VARARRAY
#ifdef _MSC_VER
#include <malloc.h>
#define VARARRAY(t,n,s) t *n=(t*)_alloca((s)*sizeof(t))
#define strcasecmp _stricmp
#else
#define VARARRAY(t,n,s) t n[s]
#endif
#endif

#include <string.h>

//! strdup function using new
inline char* newstrdup(const char* str)
{
	char* newStr = new char[strlen(str)+1];
	return strcpy(newStr, str);
}

#endif 
