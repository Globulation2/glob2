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

#include "AI.h"
#include "Player.h"
#include "Utilities.h"
#include "Game.h"
#include "Order.h"
#include <assert.h>

#include "AINull.h"
#include "AINumbi.h"
#include "AICastor.h"

/*AI::AI(Player *player)
{
	aiImplementation=new AICastor(player);
	this->implementitionID=NUMBI;
	this->player=player;
}*/

AI::AI(ImplementitionID implementitionID, Player *player)
{
	aiImplementation=NULL;
	
	switch (implementitionID)
	{
		case NONE:
			aiImplementation=new AINull();
		break;
		case NUMBI:
			aiImplementation=new AINumbi(player);
		break;
		case CASTOR:
			aiImplementation=new AICastor(player);
		break;
		default:
			assert(false);
		break;
	}
	
	this->implementitionID=implementitionID;
	this->player=player;
}

AI::AI(SDL_RWops *stream, Player *player)
{
	aiImplementation=NULL;
	implementitionID=NONE;
	this->player=player;
	load(stream);
}

AI::~AI()
{
	if (aiImplementation)
		delete aiImplementation;
	aiImplementation=NULL;
}

Order *AI::getOrder(bool paused)
{
	assert(player);
	if(paused || !player->team->isAlive)
		return new NullOrder();
	assert(aiImplementation);
	return aiImplementation->getOrder();
}

bool AI::load(SDL_RWops *stream)
{
	assert(player);
	
	if (aiImplementation)
		delete aiImplementation;
	aiImplementation=NULL;

	char signature[4];
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"AI b",4)!=0)
		return false;

	implementitionID=(ImplementitionID)SDL_ReadBE32(stream);

	switch (implementitionID)
	{
		case NONE:
			aiImplementation=new AINull();
		break;
		case NUMBI:
			aiImplementation=new AINumbi(stream, player);
		break;
		case CASTOR:
			aiImplementation=new AICastor(stream, player);
		break;
		default:
			assert(false);
		break;
	}

	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"AI e",4)!=0)
		return false;
	
	return true;
}

void AI::save(SDL_RWops *stream)
{
	SDL_RWwrite(stream, "AI b", 4, 1);
	
	SDL_WriteBE32(stream, (Uint32)implementitionID);
	
	assert(aiImplementation);
	aiImplementation->save(stream);
	
	SDL_RWwrite(stream, "AI e", 4, 1);
}
