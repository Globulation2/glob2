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

#include "AICastor.h"
#include "Player.h"
#include "Utilities.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 1

AICastor::AICastor(Player *player)
{
	init(player);
}

AICastor::AICastor(SDL_RWops *stream, Player *player, Sint32 versionMinor)
{
	init(player);
	bool goodLoad=load(stream, versionMinor);
	assert(goodLoad);
}

void AICastor::init(Player *player)
{
	assert(player);
	
	// Logical :
	timer=0;

	// Structural:
	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;

	assert(this->team);
	assert(this->game);
	assert(this->map);
}

AICastor::~AICastor()
{
}

bool AICastor::load(SDL_RWops *stream, Sint32 versionMinor)
{
	assert(game);
	if (versionMinor<29)
	{
		//TODO:init
		return true;
	}
	
	Sint32 fileVersionMinor=SDL_ReadBE32(stream);
	if (fileVersionMinor < AI_FILE_MIN_VERSION)
		return false;
	
	return true;
}

void AICastor::save(SDL_RWops *stream)
{
	SDL_WriteBE32(stream, AI_FILE_VERSION);
}


Order *AICastor::getOrder(void)
{
	timer++;
	return new NullOrder();
}
