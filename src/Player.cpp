/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include <StringTable.h>
#include <Stream.h>

#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Marshaling.h"
#include "NetConsts.h"
#include "Player.h"
#include "Team.h"
#include "Utilities.h"

BasePlayer::BasePlayer()
{
	init();
};

BasePlayer::BasePlayer(Sint32 number, const std::string& nname, Sint32 teamNumber, PlayerType type)
{
	init();
	
	assert(number>=0);
	assert(number<32);
	assert(teamNumber>=0);
	assert(teamNumber<32);
	assert(nname.size());

	setNumber(number);
	setTeamNumber(teamNumber);

	name=nname;

	this->type=type;
};

void BasePlayer::init()
{
	type=P_NONE;
	number=0;
	numberMask=0;
	name = "Debug Player";
	teamNumber=0;
	teamNumberMask=0;
	playerID=0;

	quitting=false;
	quitUStep=0;
	lastUStepToExecute=0;
	
	disableRecursiveDestruction=false;
	
	logFile=globalContainer->logFileManager->getFile("Player.log");
	assert(logFile);
}

BasePlayer::~BasePlayer(void)
{
}

void BasePlayer::setNumber(Sint32 number)
{
	this->number=number;
	this->numberMask=1<<number;
};

void BasePlayer::setTeamNumber(Sint32 teamNumber)
{
	this->teamNumber=teamNumber;
	this->teamNumberMask=1<<teamNumber;
};

bool BasePlayer::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	fprintf(logFile, "versionMinor=%d.\n", versionMinor);
	stream->readEnterSection("BasePlayer");
	type = (PlayerType)stream->readUint32("type");
	number = stream->readSint32("number");
	numberMask = stream->readUint32("numberMask");
	playerID = stream->readUint16("playerID");
	name = stream->readText("name");
	teamNumber = stream->readSint32("teamNumber");
	teamNumberMask = stream->readUint32("teamNumberMask");
	stream->readLeaveSection();
	return true;
}

void BasePlayer::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("BasePlayer");
	stream->writeUint32((Uint32)type, "type");
	stream->writeSint32(number, "number");
	stream->writeUint32(numberMask, "numberMask");
	stream->writeUint16(playerID, "playerID");
	stream->writeText(name, "name");
	stream->writeSint32(teamNumber, "teamNumber");
	stream->writeUint32(teamNumberMask, "teamNumberMask");
	stream->writeLeaveSection();
}


Uint32 BasePlayer::checkSum()
{
	Uint32 cs=0;

	cs^=number;
	cs^=numberMask;
	cs^=teamNumber;
	cs^=teamNumberMask;
	//Uint32 netHost=SDL_SwapBE32(ip.host);
	//Uint32 netPort=(Uint32)SDL_SwapBE16(ip.port);
	//cs^=netHost;
	// IP adress can't stay in checksum, because:
	// We now support NAT or IP may simply be differents between computers
	// And we uses checkSum in network.
	// (we could uses two differents check sums, but the framework would be heavier)
	//cs^=netPort;

	for (int i=0; i<name.size(); i++)
		cs^=name[i];
	
	return cs;
}


void BasePlayer::makeItAI(AI::ImplementitionID aiType)
{
	type=(PlayerType)(P_AI+aiType);
}


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
	if (success)
	{
		fprintf(logFile, "!success\n");
		fflush(logFile);
	}
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

	type=initial->type;
	setTeam(teams[this->teamNumber]);

	if (type>=P_AI)
	{
		ai=new AI((AI::ImplementitionID)(type-P_AI), this);
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
		fprintf(logFile, "Player::load: Signature missmatch at begin of Player\n");
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
		fprintf(logFile, "Player::load: Error during BasePlayer load\n");
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
			fprintf(logFile, "Player::load: Error during AI load\n");
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
		fprintf(logFile, "Player::load: Signature missmatch at end of Player\n");
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
