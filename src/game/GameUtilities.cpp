// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GameUtilities.h"
#include "Game.h"

namespace GameUtilities
{
	void globalCoordToLocalView(const Game *game, int localTeam, int globalX, int globalY, int *localX, int *localY)
	{
		assert(game);
		assert(localX);
		assert(localY);
		if (localTeam>=0)
		{
			*localX = (globalX - game->teams[localTeam]->startPosX + (game->map.getW()>>1)) & game->map.getMaskW();
			*localY = (globalY - game->teams[localTeam]->startPosY + (game->map.getH()>>1)) & game->map.getMaskH();
		}
		else
		{
			*localX = globalX;
			*localY = globalY;
		}
	}
}


