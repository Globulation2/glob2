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

#include "Header.h"
#include "EntityType.h"
#include <assert.h>

//! No ressource identifier. This correspond to ressource type 255. On this case, variety, amout and animation are undefined.
#define NORESID 0xFFFFFFFF

//! A union is used so a ressource can be accessed as a whole 32 bit unsigned int as well as 4 bytes defining its components (type, ...).
struct Ressource
{
	Uint8 type;
	Uint8 variety;
	Uint8 amount;
	Uint8 animation;
	
	Ressource(Uint32 i=NORESID) { animation=i&0xFF; amount=(i>>8)&0xFF; variety=(i>>16)&0xFF; type=(i>>24)&0xFF; }
	operator Uint32() const { return animation | (amount<<8) | (variety<<16) | (type<<24); }
};

class RessourceType: public EntityType
{
public:
#define __STARTDATA_R ((Uint32*)&terrain)
	Sint32 terrain;
	Sint32 gfxId;
	Sint32 sizesCount;
	Sint32 varietiesCount;
	Sint32 shrinkable;
	Sint32 expendable;
	Sint32 eternal;
	Sint32 granular;
	Sint32 visibleToBeCollected;
	Sint32 minimapR, minimapG, minimapB;

public:
	RessourceType() { init(); }
	RessourceType(SDL_RWops *stream) { load(stream); }
	virtual ~RessourceType() { }
	virtual const char **getVars(int *size, Uint32 **data)
	{
		static const char *vars[] =
		{
			"terrain",
			"gfxId",
			"sizesCount",
			"varietiesCount",
			"shrinkable",
			"expendable",
			"eternal",
			"granular",
			"visibleToBeCollected",
			"minimapR",
			"minimapG",
			"minimapB",
		};
		if (size)
			*size=(sizeof(vars)/sizeof(char *));
		if (data)
			*data=__STARTDATA_R;
		return vars;
	}
};

typedef EntitiesTypes<RessourceType> RessourcesTypes;

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
