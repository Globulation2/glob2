/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charri�e
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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
	//The following values are integers, but are used like booleans.
	Sint32 shrinkable; //whether the resource is depleted when it is collected.
	Sint32 expendable;	//probably a misspelling of 'extendable'. What it actually determines is whether
						//the resource multiplies itself to adjacent squares over time.
	Sint32 eternal; //whether the resource cannot be destroyed or completely consumed.
	Sint32 granular; //whether the resource is decremented, rather than removed, when it is harvested/cleared.
	Sint32 visibleToBeCollected; //whether the resource can only be collected if the fog of war is cleared on its location.
	Sint32 minimapR, minimapG, minimapB;

public:
	RessourceType() { init(); }
	RessourceType(GAGCore::InputStream *stream) { load(stream); }
	Uint32 checkSum(void) { return shrinkable+(expendable<<1)+(eternal<<2)+(granular<<3)+(visibleToBeCollected<<4);}
	virtual ~RessourceType() { }
	virtual const char **getVars(size_t *size, Uint32 **data)
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
