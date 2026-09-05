// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "UnitUtils.h"
#include "Team.h"


Sint32 UnitUtils::GIDtoID(Uint16 gid)
{
	assert(gid < UnitUtils::MAX_COUNT * Team::MAX_COUNT);
	return (gid % UnitUtils::MAX_COUNT);
}

Sint32 UnitUtils::GIDtoTeam(Uint16 gid)
{
	assert(gid < UnitUtils::MAX_COUNT * Team::MAX_COUNT);
	return (gid / UnitUtils::MAX_COUNT);
}

Uint16 UnitUtils::GIDfrom(Sint32 id, Sint32 team)
{
	assert(id >= 0);
	assert(id < UnitUtils::MAX_COUNT);
	assert(team >= 0);
	assert(team < Team::MAX_COUNT);
	return id + team * UnitUtils::MAX_COUNT;
}
