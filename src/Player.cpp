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

BasePlayer::BasePlayer(Sint32 number, const char name[MAX_NAME_LENGTH], Sint32 teamNumber, PlayerType type)
{
	init();
	
	assert(number>=0);
	assert(number<32);
	assert(teamNumber>=0);
	assert(teamNumber<32);
	assert(name);

	setNumber(number);
	setTeamNumber(teamNumber);

	memset(this->name, 0, MAX_NAME_LENGTH);
	strncpy(this->name, name, MAX_NAME_LENGTH);
	this->name[MAX_NAME_LENGTH-1] = 0;

	this->type=type;
};

void BasePlayer::init()
{
	type=P_IP;
	number=0;
	numberMask=0;
	strncpy(name, "DEBUG PLAYER", MAX_NAME_LENGTH);
	teamNumber=0;
	teamNumberMask=0;

	ip.host=0;
	ip.port=0;
	socket=NULL;
	channel=-1;
	ipFromNAT=false;
	ipFirewallClean=false;
	waitForNatResolution=false;

	netState=PNS_BAD;
	netTimeout=0;
	netTOTL=0;
	destroyNet=true;
	quitting=false;
	quitUStep=0;
	lastUStepToExecute=0;
	
	disableRecursiveDestruction=false;
	
	logFile=globalContainer->logFileManager->getFile("Player.log");
	assert(logFile);
}

BasePlayer::~BasePlayer(void)
{
	if (!disableRecursiveDestruction && destroyNet)
		unbind();
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
	// Compatibility
	if (versionMinor<25)
	{
		if (type==3)
			type=(PlayerType)6;
		else if (type==4)
			type=(PlayerType)3;
		else if (type==5)
			type=(PlayerType)4;
		else
			assert((type>=0) && (type<=2));
	}
	number = stream->readUint32("number");
	numberMask = stream->readUint32("numberMask");
	stream->read(name, MAX_NAME_LENGTH, "name");
	teamNumber = stream->readSint32("teamNumber");
	teamNumberMask = stream->readUint32("teamNumberMask");
	ip.host = stream->readUint32("ip.host");
	ip.port = stream->readUint32("ip.port");
	stream->readLeaveSection();
	return true;
}

void BasePlayer::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BasePlayer");
	stream->writeUint32((Uint32)type, "type");
	stream->writeSint32(number, "number");
	stream->writeUint32(numberMask, "numberMask");
	stream->write(name, MAX_NAME_LENGTH, "name");
	stream->writeSint32(teamNumber, "teamNumber");
	stream->writeUint32(teamNumberMask, "teamNumberMask");
	stream->writeUint32(ip.host, "ip.host");
	stream->writeUint32(ip.port, "ip.port");
	stream->writeLeaveSection();
}


Uint8 BasePlayer::getOrderType()
{
	return DATA_BASE_PLAYER;
}

Uint8 *BasePlayer::getData(bool compressed)
{
	if (compressed)
	{
		// we start with alignement-needed variables:
		Uint32 netHost=SDL_SwapBE32((Uint32)ip.host);
		Uint32 netPort=(Uint32)SDL_SwapBE16(ip.port);
		addSint32(data, netHost, 0);
		addSint32(data, netPort, 4);
		
		addSint8(data, (Sint8)type, 8);
		addSint8(data, (Sint8)number, 9);
		assert(numberMask==(Uint32)(1<<number));
		addSint8(data, (Sint8)teamNumber, 10);
		assert(teamNumberMask==(Uint32)(1<<teamNumber));
		
		addSint8(data, (Sint8)ipFromNAT, 11);

		memcpy(data+12, name, MAX_NAME_LENGTH);//TODO: compress player's names too!
	}
	else
	{
		addSint32(data, (Sint32)type, 0);
		addSint32(data, number, 4);
		addSint32(data, numberMask, 8);
		addSint32(data, teamNumber, 12);
		addSint32(data, teamNumberMask, 16);
		Uint32 netHost=SDL_SwapBE32((Uint32)ip.host);
		Uint32 netPort=(Uint32)SDL_SwapBE16(ip.port);
		addUint32(data, netHost, 20);
		addUint32(data, netPort, 24);
		addSint32(data, (Sint32)ipFromNAT, 28);

		memcpy(data+32, name, MAX_NAME_LENGTH);
	}
	return data;
}

bool BasePlayer::setData(const Uint8 *data, int dataLength, bool compressed)
{
	if (dataLength!=getDataLength(compressed))
		return false;
	
	if (compressed)
	{
		Uint32 newHost=(Uint32)SDL_SwapBE32((Uint32)getUint32(data, 0));
		Uint32 newPort=(Uint32)SDL_SwapBE16((Uint16)getUint32(data, 4));
		ip.host=newHost;
		ip.port=newPort;
		
		type=(BasePlayer::PlayerType)getSint8(data, 8);
		number=(Sint32)getSint8(data, 9);
		numberMask=1<<number;
		teamNumber=(Sint32)getSint8(data, 10);
		teamNumberMask=1<<teamNumber;
		
		ipFromNAT=(bool)getSint8(data, 11);

		memcpy(name, data+12, MAX_NAME_LENGTH);
	}
	else
	{
		type=(BasePlayer::PlayerType)getSint32(data, 0);
		number=getSint32(data, 4);
		numberMask=getSint32(data, 8);
		teamNumber=getSint32(data, 12);
		teamNumberMask=getSint32(data, 16);
		Uint32 newHost=(Uint32)SDL_SwapBE32((Uint32)getUint32(data, 20));
		Uint32 newPort=(Uint32)SDL_SwapBE16((Uint16)getUint32(data, 24));
		ip.host=newHost;
		ip.port=newPort;
		ipFromNAT=(bool)getSint32(data, 28);

		memcpy(name, data+32, MAX_NAME_LENGTH);
	}
	
	return true;
}

int BasePlayer::getDataLength(bool compressed)
{
	if (compressed)
		return 12+MAX_NAME_LENGTH;
	else
		return 32+MAX_NAME_LENGTH;
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

	int l=Utilities::strnlen(name, 32);
	for (int i=0; i<l; i++)
		cs^=name[i];
	
	return cs;
}

void BasePlayer::setip(Uint32 host, Uint16 port)
{
	if (ip.host!=host || ip.port!=port)
	{
		unbind();
		ip.host=host;
		ip.port=port;
	}
}
void BasePlayer::setip(IPaddress ip)
{
	if (ip.host!=this->ip.host || ip.port!=this->ip.port)
	{
		unbind();
		this->ip=ip;
	}
}
bool BasePlayer::sameip(IPaddress ip)
{
	return ((this->ip.host==ip.host)&&(this->ip.port==ip.port));
}
bool BasePlayer::localhostip()
{
	return (ip.host==SDL_SwapBE32(0x7F000001));
}

bool BasePlayer::bind(UDPsocket socket)
{
	this->socket=socket;
	
	assert(socket);
	
	if ((socket==NULL) || (ip.host==0))
	{
		fprintf(logFile, "no socket, or no ip to bind socket to player %d\n", number);
		return false;
	}

	assert(channel==-1);
	channel=SDLNet_UDP_Bind(socket, -1, &ip);
	if (channel != -1)
	{
		fprintf(logFile, "suceeded to bind socket to player %d, socket=(%p), channel=(%d), ip=(%s).\n", number, socket, channel, Utilities::stringIP(ip));
		return true;
	}
	else
	{
		fprintf(logFile, "failed to bind socket to player %d.\n", number);
		return false;
	}
}

void BasePlayer::unbind()
{
	if (channel!=-1)
	{
		fprintf(logFile, "Unbinding player %d (socket=%p)(channel=%d).\n", number, socket, channel);
		assert(socket);
		SDLNet_UDP_Unbind(socket, channel);
		channel=-1;
		ip.host=0;
		ip.port=0;
	}
}

bool BasePlayer::send(Uint8 *data, int size)
{
	if (ip.host==0)
		return false;
	if (socket==NULL)
		return false;
	UDPpacket *packet=SDLNet_AllocPacket(size);
	assert(packet);
	if (packet==NULL)
		return false;
	packet->len=size;
	memcpy(packet->data, data, size);
	bool success;
	packet->address=ip;
	packet->channel=-1;
	//if (abs(rand()%100)<90)
		success=SDLNet_UDP_Send(socket, -1, packet)==1;
	//else
	//	success=true; // WARNING : TODO : remove this artificial lost of packets!
	if (!success)
		fprintf(logFile, "failed to send packet!\n");
	SDLNet_FreePacket(packet);
	return success;
}

bool BasePlayer::send(Uint8 *data, int size, const Uint8 v)
{
	UDPpacket *packet=SDLNet_AllocPacket(size+4);
	if (packet==NULL)
		return false;
	if (ip.host==0)
		return false;
	packet->len=size;
	memcpy(4+packet->data, data, size);
	packet->data[0]=v;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	bool success;
	packet->address=ip;
	packet->channel=channel;
	success=SDLNet_UDP_Send(socket, channel, packet)==1;
	if (!success)
		fprintf(logFile, "failed to send packet!\n");
	SDLNet_FreePacket(packet);
	return success;
}

bool BasePlayer::send(const Uint8 v)
{
	Uint8 data[4];
	data[0]=v;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	return send(data, 4);
}

bool BasePlayer::send(const Uint8 u, const Uint8 v)
{
	Uint8 data[8];
	data[0]=u;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	data[4]=v;
	data[5]=0;
	data[6]=0;
	data[7]=0;
	return send(data, 8);
}

bool BasePlayer::send(const Uint8 u, const Uint8 v, const Uint32 checksum)
{
	Uint8 data[12];
	data[0]=u;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	data[4]=v;
	data[5]=0;
	data[6]=0;
	data[7]=0;
	*((Uint32 *)(data+8))=SDL_SwapBE32(checksum);
	return send(data, 12);
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

Player::Player(Sint32 number, const char name[MAX_NAME_LENGTH], Team *team, PlayerType type)
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
	memcpy(this->name, initial->name, MAX_NAME_LENGTH);

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

	ip=initial->ip;
	socket=initial->socket;
	channel=initial->channel;
};

void Player::makeItAI(AI::ImplementitionID aiType)
{
	if (ai)
	{
		assert(type>=P_AI);
		delete ai;
		ai=NULL;
	}
	type=(PlayerType)(P_AI+aiType);
	ai=new AI(aiType, this);
	assert(ai);
}

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
