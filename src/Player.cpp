/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "Player.h"
#include "SDL_net.h"
#include "NetConsts.h"
#include "GlobalContainer.h"

BasePlayer::BasePlayer()
{
	init();
};

BasePlayer::BasePlayer(Sint32 number, const char name[16], Sint32 teamNumber, PlayerType type)

{
	init();
	
	assert(number>=0);
	assert(number<32);
	assert(teamNumber>=0);
	assert(teamNumber<32);
	assert(name);

	setNumber(number);
	setTeamNumber(teamNumber);
	
	memcpy(this->name, name, 16);

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

	netState=PNS_BAD;
	netTimeout=0;
	netTOTL=0;
	destroyNet=true;
	quitting=false;
	quitStep=-1;
}

BasePlayer::~BasePlayer(void)
{
	close();
}

void BasePlayer::close(void)
{
	if (destroyNet)
	{
		unbind();
		if (socket)
		{
			SDLNet_UDP_Close(socket);
			printf("Socket closed to player %d.\n", number);
		}
		socket=NULL;
		channel=-1;
		destroyNet=false;
	}
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

void BasePlayer::load(SDL_RWops *stream)
{
	type=(PlayerType)SDL_ReadBE32(stream);
	number=SDL_ReadBE32(stream);
	numberMask=SDL_ReadBE32(stream);
	SDL_RWread(stream, name, 16, 1);
	teamNumber=SDL_ReadBE32(stream);
	teamNumberMask=SDL_ReadBE32(stream);

	ip.host=SDL_ReadBE32(stream);
	ip.port=SDL_ReadBE32(stream);
}

void BasePlayer::save(SDL_RWops *stream)
{
	SDL_WriteBE32(stream, (Uint32)type);
	SDL_WriteBE32(stream, number);
	SDL_WriteBE32(stream, numberMask);
	SDL_RWwrite(stream, name, 16, 1);
	SDL_WriteBE32(stream, teamNumber);
	SDL_WriteBE32(stream, teamNumberMask);
	
	SDL_WriteBE32(stream, ip.host);
	SDL_WriteBE32(stream, ip.port);
}


Uint8 BasePlayer::getOrderType()
{
	return DATA_BASE_PLAYER;
}

char *BasePlayer::getData()
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
	
	memcpy(data+28, name, 16);
	return data;
}

bool BasePlayer::setData(const char *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;
	
	type=(BasePlayer::PlayerType)getSint32(data, 0);
	number=getSint32(data, 4);
	numberMask=getSint32(data, 8);
	teamNumber=getSint32(data, 12);
	teamNumberMask=getSint32(data, 16);
	Uint32 newHost=(Uint32)SDL_SwapBE32((Uint32)getUint32(data, 20));
	Uint32 newPort=(Uint32)SDL_SwapBE16((Uint16)getUint32(data, 24));
	ip.host=newHost;
	ip.port=newPort;
	
	memcpy(name, data+28, 16);
	
	return true;
}

int BasePlayer::getDataLength()
{
	return 44;
}

Sint32 BasePlayer::checkSum()
{
	Sint32 cs=0;
	
	
	cs^=(type==P_AI)+(type==P_LOST_A)+(type==P_LOST_B);
	cs^=number;
	cs^=numberMask;
	cs^=teamNumber;
	cs^=teamNumberMask;
	Uint32 netHost=SDL_SwapBE32(ip.host);
	Uint32 netPort=(Uint32)SDL_SwapBE16(ip.port);
	cs^=netHost;
	cs^=netPort;

	{
		for (int i=0; i<(int)strlen(name); i++)
			cs^=name[i];
	}
	return cs;
}

void BasePlayer::setip(Uint32 host, Uint16 port)
{
	ip.host=host;
	ip.port=port;
}
void BasePlayer::setip(IPaddress ip)
{
	this->ip=ip;
}
bool BasePlayer::sameip(IPaddress ip)
{
	return ((this->ip.host==ip.host)&&(this->ip.port==ip.port));
}

void BasePlayer::printip(char s[32])
{
	Uint32 netHost=SDL_SwapBE32(ip.host);
	Uint32 netPort=(Uint32)SDL_SwapBE16(ip.port);
	
	int i24=(netHost>>24)&0xFF;
	int i16=(netHost>>16)&0xFF;
	int i8=(netHost>>8)&0xFF;
	int i0=(netHost>>0)&0xFF;
	
	snprintf(s, 32, "%d.%d.%d.%d : %d", i24, i16, i8, i0, netPort);
}

bool BasePlayer::bind()
{
	if (socket==NULL)
	{
		socket=SDLNet_UDP_Open(ANY_PORT);
	
		if (socket!=NULL)
			printf("Socket opened at port to player %d.\n", number);
		else
			printf("failed to open a socket to player %d.\n", number);
	}
	
	if ((socket==NULL) || (ip.host==0))
	{
		printf("no socket, or no ip to bind socket to player %d\n", number);
		return false;
	}
		
	channel=SDLNet_UDP_Bind(socket, -1, &ip);
			
	if (channel != -1)
	{
		printf("suceeded to bind socket to player %d.\n", number);
		return true;			
	}
	else
	{
		printf("failed to bind socket to player %d.\n", number);		
		return false;
	}
}

void BasePlayer::unbind()
{
	if (channel!=-1)
	{
		SDLNet_UDP_Unbind(socket, channel);
		channel=-1;
	}
}

bool BasePlayer::send(char *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size);
	if (packet==NULL)
		return false;
	if (ip.host==0)
		return false;
	packet->len=size;
			
	memcpy((char *)packet->data, data, size);
	
	bool sucess;
	//if (abs(rand()%100)<70)
	sucess=SDLNet_UDP_Send(socket, channel, packet)==1;
	//else
	//	sucess=true; // WARNING : TODO : remove this artificial 30% lost of packets!
	//if (sucess)
	//	printf("suceeded to send packet to player %d.\n", number);
	//else
	//	printf("failed to send packet to player %d. (%x, %d)\n", number, ip.host, ip.port);
			
	SDLNet_FreePacket(packet);
	
	return sucess;
}

bool BasePlayer::send(const int v)
{
	char data[4];
	data[0]=v;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	return send(data, 4);
}

bool BasePlayer::send(const int u, const int v)
{
	char data[8];
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

void BasePlayer::printNetState(char s[32])
{
	switch (netState)
	{
		case PNS_BAD:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_BAD]"));
		}
		break;
		
		case PNS_PLAYER_SILENT:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_PLAYER_SILENT]"));
		}
		break;
		
		case PNS_PLAYER_SEND_ONE_REQUEST:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_PLAYER_SEND_ONE_REQUEST]"));
		}
		break;
		case PNS_SERVER_SEND_SESSION_INFO:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_SERVER_SEND_SESSION_INFO]"));
		}
		break;
		case PNS_PLAYER_SEND_CHECK_SUM:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_PLAYER_SEND_CHECK_SUM]"));
		}
		break;
		
		case PNS_OK:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_OK]"));
		}
		break;
		
		case PNS_SERVER_SEND_CROSS_CONNECTION_START:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_SERVER_SEND_CROSS_CONNECTION_START]"));
		}
		break;
		case PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START]"));
		}
		break;
		case PNS_PLAYER_FINISHED_CROSS_CONNECTION:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_PLAYER_FINISHED_CROSS_CONNECTION]"));
		}
		break;
		
		case PNS_CROSS_CONNECTED:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_CROSS_CONNECTED]"));
		}
		break;
	
		case PNS_SERVER_SEND_START_GAME:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_CROSS_CONNECTED]"));
		}
		break;
		case PNS_PLAYER_CONFIRMED_START_GAME:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_CROSS_CONNECTED]"));
		}
		break;
		case PNS_PLAYER_PLAYS:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_CROSS_CONNECTED]"));
		}
		break;
		
		case PNS_BINDED:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_BINDED]"));
		}
		break;
		case PNS_SENDING_FIRST_PACKET:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_SENDING_FIRST_PACKET]"));
		}
		break;
		case PNS_HOST:
		{
			snprintf(s, 32, globalContainer->texts.getString("[PNS_HOST]"));
		}
		break;
		default:
		{
			snprintf(s, 32, "error %d", netState);
		}
	}
}

Player::Player()
:BasePlayer()
{
	startPositionX=0;
	startPositionY=0;
	team=NULL;
	ai=NULL;
}

Player::Player(SDL_RWops *stream, Team *teams[32])
:BasePlayer()
{
	load(stream, teams);
}

Player::Player(Sint32 number, const char name[16], Team *team, PlayerType type)
:BasePlayer(number, name, team->teamNumber, type)
{
	if (type==P_AI)
		ai=new AI(this);
	else
		ai=NULL;
	this->team=team;
}

Player::~Player()
{
	if (ai)
		delete ai;
}

void Player::setBasePlayer(const BasePlayer *initial, Team *teams[32])
{
	assert(initial);

	number=initial->number;
	numberMask=initial->numberMask;
	teamNumber=initial->teamNumber;
	teamNumberMask=initial->teamNumberMask;
	memcpy(this->name, initial->name, 16 );

	type=initial->type;
	team=teams[this->teamNumber];

	if (type==P_AI)
		ai=new AI(this);

	ip=initial->ip;
	socket=initial->socket;
	channel=initial->channel;
};

void Player::load(SDL_RWops *stream, Team *teams[32])
{
	// if AI, delete
	if ((type==P_AI) && (ai))
		delete ai;

	// base player
	BasePlayer::load(stream);

	// player
	startPositionX=SDL_ReadBE32(stream);
	startPositionY=SDL_ReadBE32(stream);
	team=teams[teamNumber];
	if (type==P_AI)
		ai=new AI(stream, this);
	else
		ai=NULL;
}

void Player::save(SDL_RWops *stream)
{
	// base player
	BasePlayer::save(stream);

	// player
	SDL_WriteBE32(stream, startPositionX);
	SDL_WriteBE32(stream, startPositionY);
	if (type==P_AI)
		ai->save(stream);
}

Sint32 Player::checkSum()
{

	return BasePlayer::checkSum();
}
