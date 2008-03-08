/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
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
AI represents the behaviour of an artificial intelligence player.
The main method is boost::shared_ptr<Order> getOrder() which return the order to be used by the AI's team.
*/

#include "BuildingType.h"
#include <boost/shared_ptr.hpp>

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}
class Player;
class Order;

/*
Howto make a new AI ?
If you want to build a new way AI behave, you have to:
Add a new Strategy to the AI::ImplementitionID enum.
Add a new case in the AI::load method.
Create a subclass of AIImplementation.
Fill AIImplenetation's methods correctly for that subclass.

Warning:
You have to understand how the Order class is used.
Never use rand(), always syncRand().
(because the AI need to behave exactly the same on every computer.)
Be sure to return at least a *NullOrder, not NULL.

Idea:
You can access usefull data this way:
player
player->team
player->team->game
player->team->game->map
The current AIs store pointers to all these for convenient access.

Fairness:
AI don't have restricted access to hidden part of the map.
You have to check it yourself, please do it.
Please don't use too much CPU either.
Test games with a lot of AI for that.

Gameplay:
Player and AI may play together, in the same team.
Think if your AI is able to play with a human player?
*/

class AIImplementation
{
public:
	AIImplementation(){}
	virtual ~AIImplementation(){}
	
	virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
	virtual void save(GAGCore::OutputStream *stream)=0;
	
	virtual boost::shared_ptr<Order> getOrder(void)=0;
};

#endif

 

