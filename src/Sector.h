/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __SECTOR_H
#define __SECTOR_H

#include <list>
#include <SDL_rwops.h>

class Map;
class Game;
class Bullet;

// a 16x16 piece of Map
class Sector
{
public:
	Sector() {}
	Sector(Game *);
	virtual ~Sector(void);
	// !This call is needed to use the Sector!
	void setGame(Game *game);

	void free(void);

	std::list<Bullet *> bullets;

	void save(SDL_RWops *stream);
	bool load(SDL_RWops *stream, Game *game);

	void step(void);
private:
	Map *map;
	Game *game;
};

#endif
 
