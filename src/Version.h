/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __VERSION_H
#define __VERSION_H

// This is the version of map and savegame format.
#define VERSION_MAJOR 0
#define VERSION_MINOR 20
// version 10 adds script saved in game
// version 11 the gamesfiles do saves which building has been seen under fog of war.
// version 12 saves map name into SessionGame instead of BaseMap.
// version 13 adds construction state into buildings
// version 14 adds the save of the end of game stats in Team.
// version 15 and 16 remove old compatibility because of major core engine changes.
// version 17 change the ressource system, move sore hardcoded ressource stuff to external file.
// version 18 adds fruit mask for unit happyness.
// version 19 removed optimisation parameters
// version 20 removed useless variable in Unit, used by old pathfinding. And added new Units-states.

#endif
