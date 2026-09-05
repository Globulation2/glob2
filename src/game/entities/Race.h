// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "UnitType.h"

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

class Race
{
public:
	static UnitType unitTypes[NB_UNIT_TYPE][NB_UNIT_LEVELS];
	static Sint32 hungryness;

public:
	Race();
	virtual ~Race();

	void load();
	// Installs the compile-time default unit-type table from race.cpp into
	// Race::unitTypes (and seeds Race::hungryness). Replaces the previous
	// runtime parser of data/units.txt.
	static void loadDefault();

	UnitType *getUnitType(int type, int level);

	void save(GAGCore::OutputStream *stream);
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
};
