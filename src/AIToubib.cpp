/*
  This file is part of Globulation 2, a free software real-time strategy game
  http://www.globulation2.org
  Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charriere and other contributors
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

#include <Stream.h>

#include "AIToubib.h"
#include "Order.h"
#include "Player.h"

using namespace boost;

AIToubib::AIToubib(Player *player)
{
	init(player);
}

AIToubib::AIToubib(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
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

	now = 0;

	/*currentStateIndex = NB_HISTORY_STATES;
	memset(history, 0, sizeof(AIState) * NB_HISTORY_STATES);

	//pq = new std::priority_queue(std::list<AIProject>);


	*/
}

bool AIToubib::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	// check version
	// saving state variables
	now = stream->readUint32("now");

	return true;
}

void AIToubib::save(GAGCore::OutputStream *stream)
{
	// loading state variables
	stream->writeUint32(now, "now");
}

boost::shared_ptr<Order> AIToubib::getOrderBuildingStep(void)
{
	return shared_ptr<Order>(new NullOrder());
}

void AIToubib::computeMyStatsStep(void)
{

}

boost::shared_ptr<Order> AIToubib::getOrder(void)
{
	now++;

	switch (now % 2)
	{
		case 0: return getOrderBuildingStep();
		default: computeMyStatsStep(); return shared_ptr<Order>(new NullOrder());
	}
}
