// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <assert.h>

#include "Toolkit.h"
#include "StringTable.h"

#include "UnitConsts.h"
#include "UnitDisplayNames.h"
#include "UnitCatalog.h"

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
		return {};
	}
}

std::string getUnitName(int type, const UnitCatalog& catalog)
{
    if (type < 0 || static_cast<std::size_t>(type) >= catalog.size()) return {};
    if (type < int(BuiltinUnitCount)) return getUnitName(type);
    const auto& definition = catalog.definition(type);
    return definition.name.empty() ? definition.key : definition.name;
}
