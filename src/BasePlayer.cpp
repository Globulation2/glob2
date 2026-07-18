// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BasePlayer.h"

#include "Stream.h"

BasePlayer::BasePlayer()
{
	init();
};

BasePlayer::BasePlayer(Sint32 number, const std::string& nname, Sint32 teamNumber, PlayerType type)
{
	init();
	
	assert(number>=0);
	assert(number<Team::MAX_COUNT);
	assert(teamNumber>=0);
	assert(teamNumber<Team::MAX_COUNT);
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
}

BasePlayer::~BasePlayer(void)
{
}

void BasePlayer::setNumber(Sint32 number)
{
	this->number=number;
	// Player-slot bit, not a team bit — Uint32(1) avoids signed-shift UB at slot 31.
	this->numberMask=Uint32(1)<<number;
};

void BasePlayer::setTeamNumber(Sint32 teamNumber)
{
	this->teamNumber=teamNumber;
	this->teamNumberMask=Team::teamNumberToMask(teamNumber);
};

// Wire/storage validation point: reads a BasePlayer from a .game, .replay,
// save file, or network packet. `number` and `teamNumber` are bounds-checked
// against Team::MAX_COUNT here so every downstream consumer
// (Game::setGameHeader, Player::setBasePlayer, ...) can safely index
// teams[teamNumber] and players[number] without re-validating, and `type` is
// range-checked before the enum cast so `type >= P_AI` branches can safely
// derive an AI::ImplementitionID from it. Returns false on bad input; the
// outer GameHeader/Player load propagates the failure.
bool BasePlayer::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	// An invalid or already-exhausted stream (truncated save, short packet)
	// would make every read below return garbage or zero-fill. Checking EOS
	// *after* the reads would be unsound: MemoryStreamBackend reports EOS
	// when a valid record ends exactly at the buffer end, and does not
	// advance past a failed overread. So guard at entry and rely on the
	// per-field range checks below to catch mid-record corruption.
	if (!stream->isValid() || stream->isEndOfStream())
	{
		fprintf(stderr, "BasePlayer::load: stream invalid or exhausted before BasePlayer record\n");
		return false;
	}
	stream->readEnterSection("BasePlayer");
	const Uint32 rawType = stream->readUint32("type");
	number = stream->readSint32("number");
	numberMask = stream->readUint32("numberMask");
	// The field is Uint32 (YOG identifier); pre-86 saves truncated it to Uint16.
	if (versionMinor >= 86)
		playerID = stream->readUint32("playerID");
	else
		playerID = stream->readUint16("playerID");
	name = stream->readText("name");
	teamNumber = stream->readSint32("teamNumber");
	teamNumberMask = stream->readUint32("teamNumberMask");
	stream->readLeaveSection();
	if (!isValidSerializedType(rawType))
	{
		fprintf(stderr, "BasePlayer::load: invalid player type %u (must be below %u)\n",
			(unsigned)rawType, (unsigned)(P_AI + AI::SIZE));
		return false;
	}
	type = (PlayerType)rawType;
	if (number < 0 || number >= Team::MAX_COUNT)
	{
		fprintf(stderr, "BasePlayer::load: out-of-range player number %d (must be in [0, %d))\n",
			(int)number, Team::MAX_COUNT);
		return false;
	}
	if (teamNumber < 0 || teamNumber >= Team::MAX_COUNT)
	{
		fprintf(stderr, "BasePlayer::load: out-of-range teamNumber %d (must be in [0, %d))\n",
			(int)teamNumber, Team::MAX_COUNT);
		return false;
	}
	return true;
}

void BasePlayer::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("BasePlayer");
	stream->writeUint32((Uint32)type, "type");
	stream->writeSint32(number, "number");
	stream->writeUint32(numberMask, "numberMask");
	stream->writeUint32(playerID, "playerID");
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
	// IP address can't stay in checksum, because:
	// We now support NAT or IP may simply be different between computers
	// And we use checkSum in network.
	// (we could use two different check sums, but the framework would be heavier)
	//cs^=netPort;

	for (unsigned i=0; i<name.size(); i++)
		cs^=name[i];
	
	return cs;
}


void BasePlayer::makeItAI(AI::ImplementitionID aiType)
{
	type=playerTypeFromImplementitionID(aiType);
}

