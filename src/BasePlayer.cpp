/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "BasePlayer.h"

#include "GlobalContainer.h"
#include "Stream.h"
#include "LogFileManager.h"

BasePlayer::BasePlayer()
{
	init();
};

BasePlayer::BasePlayer(Sint32 number, const std::string& name, Sint32 teamNumber, PlayerType type)
{
	init();
	
	assert(number>=0);
	assert(number<Team::MAX_COUNT);
	assert(teamNumber>=0);
	assert(teamNumber<Team::MAX_COUNT);
	assert(name.size());

	setNumber(number);
	setTeamNumber(teamNumber);

	this->name=name;

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
	this->numberMask=1<<number;
};

void BasePlayer::setTeamNumber(Sint32 teamNumber)
{
	this->teamNumber=teamNumber;
	this->teamNumberMask=1<<teamNumber;
};

bool BasePlayer::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
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
	// IP address can't stay in checksum, because:
	// We now support NAT or IP may simply be different between computers
	// And we uses checkSum in network.
	// (we could uses two different check sums, but the framework would be heavier)
	//cs^=netPort;

	for (unsigned i=0; i<name.size(); i++)
		cs^=name[i];
	
	return cs;
}


void BasePlayer::makeItAI(AI::ImplementationID aiType)
{
	type=(PlayerType)(P_AI+aiType);
}

