// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __BUILDING_UTILS_H
#define __BUILDING_UTILS_H

#include <SDL_net.h>

class BuildingUtils
{
 public:
	static Sint32 GIDtoID(Uint16 gid);
	static Sint32 GIDtoTeam(Uint16 gid);
	static Uint16 GIDfrom(Sint32 id, Sint32 team);

	static const int MAX_COUNT = 1024;
};


#endif  // __BUILDING_UTILS_H

