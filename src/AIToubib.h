/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  Copyright (C) 2004 Jean-David Maillefer
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com
  or jdmaillefer AT bluewin DOT ch

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

#ifndef __AI_TOUBIB_H
#define __AI_TOUBIB_H

#include "AIImplementation.h"

class Game;
class Map;
class Order;
class Player;
class Team;
class Building;

class AIToubib : public AIImplementation
{
public:
	AIToubib(Player *player);
	AIToubib(SDL_RWops *stream, Player *player, Sint32 versionMinor);
	virtual ~AIToubib();
	
	Player *player;
	Team *team;
	Game *game;
	Map *map;
	

	//! Load saved AI state from a stream
	bool load(SDL_RWops *stream, Player *player, Sint32 versionMinor);
	//! Save AI state to a stream
	void save(SDL_RWops *stream);
	
	//! return a new order in response to last events
	Order *getOrder(void);
	
private:
	//! Initialization (avoid duplicate code)
	void init(Player *player);
};

#endif

 

