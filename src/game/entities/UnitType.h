// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GAGSys.h>
#include "UnitConsts.h"

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

// UnitType is an aggregate so that the per-type defaults in race.cpp can use
// C++20 designated initializers ({ .startImage = {...}, .hungryness = N, ... }).
// Aggregate-ness requires no user-declared constructors and no virtual
// functions; the previous virtual ~UnitType() and the unused
// UnitType(InputStream*) constructor were dropped accordingly. No callers
// derive from UnitType (only Race::unitTypes ever holds instances), so
// removing the virtual destructor is behavior-preserving.
struct UnitType
{
	// caracteristic modulated by player choice, if 0, feature disabled
	// display infos
	Uint32 startImage[NB_MOVE];

	Sint32 hungryness;

	Sint32 performance[NB_ABILITY];

	Sint32 harvestDamage;
	Sint32 armorReductionPerHappyness;
	Sint32 experiencePerLevel;

	Sint32 magicActionCooldown;

	UnitType& operator+=(const UnitType &a);
	UnitType operator+(const UnitType &a);
	UnitType& operator/=(int a);
	UnitType operator/(int a);
	UnitType& operator*=(int a);
	UnitType operator*(int a);
	int operator*(const UnitType &a);

	void copyIf(const UnitType a, const UnitType b);
	void copyIfNot(const UnitType a, const UnitType b);

	// Used by save-file serialization in Race::save() / Race::load(stream).
	// Note: the text-stream "data/units.txt" load path is gone — the default
	// table is now baked into race.cpp at compile time.
	void load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
};
