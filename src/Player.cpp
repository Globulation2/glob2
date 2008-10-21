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

#include <StringTable.h>
#include <Stream.h>

#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Marshaling.h"
#include "NetConsts.h"
#include "Player.h"
#include "Team.h"
#include "Utilities.h"

Player::Player()
:BasePlayer()
{
	startPositionX=0;
	startPositionY=0;
	setTeam(NULL);
	ai=NULL;
}

Player::Player(GAGCore::InputStream *stream, Team *teams[32], Sint32 versionMinor)
:BasePlayer()
{
	bool success=load(stream, teams, versionMinor);
	assert(success);
}

Player::Player(Sint32 number, const std::string& name, Team *team, PlayerType type)
:BasePlayer(number, name, team->teamNumber, type)
{
	setTeam(team);
	if (type>=P_AI)
	{
		ai=new AI(implementitionIdFromPlayerType(type), this);
	}
	else
	{
		ai=NULL;
		team->type=BaseTeam::T_HUMAN;
	}
	startPositionX = startPositionY = 0;
}

Player::~Player()
{
	if (!disableRecursiveDestruction)
		if (ai)
		{
			assert(type>=P_AI);
			delete ai;
		}
}

void Player::setTeam(Team *team)
{
	if (team)
	{
		this->team=team;
		this->game=team->game;
		this->map=team->map;
	}
	else
	{
		this->team=NULL;
		this->game=NULL;
		this->map=NULL;
	}
}

void Player::setBasePlayer(const BasePlayer *initial, Team *teams[32])
{
	assert(initial);

	number=initial->number;
	numberMask=initial->numberMask;
	teamNumber=initial->teamNumber;
	teamNumberMask=initial->teamNumberMask;
	name = initial->name;
	playerID = initial->playerID;

	type=initial->type;
	setTeam(teams[this->teamNumber]);

	if (type>=P_AI)
	{
		ai=new AI((AI::ImplementitionID)(type-P_AI), this);
	}
	else if(type==P_NONE)
	{
		ai=NULL;
	}
	else
	{
		ai=NULL;
		team->type=BaseTeam::T_HUMAN;
	}
};

bool Player::load(GAGCore::InputStream *stream, Team *teams[32], Sint32 versionMinor)
{
	stream->readEnterSection("Player");
	char signature[4];
	stream->read(signature, 4, "signatureStart");
	if (memcmp(signature,"PLYb",4)!=0)
	{
		fprintf(stderr, "Player::load: Signature missmatch at begin of Player\n");
		stream->readLeaveSection();
		return false;
	}
	
	// if AI, delete
	if (type>=P_AI)
	{
		assert(ai);
		delete ai;
		ai = NULL;
	}

	// base player
	bool success = BasePlayer::load(stream, versionMinor);
	if (!success)
	{
		fprintf(stderr, "Player::load: Error during BasePlayer load\n");
		stream->readLeaveSection();
		return false;
	}

	// player
	startPositionX = stream->readSint32("startPositionX");
	startPositionY = stream->readSint32("startPositionY");
	setTeam(teams[teamNumber]);
	if (type >= P_AI)
	{
		ai = new AI(AI::NONE, this);
		if (!ai->load(stream, versionMinor))
		{
			fprintf(stderr, "Player::load: Error during AI load\n");
			stream->readLeaveSection();
			return false;
		}
	}
	else
	{
		ai = NULL;
		team->type = BaseTeam::T_HUMAN;
	}
	
	stream->read(signature, 4, "signatureEnd");
	if (memcmp(signature,"PLYe",4)!=0)
	{
		fprintf(stderr, "Player::load: Signature missmatch at end of Player\n");
		stream->readLeaveSection();
		return false;
	}
	
	stream->readLeaveSection();
	return true;
}

void Player::save(GAGCore::OutputStream  *stream)
{
	stream->writeEnterSection("Player");
	stream->write("PLYb", 4, "signatureStart");
	// base player
	BasePlayer::save(stream);

	// player
	stream->writeSint32(startPositionX, "startPositionX");
	stream->writeSint32(startPositionY, "startPositionY");
	if (type>=P_AI)
		ai->save(stream);
	stream->write("PLYe", 4, "signatureEnd");
	stream->writeLeaveSection();
}



void Player::makeItAI(AI::ImplementitionID aiType)
{
	BasePlayer::makeItAI(aiType);
	if(ai)
		delete ai;
	ai=new AI(aiType, this);
}



Uint32 Player::checkSum(std::vector<Uint32> *checkSumsVector)
{
	Uint32 cs;
	if (ai)
		cs=ai->step;
	else
		cs=0;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [2+t*20+p*2]
	
	cs^=BasePlayer::checkSum();
	
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [3+t*20+p*2]
	
	return cs;
}
