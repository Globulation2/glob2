// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "TeamDisplay.h"

#include "StringTable.h"
#include "Toolkit.h"

#include "Team.h"

using namespace GAGCore;

std::string displayPlayerName(const Team& team)
{
	return displayPlayerName(team.getFirstPlayerName());
}

std::string displayPlayerName(const std::string& name)
{
	if (name.empty())
		return Toolkit::getStringTable()->getString("[Uncontrolled]");
	return name;
}
