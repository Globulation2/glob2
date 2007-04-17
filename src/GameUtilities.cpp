/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

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


