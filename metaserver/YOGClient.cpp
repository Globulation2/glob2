/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�e
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#include "YOGClient.h"
#include "../src/Marshaling.h"

extern FILE *logServer;
extern YOGClient *admin;

inline void standardTimeout(int *timeout, const unsigned size, const int base, const int div)
{
	if (size==1)
		*timeout=16;
	else
	{
		int targetTimeout=base-size/div;
		if (targetTimeout<0)
			*timeout=0;
		else if (*timeout>targetTimeout)
			*timeout=targetTimeout;
	}
}

YOGClient::YOGClient(IPaddress ip, UDPsocket socket, char userName[32])
{
	this->ip=ip;
	hostGameip.host=0;
	hostGameip.port=0;
	joinGameip.host=0;
	joinGameip.port=0;
	this->socket=socket;
	memcpy(this->userName, userName, 32);
	
	lastSentMessageID=0;
	lastMessageID=0;
	messageTimeout=DEFAULT_NETWORK_TIMEOUT;
	messageTOTL=3;
	
	joinersTimeout=DEFAULT_NETWORK_TIMEOUT;
	sharingGame=NULL;
	joinedGame=NULL;
	
	timeout=DEFAULT_NETWORK_TIMEOUT;
	TOTL=3;
	
	gamesSize=0;
	gamesTimeout=DEFAULT_NETWORK_TIMEOUT;
	gamesTOTL=3;
	unsharedTimeout=DEFAULT_NETWORK_TIMEOUT;
	unsharedTOTL=3;
	
	static Uint32 clientUID=1;
	clientUID++;
	if (clientUID==0)
		clientUID++;
	uid=clientUID;
	
	clientsPacketID=0;
	clientsUpdatePacketID=0;
	playing=false;
	
	clientsTimeout=DEFAULT_NETWORK_TIMEOUT;
	clientsTOTL=3;
	clientsUpdatesTimeout=DEFAULT_NETWORK_TIMEOUT;
	clientsUpdatesTOTL=3;
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
	{
		lprintf("Failed to allocate packet!\n");
		return;
	}
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
	{
		lprintf("Failed to allocate packet!\n");
		return;
	}
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
	{
		lprintf("Failed to allocate packet!\n");
		return;
	}
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
	
	Uint8 sdata[4];
	sdata[0]=m.messageType;
	sdata[1]=m.messageID;
	sdata[2]=0;
	sdata[3]=0;
	memcpy((char *)packet->data, sdata, 4);
	
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
	if (gamesSize>16)
		nbGames=16;
	int size=(4+2+4+2+4+4+64)*nbGames+4;
	Uint8 data[size];
	int index=4;
	nbGames=0;
	for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); ++game)
		if ((*game)->host->hostGameip.host && (*game)->host->hostGameip.port)
		{
			addUint32(data, (*game)->host->hostGameip.host, index);
			index+=4;
			addUint16(data, (*game)->host->hostGameip.port, index);
			index+=2;
			addUint32(data, (*game)->uid, index);
			index+=4;
			
			addUint32(data, (*game)->host->uid, index);
			index+=4;

			int l=strmlen((*game)->name, 64);
			memcpy(data+index, (*game)->name, l);
			index+=l;
			lprintf("%d index=%d.\n", nbGames, index);
			nbGames++;
		}
	addUint32(data, nbGames, 0); // This is redundancy
	assert(nbGames=gamesSize);
	lprintf("may send a %d/%d games list to %s\n", nbGames, gamesSize, userName);
	if (nbGames>0)
		send(YMT_GAMES_LIST, data, index);
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
	computeGamesSize();
	standardTimeout(&gamesTimeout, gamesSize, 16, 1);
	gamesTOTL=3;
}

void YOGClient::addMessage(Message *message)
{
	unsigned size=messages.size();
	if (size<256)
	{
		lastMessageID++;
		if (lastMessageID==0)
			lastMessageID++;
		message->messageID=lastMessageID;
		messages.push_back(*message);
		standardTimeout(&messageTimeout, size, 4, 64);
		messageTOTL=3;
	}
	else
		lprintf("Warning!, client (%s) is being flooded!\n", userName);
}

void YOGClient::deleteMessage(Uint8 messageID)
{
	unsigned size=messages.size();
	if (size==0)
		return;
	std::list<Message>::iterator mit=messages.begin();
	if (mit->messageID==messageID)
	{
		lprintf("message (%s) delivered to (%s)\n", mit->text, userName);
		messages.erase(mit);
		standardTimeout(&messageTimeout, size, 4, 64);//TODO:here we can improve the TCP/IP friendlyness
		messageTOTL=3;
	}
}

void YOGClient::removeGame(Uint32 uid)
{
	unshared.push_back(uid);
	standardTimeout(&unsharedTimeout, unshared.size(), 16, 16);
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
	computeGamesSize();
}

void YOGClient::computeGamesSize()
{
	gamesSize=0;
	for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); ++game)
		if ((*game)->host->hostGameip.host && (*game)->host->hostGameip.port)
			gamesSize++;
}

void YOGClient::gamesClear()
{
	// Actualy, we only remove one game.
	gamesSize=0;
	for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); ++game)
		if ((*game)->host->hostGameip.host && (*game)->host->hostGameip.port)
		{
			games.erase(game);
			break;
		}
	computeGamesSize();
}

void YOGClient::sendClients()
{
	int nbClients=clients.size();
	if (nbClients>32)
		nbClients=32;
	int size=(4+32+2)*nbClients+1;
	Uint8 data[size];
	addUint8(data, (Uint8)nbClients, 0); // This is redundancy
	clientsPacketID++;
	addUint8(data, clientsPacketID, 1); // This is redundancy
	int index=2;
	int rri=clientsPacketID&15;
	lastSentClients[rri].clear();
	for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
	{
		addUint32(data, (*client)->uid, index);
		index+=4;
		int l=strlen((*client)->userName)+1;
		assert(l<=32);
		memcpy(data+index, (*client)->userName, l);
		index+=l;
		data[index]=(*client)->hostGameip.host||(*client)->joinGameip.host;
		index++;
		assert(index<size);
		lastSentClients[rri].push_back(*client);
	}
	send(YMT_CLIENTS_LIST, data, index);
	lprintf("sent a (%d) clients list to (%s), cpid=%d\n", nbClients, userName, clientsPacketID);
}

void YOGClient::sendClientsUpdates()
{
	int nbClients=clientsUpdates.size();
	if (nbClients>32)
		nbClients=32;
	int size=6*nbClients+2;
	Uint8 data[size];
	addUint8(data, (Uint8)nbClients, 0);
	
	clientsUpdatePacketID++;
	addUint8(data, clientsUpdatePacketID, 1); // This is redundancy
	
	int index=2;
	int rri=clientsUpdatePacketID&15;
	lastSentClientsUpdates[rri].clear();
	for (std::list<ClientUpdate>::iterator cup=clientsUpdates.begin(); cup!=clientsUpdates.end(); ++cup)
	{
		addUint32(data, cup->uid, index);
		index+=4;
		addUint16(data, cup->change, index);
		index+=2;
		lastSentClientsUpdates[rri].push_back(*cup);
	}
	
	if (nbClients>0)
		send(YMT_UPDATE_CLIENTS_LIST, data, index);
	lprintf("sent a (%d) clients update list to (%s), cupid=%d\n", nbClients, userName, clientsUpdatePacketID);
}

void YOGClient::addClient(YOGClient *client)
{
	for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
		if (*c==client)
		{
			assert(false);
			return;
		}
	clients.push_back(client);
	standardTimeout(&clientsTimeout, clients.size(), 16, 2);
	clientsTOTL=3;
}

void YOGClient::removeClient(Uint32 uid)
{
	for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
		if ((*client)->uid==uid)
		{
			clients.erase(client);
			break;
		}
	for (int i=0; i<16; i++)
		for (std::list<YOGClient *>::iterator client=lastSentClients[i].begin(); client!=lastSentClients[i].end(); ++client)
			if ((*client)->uid==uid)
			{
				lastSentClients[i].erase(client);
				break;
			}
	bool found=false;
	for (std::list<ClientUpdate>::iterator cup=clientsUpdates.begin(); cup!=clientsUpdates.end(); ++cup)
		if (cup->uid==uid)
		{
			cup->change=CUP_LEFT;
			found=true;
			break;
		}
	if (!found)
	{
		ClientUpdate cup;
		cup.uid=uid;
		cup.change=CUP_LEFT;

		clientsUpdates.push_back(cup);
	}
	
	int importance=0;
	for (std::list<ClientUpdate>::iterator cup=clientsUpdates.begin(); cup!=clientsUpdates.end(); ++cup)
		if (cup->change==CUP_LEFT)
			importance+=2;
		else
			importance++;
	standardTimeout(&clientsUpdatesTimeout, importance, 16, 4);
	clientsUpdatesTOTL=3;
}

void YOGClient::updateClient(Uint32 uid, bool playing)
{
	for (std::list<ClientUpdate>::iterator cup=clientsUpdates.begin(); cup!=clientsUpdates.end(); ++cup)
		if (cup->uid==uid)
		{
			if (playing)
			{
				if (cup->change==CUP_NOT_PLAYING)
					cup->change=CUP_PLAYING;
				else
					assert(cup->change!=CUP_LEFT);
			}
			else
			{
				if (cup->change==CUP_PLAYING)
					cup->change=CUP_NOT_PLAYING;
				else
					assert(cup->change!=CUP_LEFT);
			}
			return;
		}
	
	ClientUpdate cup;
	cup.uid=uid;
	if (playing)
		cup.change=CUP_PLAYING;
	else
		cup.change=CUP_NOT_PLAYING;
	
	clientsUpdates.push_back(cup);
	
	int importance=0;
	for (std::list<ClientUpdate>::iterator cup=clientsUpdates.begin(); cup!=clientsUpdates.end(); ++cup)
		if (cup->change==CUP_LEFT)
			importance+=2;
		else
			importance++;
	standardTimeout(&clientsUpdatesTimeout, importance, 16, 4);
	clientsUpdatesTOTL=3;
}

void YOGClient::lprintf(const char *msg, ...)
{
	va_list arglist;
	char output[256];

	va_start(arglist, msg);
	vsnprintf(output, 256, msg, arglist);
	va_end(arglist);
	output[255]=0;
	if (strcmp(YOG_SERVER_IP, "192.168.1.5")==0)
		printf("%s", output);
	
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
