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

#ifndef __RACE_H
#define __RACE_H

#include "Header.h"
#include "UnitType.h"

class Race
{
public:
	enum CreationType
	{
		USE_DEFAULT,
		USE_GUI,
		USE_AI
	};

	UnitType unitTypes[NB_UNIT_TYPE][NB_UNIT_LEVELS];

public:
	virtual ~Race();
	
	void create(CreationType creationType);
	UnitType *getUnitType(int type, int level);
	
	void save(SDL_RWops *stream);
	bool load(SDL_RWops *stream);
};

#endif
 
