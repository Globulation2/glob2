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

enum RessourceType
{
	WOOD=0,
	CORN=1,
	FUNGUS=2,
	STONE=3,
	ALGA=4,
	NB_RESSOURCES
};

#endif
