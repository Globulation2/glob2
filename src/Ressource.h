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

#ifndef __RESSOURCE_H
#define __RESSOURCE_H

#include <SDL.h>

//! No ressource identifier. This correspond to ressource type 255. On this case, variety, amout and animation are undefined.
#define NO_RES_TYPE 0xFF

//! A union is used so a ressource can be accessed as a whole 32 bit unsigned int as well as 4 bytes defining its components (type, ...).
struct Ressource
{
	Uint8 type;
	Uint8 variety;
	Uint8 amount;
	Uint8 animation;
	
	void clear() {type=0xFF; variety=0xFF;  amount=0xFF;  animation=0xFF; }
	//void setUint32(Uint32 i) { animation=i&0xFF; amount=(i>>8)&0xFF; variety=(i>>16)&0xFF; type=(i>>24)&0xFF; }
	Uint32 getUint32() { return animation | (amount<<8) | (variety<<16) | (type<<24); }
};

#define MAX_NB_RESSOURCES 15
#define MAX_RESSOURCES 8
#define WOOD 0
#define CORN 1
#define PAPYRUS 2
#define STONE 3
#define ALGA 4
#define BASIC_COUNT 5
#define HAPPYNESS_BASE 5
#define HAPPYNESS_COUNT (MAX_RESSOURCES-BASIC_COUNT)

#endif
