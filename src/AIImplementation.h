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

#ifndef __AI_IMPLEMENTATION_H
#define __AI_IMPLEMENTATION_H

/*
What's in AI ?
AI represents teh behaviour of an artificial intelligence player.
The main methode is Order *getOrder() which return the order to be used by the AI's team.
*/

#include "BuildingType.h"

class Player;
class Order;

/*
Howto make a new AI ?
If you want to build a new way AI behave, you have to:
Add a new Strategy to the below enum.
Add a new if in the getOrder methode.
Add a new case in the load and save methodes.
Fill these methodes correctly.

Warning:
You have to understand Order's mechanism.
Never uses rand() but allways syncRand().
(because the AI need to behave exactly the same on every computers.)
Be sure to return at least a *NullOrder, not NULL.

Idea:
You can access usefull data this way:
player
player->team
player->team->game
player->team->game->map

Fairness:
AI don't have restriction access to hidden part of the map.
You have to check it yourself, please do it.
Please don't uses too much CPU too.
Test games with a lot of AI that for.

Gameplay:
Player and AI may play together, in the same team.
Think if your AI is able to play with another human player ?
*/

class AIImplementation
{
public:
	AIImplementation(){}
	virtual void init(Player *player)=0;
	virtual ~AIImplementation(){}
	
	virtual bool load(SDL_RWops *stream, Player *player, Sint32 versionMinor)=0;
	virtual void save(SDL_RWops *stream)=0;
	
	virtual Order *getOrder(void)=0;
};

#endif

 

