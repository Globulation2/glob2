// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Toolkit.h"
#include "StringTable.h"

#include "UnitConsts.h"

using namespace GAGCore;

std::string getUnitName(int type)
{
	switch(type)
	{
	case WORKER:
		return Toolkit::getStringTable()->getString("[Worker]");
	case WARRIOR:
		return Toolkit::getStringTable()->getString("[Warrior]");
	case EXPLORER:
		return Toolkit::getStringTable()->getString("[Explorer]");
	default:
		assert(false);
		return "";//to satisfy -Wall
	}
}
