// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BuildingUtils.h"
#include "Team.h"


Sint32 BuildingUtils::GIDtoID(Uint16 gid)
{
	assert(gid < BuildingUtils::MAX_COUNT * Team::MAX_COUNT);
	return gid % BuildingUtils::MAX_COUNT;
}

Sint32 BuildingUtils::GIDtoTeam(Uint16 gid)
{
	assert(gid < BuildingUtils::MAX_COUNT * Team::MAX_COUNT);
	return gid / BuildingUtils::MAX_COUNT;
}

Uint16 BuildingUtils::GIDfrom(Sint32 id, Sint32 team)
{
	assert(id < BuildingUtils::MAX_COUNT);
	assert(team < Team::MAX_COUNT);
	return id + team * BuildingUtils::MAX_COUNT;
}

