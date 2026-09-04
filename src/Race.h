// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __RACE_H
#define __RACE_H

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
	static void loadDefault();
	static Uint32 checkSumDefault();
	
	UnitType *getUnitType(int type, int level);
	
	void save(GAGCore::OutputStream *stream);
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	static Uint32 checkSum(void);
};

#endif
