/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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
#include <assert.h>

//! No ressource identifier. This correspond to ressource type 255. On this case, variety, amout and animation are undefined.
#define NORESID 0xFFFFFFFF

//! A union is used so a ressource can be accessed as a whole 32 bit unsigned int as well as 4 bytes defining its components (type, ...).
union Ressource
{
	Uint32 id;
	struct
	{
		Uint8 type;
		Uint8 variety;
		Uint8 amount;
		Uint8 animation;
	} field;
};

class RessourceType : public base::Object
{
protected:
	CLASSDEF(RessourceType)
		BASECLASS(base::Object)
	MEMBERS
		//! the name of the ressource, will be lookuped for internationalisation
		ITEM(std::string, name)
		//! The type of terrain of which this ressource can grow
		ITEM(Sint32, terrain)
		//! The First image of the ressource
		ITEM(Sint32, gfxId)
		//! The number of images this ressource has for a given variety
		ITEM(Sint32, sizesCount)
		//! The number of veriety this ressource has
		ITEM(Sint32, varietiesCount)
		//! Does this ressource shrinks when harvested. A non-shrinkable ressource is by definition eternal.
		ITEM(bool, shrinkable)
		//! If expendable, a ressource spaw to nearby terrain
		ITEM(bool, expendable)
		//! If ethernal, a ressource cannot be fully harvested. There is always an amout of 1.
		ITEM(bool, eternal)
		//! If granular, we can decrement one by one. Otherwise, the ressource is fully taken
		ITEM(bool, granular)
		//! We given to a building, the buildings get multiplicator amount of ressource
		ITEM(Sint32, multiplicator)
		//! If yes, the ressource need to be visible NOW to be able to be collected
		ITEM(bool, visibleToBeCollected)
	CLASSEND;
public:
	RessourceType();
};

class RessourcesTypes : public base::Object
{
public:
	typedef Uint32 intResType;

public:
	CLASSDEF(RessourcesTypes)
		BASECLASS(base::Object)
	MEMBERS
		ITEM(std::vector<RessourceType>, res)
		ITEM(intResType, wood)
		ITEM(intResType, corn)
		ITEM(intResType, fungus)
		ITEM(intResType, stone)
		ITEM(intResType, alga)
		ITEM(intResType, happynessBase)
		ITEM(intResType, happynessCount)
	CLASSEND;

public:
	const RessourceType* get(const unsigned i) { assert(i < res.size()); return &res[i]; }
	unsigned int number() { return res.size(); }

	RessourcesTypes();
};

#define MAX_NB_RESSOURCES 20
#define MAX_RESSOURCES 7
#define WOOD (globalContainer->ressourcesTypes->wood)
#define CORN (globalContainer->ressourcesTypes->corn)
#define FUNGUS (globalContainer->ressourcesTypes->fungus)
#define STONE (globalContainer->ressourcesTypes->stone)
#define ALGA (globalContainer->ressourcesTypes->alga)
#define HAPPYNESS_BASE (globalContainer->ressourcesTypes->happynessBase)
#define HAPPYNESS_COUNT (globalContainer->ressourcesTypes->happynessCount)

#endif
