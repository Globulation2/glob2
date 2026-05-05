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

#pragma once

#include "unit_type.h"

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
