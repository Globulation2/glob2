// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charriere and other contributors

#include <Stream.h>

#include "AIToubib.h"
#include "Order.h"
#include "Player.h"

using std::shared_ptr;

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

std::shared_ptr<Order> AIToubib::getOrderBuildingStep(void)
{
	return shared_ptr<Order>(new NullOrder());
}

void AIToubib::computeMyStatsStep(void)
{

}

std::shared_ptr<Order> AIToubib::getOrder(void)
{
	now++;
	
	switch (now % 2)
	{
		case 0: return getOrderBuildingStep();
		default: computeMyStatsStep(); return shared_ptr<Order>(new NullOrder());
	}
}
