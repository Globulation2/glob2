// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL_net.h>

class BuildingUtils
{
 public:
	static Sint32 GIDtoID(Uint16 gid);
	static Sint32 GIDtoTeam(Uint16 gid);
	static Uint16 GIDfrom(Sint32 id, Sint32 team);

	/// Map an octant index (0..7) plus a (ring, offset) pair to the tile
	/// coordinate it addresses around a turret at (posX, posY). Pure integer
	/// geometry with no instance state — the eight cases tile the square ring at
	/// distance `ring` so the turret's target scan visits every surrounding tile.
	/// Static so it can be unit-tested without a Building/Game instance.
	static void turretScanTile(int posX, int posY, int ring, int offset,
	                           int octant, int& outX, int& outY);

	static const int MAX_COUNT = 1024;
};


