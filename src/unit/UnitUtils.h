// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL3_net/SDL_net.h>
#include "TeamCapacity.h"
#include <cassert>
#include <cstdint>

class UnitUtils
{
 public:
	static Sint32 GIDtoID(Uint16 gid)
	{
		assert(gid < MAX_COUNT * TEAM_MAX_COUNT);
		return (gid % MAX_COUNT);
	}

	static Sint32 GIDtoTeam(Uint16 gid)
	{
		assert(gid < MAX_COUNT * TEAM_MAX_COUNT);
		return (gid / MAX_COUNT);
	}

	static Uint16 GIDfrom(Sint32 id, Sint32 team)
	{
		assert(id >= 0);
		assert(id < MAX_COUNT);
		assert(team >= 0);
		assert(team < TEAM_MAX_COUNT);
		return id + team * MAX_COUNT;
	}

	static const int MAX_COUNT = 1024;
};

// 0xffff is the map's empty-entity sentinel; valid GIDs must remain below it.
static_assert(UnitUtils::MAX_COUNT * TEAM_MAX_COUNT <= UINT16_MAX);
