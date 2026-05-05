// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Ressource.h"
#include "StringTable.h"
#include "Toolkit.h"

using namespace GAGCore;

std::string getRessourceName(int type)
{
	if(type == WOOD)
		return Toolkit::getStringTable()->getString("[Wood]");
	if(type == CORN)
		return Toolkit::getStringTable()->getString("[Wheat]");
	if(type == PAPYRUS)
		return Toolkit::getStringTable()->getString("[Papyrus]");
	if(type == STONE)
		return Toolkit::getStringTable()->getString("[Stone]");
	if(type == ALGA)
		return Toolkit::getStringTable()->getString("[Alga]");
	if(type == CHERRY)
		return Toolkit::getStringTable()->getString("[Cherry]");
	if(type == ORANGE)
		return Toolkit::getStringTable()->getString("[Orange]");
	if(type == PRUNE)
		return Toolkit::getStringTable()->getString("[Prune]");
	return "";
}
