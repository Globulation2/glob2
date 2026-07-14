// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <Stream.h>

#include "FileFormatVersions.h"
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

Player::Player(GAGCore::InputStream *stream, Team *teams[Team::MAX_COUNT], Sint32 versionMinor)
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

void Player::setBasePlayer(const BasePlayer *initial, Team *teams[Team::MAX_COUNT])
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

bool Player::load(GAGCore::InputStream *stream, Team *teams[Team::MAX_COUNT], Sint32 versionMinor)
{
	stream->readEnterSection("Player");
	char signature[FILE_SIG_LEN];
	stream->read(signature, FILE_SIG_LEN, "signatureStart");
	if (memcmp(signature,FILE_SIG_PLAYER_BEGIN,FILE_SIG_LEN)!=0)
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
	
	stream->read(signature, FILE_SIG_LEN, "signatureEnd");
	if (memcmp(signature,FILE_SIG_PLAYER_END,FILE_SIG_LEN)!=0)
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
	stream->write(FILE_SIG_PLAYER_BEGIN, FILE_SIG_LEN, "signatureStart");
	// base player
	BasePlayer::save(stream);

	// player
	stream->writeSint32(startPositionX, "startPositionX");
	stream->writeSint32(startPositionY, "startPositionY");
	if (type>=P_AI)
		ai->save(stream);
	stream->write(FILE_SIG_PLAYER_END, FILE_SIG_LEN, "signatureEnd");
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
	Uint32 cs=0;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [2+t*20+p*2]
	
	cs^=BasePlayer::checkSum();
	
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [3+t*20+p*2]
	
	return cs;
}
