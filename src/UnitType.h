// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __UNITTYPE_H
#define __UNITTYPE_H

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
	// caracteristic modulated by player choice, if 0, feature disabled
	// display infos
	Uint32 startImage[NB_MOVE];
	
	Sint32 hungryness;

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

