/*
 * Globulation 2 header
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
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
#elif macintosh
	#include "SDL.h"
	#include "SDL_endian.h"
	#include <Assert.h>
	#include <StdLib.h>
#else
	#include <SDL/SDL.h>
	#include <SDL/SDL_endian.h>
	#include <SDL/SDL_image.h>
#endif

//#define DBG_UID
#undef DBG_UID
#define DBG_PATHFINDING

#endif 
