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

#ifndef __RESSOURCE_TYPE_H
#define __RESSOURCE_TYPE_H

#include "EntityType.h"
#include "Ressource.h"

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

#endif
