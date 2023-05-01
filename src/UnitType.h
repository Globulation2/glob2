/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __UNIT_TYPE_H
#define __UNIT_TYPE_H

#include <GAGSys.h>
#include "UnitConsts.h"

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

class UnitType
{
public:
	// characteristic modulated by player choice, if 0, feature disabled
	// display infos
	Uint32 startImage[NB_MOVE];
	
	Sint32 hungriness;

	Sint32 performance[NB_ABILITY];
	
	Sint32 harvestDamage;
	Sint32 armorReductionPerHappyness;
	Sint32 experiencePerLevel;
	
	Sint32 magicActionCooldown;

public:
	UnitType() {}
	UnitType(GAGCore::InputStream *stream, Sint32 versionMinor) { load(stream, versionMinor); }
	virtual ~UnitType() {}

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
	
	void load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	Uint32 checkSum(void);
};

#endif

