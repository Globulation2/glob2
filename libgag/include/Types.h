/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __TYPES_H
#define __TYPES_H

#include <stdlib.h>	// For NULL...

#ifndef DX9_BACKEND
#include <SDL.h>
#endif

#ifdef DX9_BACKEND

#include <windows.h>

// Some typedefs replacing the SDL stuff
typedef unsigned char Uint8;
typedef unsigned short Uint16;
typedef unsigned int Uint32;
typedef unsigned __int64 Uint64;
typedef char Sint8;
typedef short Sint16;
typedef int Sint32;
typedef __int64 Sint64;

struct SDL_RWops { };
struct IPaddress { };
struct UDPsocket { };

struct SDL_Rect		// TODO: Well, guess what :-)
{
	int x,y,w,h;
};

struct SDL_Event { };
#endif

namespace GAGCore
{
}

#endif
