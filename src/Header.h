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

#ifndef __HEADER_H
#define __HEADER_H

#define MAX_SINT32 0x7FFFFFFF

#ifdef WIN32
	#include <SDL.h>
	#include <SDL_endian.h>
#	include <SDL_image.h>
#	include <assert.h>
#	pragma warning (disable : 4786)
#	pragma warning (disable : 4250)
#elif macintosh
	#include "SDL.h"
	#include "SDL_endian.h"
	#include <Assert.h>
	#include <StdLib.h>
#else // Unix ??? autre ??? TODO a preciser...
	#include <SDL/SDL.h>
	#include <SDL/SDL_endian.h>
	#include <SDL/SDL_image.h>
#endif

//#define DBG_UID
#undef DBG_UID
#define DBG_PATHFINDING

#endif 
