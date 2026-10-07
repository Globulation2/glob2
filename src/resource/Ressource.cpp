// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Ressource.h"
#include "StringTable.h"
#include "Toolkit.h"

using namespace GAGCore;

std::string getMaterialName(int material)
{
	static constexpr const char* labels[] = {"[Wood]", "[Food]", "[Paper]", "[Stone]", "[Algae]",
		"[Cherries]", "[Oranges]", "[Prunes]", "[Gold]", "[Metal]", "[Glass]", "[Fabric]"};
	return material >= 0 && validMaterial(unsigned(material)) ? Toolkit::getStringTable()->getString(labels[material]) : "";
}

std::string getResourceDisplayName(const std::string& authoredName)
{
    const auto token = "[" + authoredName + "]";
    const auto* strings = Toolkit::getStringTable();
    return strings && strings->doesStringExist(token) ? strings->getString(token) : authoredName;
}
