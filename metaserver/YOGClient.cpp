/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#include "YOGClient.h"
#include "../src/Marshaling.h"

extern FILE *logServer;
extern YOGClient *admin;

YOGClient::YOGClient(IPaddress ip, UDPsocket socket, char userName[32])
{
	this->ip=ip;
	gameip.host=0;
	gameip.port=0;
	this->socket=socket;
	memcpy(this->userName, userName, 32);
	
	lastSentMessageID=0;
	lastMessageID=0;
	messageTimeout=0;
	messageTOTL=3;
	
	sharingGame=NULL;
	
	timeout=0;
	TOTL=3;
	
	gamesTimeout=0;
	gamesTOTL=3;
	unsharedTimeout=0;
	unsharedTOTL=3;
	
	static Uint32 clientUID=1;
	uid=clientUID++;
	
	leftClientPacketID=0;
	for (int i=0; i<4; i++)
		lastLeftClientNumber[i]=0;
	allreadyRemovedClients=0;
}

YOGClient::~YOGClient()
{
}

void YOGClient::send(YOGMessageType v)
{
	Uint8 data[4];
	data[0]=v;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	UDPpacket *packet=SDLNet_AllocPacket(4);
	if (packet==NULL)
		lprintf("Failed to allocate packet!\n");

	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		lprintf("Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOGClient::send(YOGMessageType v, Uint8 id)
{
	Uint8 data[4];
	data[0]=v;
	data[1]=id;
	data[2]=0;
	data[3]=0;
	UDPpacket *packet=SDLNet_AllocPacket(4);
	if (packet==NULL)
		lprintf("Failed to allocate packet!\n");

	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		lprintf("Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOGClient::send(Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size);
	if (packet==NULL)
		lprintf("Failed to allocate packet!\n");

	packet->len=size;
	memcpy((char *)packet->data, data, size);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		lprintf("Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOGClient::send(YOGMessageType v, Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size+4);
	if (packet==NULL)
	{
		lprintf("Failed to allocate packet!\n");
		return;
	}
	{
		Uint8 data[4];
		data[0]=v;
		data[1]=0;
		data[2]=0;
		data[3]=0;
		memcpy((char *)packet->data, data, 4);
	}
	packet->len=size+4;
	memcpy((char *)packet->data+4, data, size);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		lprintf("Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOGClient::send(YOGMessageType v, Uint8 id, Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size+4);
	if (packet==NULL)
	{
		lprintf("Failed to allocate packet!\n");
		return;
	}
	{
		Uint8 data[4];
		data[0]=v;
		data[1]=id;
		data[2]=0;
		data[3]=0;
		memcpy((char *)packet->data, data, 4);
	}
	packet->len=size+4;
	memcpy((char *)packet->data+4, data, size);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		lprintf("Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOGClient::send(const Message &m)
{
	int size=4+m.textLength+m.userNameLength;
	UDPpacket *packet=SDLNet_AllocPacket(size);
	if (packet==NULL)
	{
		lprintf("Failed to allocate packet!\n");
		return;
	}
	packet->len=size;
	{
		Uint8 data[4];
		data[0]=m.messageType;
		data[1]=m.messageID;
		data[2]=0;
		data[3]=0;
		memcpy((char *)packet->data, data, 4);
	}
	memcpy((char *)packet->data+4, m.text, m.textLength);
	memcpy((char *)packet->data+4+m.textLength, m.userName, m.userNameLength);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		lprintf("Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
	lprintf("size=%d, m.textLength=%d, m.userNameLength=%d.\n", size, m.textLength, m.userNameLength);
}

void YOGClient::sendGames()
{
	int nbGames=games.size();
	if (nbGames>16)
		nbGames=16;
	int size=(4+2+4+32+128)*nbGames+4;
	Uint8 data[size];
	addUint32(data, nbGames, 0); // This is redundancy
	int index=4;
	for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); ++game)
		if ((*game)->host->gameip.host && (*game)->host->gameip.port)
		{
			addUint32(data, (*game)->host->gameip.host, index);
			index+=4;
			addUint16(data, (*game)->host->gameip.port, index);
			index+=2;
			addUint32(data, (*game)->uid, index);
			index+=4;
			int l;
			l=strmlen((*game)->host->userName, 32);
			memcpy(data+index, (*game)->host->userName, l);
			index+=l;

			l=strmlen((*game)->name, 32);
			memcpy(data+index, (*game)->name, l);
			index+=l;
			lprintf("index=%d.\n", index);
		}
	send(YMT_GAMES_LIST, data, index);
	lprintf("sent a %d games list to %s\n", nbGames, userName);
}


void YOGClient::sendUnshared()
{
	int nbUnshared=unshared.size();
	if (nbUnshared>256)
		nbUnshared=256;
	int size=4*nbUnshared+4;
	Uint8 data[size];
	addUint32(data, nbUnshared, 0); // This is redundancy
	int index=4;
	for (std::list<Uint32>::iterator uid=unshared.begin(); uid!=unshared.end(); ++uid)
	{
		addUint32(data, *uid, index);
		index+=4;
	}
	send(YMT_UNSHARED_LIST, data, index);
	lprintf("sent a %d unshared list to %s\n", nbUnshared, userName);
}

void YOGClient::addGame(Game *game)
{
	games.push_back(game);
	if (games.size()>256)
		gamesTimeout=0;
	else
		gamesTimeout=10;
	gamesTOTL=3;
}

void YOGClient::removeGame(Uint32 uid)
{
	unshared.push_back(uid);
	if (unshared.size()>256)
		unsharedTimeout=0;
	else
		unsharedTimeout=10;
	unsharedTOTL=3;
}

void YOGClient::removeUselessGames()
{
	for (std::list<Uint32>::iterator uid=unshared.begin(); uid!=unshared.end(); ++uid)
		for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); ++game)
			if ((*game)->uid==*uid)
			{
				games.erase(game);
				break;
			}
}

void YOGClient::removeUselessGamesAndUnshared()
{
	int size=unshared.size();
	for (int i=0; i<size; i++)
	{
		for (std::list<Uint32>::iterator uid=unshared.begin(); uid!=unshared.end(); ++uid)
			for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); ++game)
				if ((*game)->uid==*uid)
				{
					games.erase(game);
					unshared.erase(uid);
					goto doublebreak;
				}
		doublebreak:;
	}
	
}

void YOGClient::sendClients()
{
	int nbClients=clients.size();
	if (nbClients>32)
		nbClients=32;
	int size=(32+4)*nbClients+4;
	Uint8 data[size];
	addUint32(data, nbClients, 0); // This is redundancy
	int index=4;
	for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
	{
		addUint32(data, (*client)->uid, index);
		index+=4;
		int l=strlen((*client)->userName)+1;
		assert(l<=32);
		memcpy(data+index, (*client)->userName, l);
		index+=l;
		assert(index<size);
	}
	send(YMT_CLIENTS_LIST, data, index);
	lprintf("sent a %d clients list to %s\n", nbClients, userName);
}

void YOGClient::sendLeftClients()
{
	int nbClients=leftClients.size();
	if (nbClients>32)
		nbClients=32;
	int size=4*nbClients+4;
	Uint8 data[size];
	addUint32(data, nbClients, 0);
	
	leftClientPacketID++;
	addUint32(data, leftClientPacketID, 4); // This is redundancy
	lastLeftClientNumber[leftClientPacketID&0x3]=nbClients;
	
	int index=8;
	for (std::list<Uint32>::iterator uid=leftClients.begin(); uid!=leftClients.end(); ++uid)
	{
		addUint32(data, *uid, index);
		lprintf("uid=%d.\n", *uid);
		index+=4;
	}
	send(YMT_LEFT_CLIENTS_LIST, data, index);
	lprintf("sent a %d left clients list to %s\n", nbClients, userName);
}

void YOGClient::addClient(YOGClient *client)
{
	clients.push_back(client);
	if (clients.size()>32)
		clientsTimeout=0;
	else
		clientsTimeout=10;
	clientsTOTL=3;
}

void YOGClient::removeClient(Uint32 uid)
{
	leftClients.push_back(uid);
	if (leftClients.size()>32)
		leftClientsTimeout=0;
	else
		leftClientsTimeout=10;
	leftClientsTOTL=3;
}

void YOGClient::removeUselessClients()
{
	for (std::list<Uint32>::iterator uid=leftClients.begin(); uid!=leftClients.end(); ++uid)
		for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
			if ((*client)->uid==*uid)
			{
				clients.erase(client);
				break;
			}
}

void YOGClient::lprintf(const char *msg, ...)
{
	va_list arglist;
	char output[256];

	va_start(arglist, msg);
	vsnprintf(output, 256, msg, arglist);
	va_end(arglist);
	output[255]=0;
	
	if (logServer)
		fputs(output, logServer);
	
	int i;
	for (i=0; i<256; i++)
		if (output[i]=='\n')
		{
			output[i]=0;
			break;
		}
	if (admin)
		admin->send(YMT_ADMIN_MESSAGE, (Uint8 *)output, i+1);
}

int YOGClient::strmlen(const char *s, int max)
{
	for (int i=0; i<max; i++)
		if (*(s+i)==0)
			return i+1;
	return max;
}
