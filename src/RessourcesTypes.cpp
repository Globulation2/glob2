/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charri�e
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <assert.h>
#include <vector>
#include <iostream>

#include "RessourcesTypes.h"
#include "GlobalContainer.h"

Uint32 RessourcesTypes::checkSum(void)
{
	Uint32 cs = 0;
	
	for (std::vector <RessourceType *>::iterator it=entitiesTypes.begin(); it!=entitiesTypes.end(); ++it)
	{
		cs ^= (*it)->checkSum();
		cs = (cs<<1) | (cs>>31);
	}
	
	return cs;
}
