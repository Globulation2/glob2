/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charri?re
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

#include "Ressource.h"

RessourceType::RessourceType()
{
	name="undefined";
	terrain=0;
	gfxId=0;
	sizesCount=0;
	varietiesCount=0;
	shrinkable=0;
	eternal=0;
}

RessourcesTypes::RessourcesTypes()
{
	wood=0;
	corn=1;
	fungus=2;
	stone=3;
	alga=4;
}
