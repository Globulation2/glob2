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

#ifndef __HEADER_H
#define __HEADER_H

#define MAX_SINT32 0x7FFFFFFF

#ifdef WIN32
#	include <windows.h>
#	include <SDL.h>
#	include <SDL_endian.h>
#	include <SDL_image.h>
#	include <SDL_net.h>
#	include <assert.h>
#	define snprintf _snprintf
#	define vsnprintf _vsnprintf
#	pragma warning (disable : 4786)
#	pragma warning (disable : 4250)
#elif macintosh
	#include "SDL.h"
	#include "SDL_endian.h"
	#include <Assert.h>
	#include <StdLib.h>
        #import <Cocoa/Cocoa.h>
#else // Unix ??? autre ??? TODO a preciser...
	#include <SDL/SDL.h>
	#include <SDL/SDL_endian.h>
	#include <SDL/SDL_image.h>
	#include <SDL/SDL_net.h>
#endif

// usefull macros
#define MAX(a, b) ((a)>(b) ? (a) : (b))
#define MIN(a, b) ((a)<(b) ? (a) : (b))

//! if defined, enable UID debug code
//#define DBG_UID

//! if defined, enable pathfinding debug code
#define DBG_PATHFINDING

//! define this to true to have a verbose vPath error by default and to false otherwise
#ifndef DBG_VPATH_OPEN
#define DBG_VPATH_OPEN false
#endif

//! if defined, enable vPath listing
//#define DBG_VPATH_LIST

//! if defined, enable fow and map invisible
//#define DBG_ALL_MAP_DISCOVERED


#endif 
