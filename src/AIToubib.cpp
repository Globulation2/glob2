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

#include "AIToubib.h"
#include "Order.h"
#include "Player.h"

AIToubib::AIToubib(Player *player)
{
	init(player);
}

AIToubib::AIToubib(SDL_RWops *stream, Player *player, Sint32 versionMinor)
{
	init(player);
	
	bool goodLoad = load(stream, player, versionMinor);
	assert(goodLoad);
}

AIToubib::~AIToubib()
{

}

void AIToubib::init(Player *player)
{
	assert(player);
	
	this->player = player;
	this->team = player->team;
	this->game = player->game;
	this->map = player->map;

	assert(this->team);
	assert(this->game);
	assert(this->map);
}

bool AIToubib::load(SDL_RWops *stream, Player *player, Sint32 versionMinor)
{
	if (versionMinor<=32)
	{
		fprintf(stderr, "AINumbi::load : trying to load too old AINumbi (versionMinor < 33)\n");
		assert(false);
	}
	
	return true;
}

void AIToubib::save(SDL_RWops *stream)
{

}

Order *AIToubib::getOrder(void)
{
	return new NullOrder();
}
