// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

/*
What's in AI ?
AI represents the behaviour of an artificial intelligence player.
The main method is std::shared_ptr<Order> getOrder() which return the order to be used by the AI's team.
*/

#include "BuildingType.h"
#include <memory>

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
	
	virtual std::shared_ptr<Order> getOrder(void)=0;
};


 

