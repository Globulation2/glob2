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

#ifndef __UNITTYPE_H
#define __UNITTYPE_H

#include "EntityType.h"
#include "UnitConsts.h"

class UnitType: public EntityType
{
public:
	// caracteristic modulated by player choice, if 0, feature disabled
	// display infos
#	define __STARTDATA_U ((Uint32*)startImage)

	Uint32 startImage[NB_MOVE];
	
	Sint32 hungryness;

	Sint32 performance[NB_ABILITY];

public:
	UnitType() { init(); }
	UnitType(GAGCore::InputStream *stream) { load(stream); }
	virtual ~UnitType() {}
	virtual const char **getVars(size_t *size, Uint32 **data)
	{
		static const char *vars[] =
		{
			"startImageStopWalk",
			"startImageStopSwim",
			"startImageStopFly",
			"startImageWalk",
			"startImageSwim",
			"startImageFly",
			"startImageBuild",
			"startImageHarvest",
			"startImageAttack",

			"hungryness",

			"stopWalkSpeed",
			"stopSwimSpeed",
			"stopFlySpeed",
			"walkSpeed",
			"swimSpeed",
			"flySpeed",
			"buildSpeed",
			"harvestSpeed",
			"attackSpeed",
			"attackForce",
			"armor",
			"hpMax"
		};
		if (size)
			*size=(sizeof(vars)/sizeof(char *));
		if (data)
			*data=__STARTDATA_U;
		return vars;
	}

public:
	UnitType& operator+=(const UnitType &a);
	UnitType operator+(const UnitType &a);
	UnitType& operator/=(int a);
	UnitType operator/(int a);
	UnitType& operator*=(int a);
	UnitType operator*(int a);
	int operator*(const UnitType &a);
	
	void copyIf(const UnitType a, const UnitType b);
	void copyIfNot(const UnitType a, const UnitType b);
	
};

#endif
 
