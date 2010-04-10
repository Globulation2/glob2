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

#include "AI.h"
#include "Player.h"
#include "Utilities.h"
#include "Game.h"
#include "Order.h"
#include <assert.h>
#include <Stream.h>

#include "StringTable.h"

#include "AINull.h"
#include "AINumbi.h"
#include "AICastor.h"
#include "AIToubib.h"
#include "AIWarrush.h"
#include "AINicowar.h"
#include "AIEcho.h"

using namespace boost;

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
		case NICOWAR:
			aiImplementation=new AIEcho::Echo(new NewNicowar, player);
		break;
		case WARRUSH:
			aiImplementation=new AIWarrush(player);
		break;
		case REACHTOINFINITY:
			aiImplementation=new AIEcho::Echo(new AIEcho::ReachToInfinity, player);
		break;
		case TOUBIB:
			aiImplementation=new AIToubib(player);
		break;
		default:
			assert(false);
		break;
	}
	
	this->implementitionID=implementitionID;
	this->player=player;
}

AI::AI(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	aiImplementation=NULL;
	implementitionID=NONE;
	this->player=player;
	bool goodLoad=load(stream, versionMinor);
	assert(goodLoad);
}

AI::~AI()
{
	if (aiImplementation)
		delete aiImplementation;
	aiImplementation=NULL;
}

boost::shared_ptr<Order> AI::getOrder(bool paused)
{
	assert(player);
	if (paused || !player->team->isAlive)
		return shared_ptr<Order>(new NullOrder());
	assert(aiImplementation);
	return aiImplementation->getOrder();
}

bool AI::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	assert(player);
	
	if (aiImplementation)
		delete aiImplementation;
	aiImplementation=NULL;

	char signature[4];
	
	stream->readEnterSection("AI");
	stream->read(signature, 4, "signatureStart");
	if (memcmp(signature,"AI b", 4)!=0)
	{
		fprintf(stderr, "AI::bad begining signature\n");
		stream->readLeaveSection();
		return false;
	}

	implementitionID=(ImplementitionID)stream->readUint32("implementitionID");

	switch (implementitionID)
	{
		case NONE:
			aiImplementation=new AINull();
		break;
		case NUMBI:
			aiImplementation=new AINumbi(stream, player, versionMinor);
		break;
		case CASTOR:
			aiImplementation=new AICastor(stream, player, versionMinor);
		break;
		case NICOWAR:
			aiImplementation=new AIEcho::Echo(new NewNicowar, player);
			aiImplementation->load(stream, player, versionMinor);
		break;
		case REACHTOINFINITY:
			aiImplementation=new AIEcho::Echo(new AIEcho::ReachToInfinity, player);
			aiImplementation->load(stream, player, versionMinor);
		break;
		case TOUBIB:
			aiImplementation=new AIToubib(stream, player, versionMinor);
		break;
		case WARRUSH:
			aiImplementation=new AIWarrush(stream, player, versionMinor);
		break;
		default:
			fprintf(stderr, "AI id %d does not exist, you probably try to load a map from a more recent version of glob2.\n", implementitionID);
			assert(false);
		break;
	}

	stream->read(signature, 4, "signatureEnd");
	stream->readLeaveSection();
	if (memcmp(signature,"AI e", 4)!=0)
	{
		fprintf(stderr, "AI::bad end signature\n");
		return false;
	}
	
	return true;
}

void AI::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AI");
	stream->write("AI b", 4, "signatureStart");
	
	stream->writeUint32(static_cast<Uint32>(implementitionID), "implementitionID");
	
	assert(aiImplementation);
	aiImplementation->save(stream);
	
	stream->write( "AI e",  4, "signatureEnd");
	stream->writeLeaveSection();
}
