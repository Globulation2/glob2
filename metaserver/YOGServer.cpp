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

#include "../gnupg/sha1.c"

#include "../src/Marshaling.h"
#include "YOGServer.h"
#include "../src/Utilities.h"
#include <errno.h>
#include <stdarg.h>

FILE *logServerFile=NULL;
YOGClient *admin=NULL;

YOGServer::YOGServer()
{
	socket=NULL;
	running=false;
	gameUID=1;
}

YOGServer::~YOGServer()
{
	if (socket)
		SDLNet_UDP_Close(socket);
	
	for (std::list<YOGClient *>::iterator client=unconnectedClients.begin(); client!=unconnectedClients.end(); ++client)
		delete (*client);
	for (std::list<YOGClient *>::iterator client=connectedClients.begin(); client!=connectedClients.end(); ++client)
		delete (*client);
	for (std::list<YOGClient *>::iterator client=authentifiedClients.begin(); client!=authentifiedClients.end(); ++client)
		delete (*client);
	for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); ++game)
		delete (*game);
}

bool YOGServer::init()
{
	lprintf("YOGServer::init()\n");
	
	socket=SDLNet_UDP_Open(YOG_SERVER_PORT);
	if (!socket)
	{
		lprintf("failed to open socket at port %d.\n", YOG_SERVER_PORT);
		return false;
	}
	return true;
}

void YOGServer::send(IPaddress ip, Uint8 *data, int size)
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

void YOGServer::send(IPaddress ip, YOGMessageType v)
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

void YOGServer::send(IPaddress ip, YOGMessageType v, Uint8 id)
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

void YOGServer::executeCommand(YOGClient *sender, char *s, Uint8 messageID)
{
	if (s[0]!='/')
		return;
	switch (s[1])
	{
		case 'w':
		case 'm':
		if (s[2]==' ')
		{
			PrivateReceipt privateReceipt;
			privateReceipt.messageID=messageID;

			for (std::list<YOGClient *>::iterator client=authentifiedClients.begin(); client!=authentifiedClients.end(); ++client)
			{
				char *name=(*client)->userName;
				int unl=Utilities::strnlen(name, 32);
				bool away=(*client)->away;
				if ((strncmp(name, s+3, unl)==0)&&(s[3+unl]==' '))
				{
					Message m;
					int l;
					l=Utilities::strmlen(s+unl+4, 256-unl-4);
					memcpy(m.text, s+unl+4, l);
					m.text[l-1]=0;
					m.textLength=l;

					l=Utilities::strmlen(sender->userName, 32);
					memcpy(m.userName, sender->userName, l);
					m.userName[l-1]=0;
					m.userNameLength=l;

					m.messageType=YMT_PRIVATE_MESSAGE;
					(*client)->addMessage(&m);
					
					privateReceipt.addr.push_back(unl);
					privateReceipt.away.push_back(away);
					if (away)
					{
						char *awayMessage=(*client)->awayMessage;
						int l=Utilities::strmlen(awayMessage, 64);
						char *s=(char *)malloc(l);
						memcpy(s, awayMessage, l);
						lprintf("l=(%d), s=(%s), s=(%x)\n", l, s, (int)s);
						privateReceipt.awayMessages.push_front(s);
						lprintf("(*client)->awayMessage=(%s)\n", (*client)->awayMessage);
						lprintf("awayMessages.begin()=(%s)\n", *privateReceipt.awayMessages.begin());
					}
				}
			}

			sender->addReceipt(&privateReceipt);
		}
		break;
		case 'a':
		{
			bool playing=sender->playing; // We could completely avoid to send the CUP_[NOT_]PLAYING flag.
			bool away=!sender->away;
			sender->away=away;
			Uint32 uid=sender->uid;
			Uint16 change=0;
			if (playing)
				change|=CUP_PLAYING;
			else
				change|=CUP_NOT_PLAYING;
			if (away)
				change|=CUP_AWAY;
			else
				change|=CUP_NOT_AWAY;
			for (std::list<YOGClient *>::iterator client=authentifiedClients.begin(); client!=authentifiedClients.end(); ++client)
				(*client)->updateClient(uid, change);
			if (away)
			{
				if (s[2]==' ') // Do we have one argument ?
					strncpy(sender->awayMessage, s+3, 64);
				else
					sender->awayMessage[0]=0;
				lprintf("Client (%s) is now away (%s).\n", sender->userName, sender->awayMessage);
			}
			else
				lprintf("Client (%s) is no more away.\n", sender->userName);
		}
		break;
		case 0:
		{
			return;
		}
		break;
	}
}

void YOGServer::removeGame(YOGClient *host)
{
	Game *game=host->sharingGame;
	bool good=false;
	std::list<Game *>::iterator gi;
	for (gi=games.begin(); gi!=games.end(); ++gi)
		if ((*gi)->host==host)
		{
			good=true;
			break;
		}
	assert(good); // This is a database consistency.
	assert(host->sharingGame==game);
	if (host->joinedGame)
		if (host->joinedGame!=game)
			lprintf("Warning! A game-host has not joined his own game!\n");
	
	Uint32 uid=game->uid;
	std::list<YOGClient *> &joiners=game->joiners;
	for (std::list<YOGClient *>::iterator c=joiners.begin(); c!=joiners.end(); ++c)
		if ((*c)->joinedGame!=game)
			lprintf("Warning! Critical error! We have a (game->joiners->joinedGame!=game) inconstancy!\n");
		else
		{
			(*c)->joinedGame=NULL;
			lprintf("removed (%s) from game (%s).\n", (*c)->userName, game->name);
		}
	
	for (std::list<YOGClient *>::iterator c=authentifiedClients.begin(); c!=authentifiedClients.end(); ++c)
	{
		(*c)->removeGame(uid);
		(*c)->removeUselessGames();
	}
	games.erase(gi);
	delete game;
	
	host->joiners.clear();
	host->sharingGame=NULL;
	host->joinedGame=NULL;
	host->hostGameip.host=0;
	host->joinGameip.host=0;
}

void YOGServer::deconnectClient(YOGClient *client)
{
	if (client->sharingGame)
		removeGame(client);
	client->sharingGame=NULL;
	
	if (client->joinedGame)
	{
		client->joinedGame->joiners.remove(client);
		client->joinedGame=NULL;
	}

	Uint32 uid=client->uid;
	for (std::list<YOGClient *>::iterator c=authentifiedClients.begin(); c!=authentifiedClients.end(); ++c)
		(*c)->removeClient(uid);

	if (client==admin)
		admin=NULL;
	
	client->deconnected();
}

void YOGServer::treatPacket(IPaddress ip, Uint8 *data, int size)
{
	//lprintf("packet type (%d, %d) received by ip=%s\n", data[0], data[1], , Utilities::stringIP(ip));
	if (data[2]!=0 || data[3]!=0)
	{
		lprintf("bad packet\n");
		return;
	}
	switch (data[0])
	{
	case YMT_BAD:
		lprintf("bad packet.\n");
	break;
	case YMT_CONNECTING:
	{
		if (size>8+32 || size<8+2)
		{
			lprintf("bad YMT_CONNECTING message size (%d).\n", size);
			break;
		}
		Uint32 protocolVersion=getUint32(data, 4);
		if (protocolVersion!=YOG_PROTOCOL_VERSION)
		{
			lprintf("bad protocolVersion=%d, YOG_PROTOCOL_VERSION=%d\n", protocolVersion, YOG_PROTOCOL_VERSION);
			Uint8 data[8];
			data[0]=YMT_CONNECTION_REFUSED;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			
			data[4]=YCRT_PROTOCOL_TOO_OLD;
			data[5]=YOG_PROTOCOL_VERSION;
			data[6]=0;
			data[7]=0;
			send(ip, data, 8);
			return;
		}
		YOGClient *authentifiedClient=NULL;
		for (std::list<YOGClient *>::iterator ci=authentifiedClients.begin(); ci!=authentifiedClients.end(); ++ci)
			if ((*ci)->hasip(ip))
			{
				authentifiedClient=*ci;
				break;
			}
		if (authentifiedClient)
		{
			Uint8 data[8];
			data[0]=YMT_CONNECTION_REFUSED;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			
			data[4]=YCRT_ALREADY_AUTHENTICATED;
			data[5]=YOG_PROTOCOL_VERSION;
			data[6]=0;
			data[7]=0;
			send(ip, data, 8);
			return;
		}
		char userName[32];
		memset(userName, 0, 32);
		strncpy(userName, (char *)data+8, 32);
		userName[31]=0;
		YOGClient *client=NULL;
		YOGClient *connectedClient=NULL;
		YOGClient *unconnectedClient=NULL;
		std::list<YOGClient *>::iterator cci;
		for (cci=connectedClients.begin(); cci!=connectedClients.end(); ++cci)
			if (strncmp((*cci)->userName, userName, 32)==0)
			{
				connectedClient=*cci;
				break;
			}
		std::list<YOGClient *>::iterator uci;
		for (uci=unconnectedClients.begin(); uci!=unconnectedClients.end(); ++uci)
			if (strncmp((*uci)->userName, userName, 32)==0)
			{
				unconnectedClient=*uci;
				break;
			}
		for (std::list<YOGClient *>::iterator aci=authentifiedClients.begin(); aci!=authentifiedClients.end(); ++aci)
			if (strncmp((*aci)->userName, userName, 32)==0)
			{
				Uint8 data[8];
				data[0]=YMT_CONNECTION_REFUSED;
				data[1]=0;
				data[2]=0;
				data[3]=0;
				
				data[4]=YCRT_USERNAME_ALLREADY_USED;
				data[5]=YOG_PROTOCOL_VERSION;
				data[6]=0;
				data[7]=0;
				send(ip, data, 8);
				return;
			}
		if (connectedClient)
		{
			if (unconnectedClient)
			{
				lprintf("Internal integrity error, (connectedClient && unconnectedClient), userName=(%s) ip=(%s)\n",
					userName, Utilities::stringIP(ip));
				unconnectedClients.erase(uci);
				unconnectedClient=NULL;
			}
			client=connectedClient;
		}
		else
		{
			if (unconnectedClient)
			{
				client=unconnectedClient;
				unconnectedClients.erase(uci);
			}
			else
				client=new YOGClient(ip, socket, userName);
			connectedClients.push_back(client);
		}
		client->reconnected(ip);
		if ((data[1]&1)==1 && (strncmp(userName, "admin", 32)==0))
			lprintf("admin (%s) uid=%d connected from (%s). Authenticating.\n", client->userName, client->uid, Utilities::stringIP(ip));
		else
			lprintf("client (%s) uid=%d connected from (%s). Authenticating.\n", client->userName, client->uid, Utilities::stringIP(ip));
		Uint8 data[36];
		data[0]=YMT_CONNECTING;
		data[1]=0;
		data[2]=0;
		data[3]=0;
		client->newRandomXorPassw();
		memcpy(data+4, client->xorpassw, 32);
		send(ip, data, 36);
	}
	break;
	case YMT_AUTHENTICATING:
	{
		if (size!=24 && size!=36)
		{
			lprintf("bad YMT_AUTHENTIFING message size (%d).\n", size);
			break;
		}
		YOGClient *authentifiedClient=NULL;
		for (std::list<YOGClient *>::iterator ci=authentifiedClients.begin(); ci!=authentifiedClients.end(); ++ci)
			if ((*ci)->hasip(ip))
			{
				(*ci)->TOTL=3;
				authentifiedClient=*ci;
				break;
			}
		if (authentifiedClient)
		{
			Uint8 data[8];
			data[0]=YMT_AUTHENTICATING;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			addSint32(data, authentifiedClient->uid, 4);
			send(ip, data, 8);
		}
		YOGClient *connectedClient=NULL;
		std::list<YOGClient *>::iterator cci;
		for (cci=connectedClients.begin(); cci!=connectedClients.end(); ++cci)
			if ((*cci)->hasip(ip))
			{
				connectedClient=*cci;
				break;
			}
		if (connectedClient==NULL)
		{
			Uint8 data[8];
			data[0]=YMT_CONNECTION_REFUSED;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			
			data[4]=YCRT_NOT_CONNECTED_YET;
			data[5]=YOG_PROTOCOL_VERSION;
			data[6]=0;
			data[7]=0;
			send(ip, data, 8);
			return;
		}
		YOGClient *client=connectedClient;
		
		if ((data[1]&2)==0 && size==36)
		{
			// newYogPassword
			// We received xored (password):
			Uint8 xored[32];
			memcpy(xored, (char *)data+4, 32);
			// We compute real password:
			Uint8 passWord[32];
			for (int i=0; i<32; i++)
				passWord[i]=xored[i]^client->xorpassw[i];
			//printf("newYogPassword (%s) for client (%s) \n", passWord, client->userName);
			if (client->passWord[0]==0)
				memcpy(client->passWord, passWord, 32);
			else
			{
				Uint8 data[8];
				data[0]=YMT_CONNECTION_REFUSED;
				data[1]=0;
				data[2]=0;
				data[3]=0;
				
				data[4]=YCRT_ALREADY_PASSWORD;
				data[5]=YOG_PROTOCOL_VERSION;
				data[6]=0;
				data[7]=0;
				send(ip, data, 8);
				return;
			}
		}
		else if ((data[1]&2)==2 && size==24)
		{
			// hashed
			// We received hashed:
			Uint8 receivedHashed[20];
			memcpy(receivedHashed, (char *)data+4, 20);
			// We compute hashed:
			Uint8 xored[32];
			for (int i=0; i<32; i++)
				xored[i]=client->passWord[i]^client->xorpassw[i];
			
			/*printf("passWord=");
			for (int i=0; i<32; i+=4)
				printf("[%2d %2d %2d %2d] ", client->passWord[i], client->passWord[i+1], client->passWord[i+2], client->passWord[i+3]);
			printf("\n");
			printf("xorpassw=");
			for (int i=0; i<32; i+=4)
				printf("[%2d %2d %2d %2d] ", client->xorpassw[i], client->xorpassw[i+1], client->xorpassw[i+2], client->xorpassw[i+3]);
			printf("\n");
			printf("xored   =");
			for (int i=0; i<32; i+=4)
				printf("[%2d %2d %2d %2d] ", xored[i], xored[i+1], xored[i+2], xored[i+3]);
			printf("\n");*/
			
			SHA1_CONTEXT hd;
			sha1_init(&hd);
			sha1_write(&hd, xored, 32);
			sha1_final(&hd);
			Uint8 *computedHashed=sha1_read(&hd);
			
			// We compare:
			if (memcmp(computedHashed, receivedHashed, 20)!=0)
			{
				Uint8 data[8];
				data[0]=YMT_CONNECTION_REFUSED;
				data[1]=0;
				data[2]=0;
				data[3]=0;
				
				data[4]=YCRT_BAD_PASSWORD;
				data[5]=YOG_PROTOCOL_VERSION;
				data[6]=0;
				data[7]=0;
				send(ip, data, 8);
				return;
			}
		}
		else
		{
			Uint8 data[8];
			data[0]=YMT_CONNECTION_REFUSED;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			
			data[4]=YCRT_PROTOCOL_TOO_OLD;
			data[5]=YOG_PROTOCOL_VERSION;
			data[6]=0;
			data[7]=0;
			send(ip, data, 8);
			return;
		}
		
		connectedClients.erase(cci);
		
		if ((data[1]&1)==1 && strncmp(client->userName, "admin", 32)==0)
		{
			admin=client;
			lprintf("new admin authentified as (%s), from (%s), uid=(%d)\n", client->userName, Utilities::stringIP(ip), client->uid);
		}
		else
			lprintf("new client authentified as (%s), from (%s), uid=(%d)\n", client->userName, Utilities::stringIP(ip), client->uid);
		
		for (std::list<YOGClient *>::iterator aci=authentifiedClients.begin(); aci!=authentifiedClients.end(); ++aci)
			client->addClient(*aci);
		
		authentifiedClients.push_back(connectedClient);
		
		for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); game++)
			client->addGame(*game);
		
		for (std::list<YOGClient *>::iterator aci=authentifiedClients.begin(); aci!=authentifiedClients.end(); ++aci)
			(*aci)->addClient(client);
		
		
		Uint8 data[8];
		data[0]=YMT_AUTHENTICATING;
		data[1]=0;
		data[2]=0;
		data[3]=0;
		addSint32(data, client->uid, 4);
		send(ip, data, 8);
	}
	break;
	case YMT_DECONNECTING:
	{
		send(ip, YMT_DECONNECTING);
		std::list<YOGClient *>::iterator client;
		for (client=authentifiedClients.begin(); client!=authentifiedClients.end(); ++client)
			if ((*client)->hasip(ip))
			{
				lprintf("(authentified)client \"%s\" deconnected.\n", (*client)->userName);
				deconnectClient(*client);
				authentifiedClients.erase(client);
				unconnectedClients.push_back(*client);
				return;
			}
		for (client=connectedClients.begin(); client!=connectedClients.end(); ++client)
			if ((*client)->hasip(ip))
			{
				lprintf("(connected)client \"%s\" deconnected.\n", (*client)->userName);
				deconnectClient(*client);
				connectedClients.erase(client);
				unconnectedClients.push_back(*client);
				return;
			}
	}
	break;
	case YMT_SEND_MESSAGE:
	{
		if (size>YOG_MAX_PACKET_SIZE)
		{
			lprintf("bad message size (%d).\n", size);
			break;
		}
		Uint8 messageID=data[1];
		send(ip, YMT_SEND_MESSAGE, messageID); // We allways confirm client that we received the packet.
		
		bool good=false;
		std::list<YOGClient *>::iterator sender;
		for (sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true; // ok, he's connected
				if ((*sender)->lastSentMessageID == messageID)
					good=false; // bad, this message allready sent.
				break;
			}
		if (good)
		{
			char *s=(char *)data+4;
			
			YOGClient *c=*sender;
			c->lastSentMessageID=messageID;
			if (s[0]=='/')
			{
				// We received a command
				executeCommand(c, s, messageID);
			}
			else
			{
				Message m;
				int l;

				l=Utilities::strmlen(s, 256);
				memcpy(m.text, s, l);
				m.text[l-1]=0;
				m.textLength=l;

				l=Utilities::strmlen(c->userName, 32);
				memcpy(m.userName, c->userName, l);
				m.userName[l-1]=0;
				m.userNameLength=l;
				if (c==admin)
					m.messageType=YMT_ADMIN_MESSAGE;
				else
					m.messageType=YMT_MESSAGE;
				
				// Here we have to send this message to all authentifiedClients!
				lprintf("%d:%d %s:%s\n", m.textLength, m.userNameLength, m.userName, m.text);
				for (std::list<YOGClient *>::iterator client=authentifiedClients.begin(); client!=authentifiedClients.end(); ++client)
					if ((*client!=admin) || (c!=admin))
						(*client)->addMessage(&m);
			}
		}
	}
	break;
	case YMT_MESSAGE:
	{
		if (size!=4)
		{
			lprintf("bad YMT_MESSAGE message size (%d).\n", size);
			break;
		}
		Uint8 messageID=data[1];
		bool good=false;
		std::list<YOGClient *>::iterator sender;
		for (sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true; // ok, he's connected
				break;
			}
		if (good)
			(*sender)->deliveredMessage(messageID);
	}
	break;
	case YMT_PRIVATE_RECEIPT:
	{
		if (size!=4)
		{
			lprintf("bad YMT_PRIVATE_RECEIPT message size (%d).\n", size);
			break;
		}
		Uint8 receiptID=data[1];
		bool good=false;
		std::list<YOGClient *>::iterator sender;
		for (sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true; // ok, he's connected
				break;
			}
		if (good)
			(*sender)->deliveredReceipt(receiptID);
	}
	break;
	case YMT_SHARING_GAME:
	{
		if (size>4+64)
		{
			lprintf("bad message size (%d).\n", size);
			break;
		}
		
		bool good=false;
		bool dosend=false;
		std::list<YOGClient *>::iterator sender;
		for (sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true; // ok, he's connected
				dosend=true;
				if ((*sender)->sharingGame)
					good=false; // bad, he's allready sharing a game.
				break;
			}
		if (good)
		{
			YOGClient *host=*sender;
			Game *game=new Game();
			strncpy(game->name, (char *)data+4, 64);
			if (game->name[63]!=0)
				lprintf("warning, non-zero ending game name!\n");
			game->name[63]=0;
			game->host=host;
			gameUID++;
			if (gameUID==0)
				gameUID++;
			game->uid=gameUID;
			games.push_back(game);
			host->sharingGame=game;
			lprintf("%s is hosting a game called %s. (gameUID=%d)\n", host->userName, game->name, gameUID);
		}
		if (dosend)
		{
			Uint8 data[8];
			data[0]=YMT_SHARING_GAME;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			addUint32(data, (*sender)->sharingGame->uid, 4);
			lprintf("YMT_SHARING_GAME (gameUID=%d)\n", (*sender)->sharingGame->uid);
			send(ip, (Uint8 *)data, 8); // We allways confirm client that we received the packet.
		}
	}
	break;
	case YMT_STOP_SHARING_GAME:
	{
		if (size>4)
		{
			lprintf("bad message size (%d).\n", size);
			break;
		}
		send(ip, YMT_STOP_SHARING_GAME); // We allways confirm client that we received the packet.
		
		bool good=false;
		std::list<YOGClient *>::iterator sender;
		for (sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip) && (*sender)->sharingGame)
			{
				(*sender)->TOTL=3;
				good=true;
				break;
			}
		if (good)
		{
			lprintf("(%s) stop hosting the game called (%s).\n", (*sender)->userName, (*sender)->sharingGame->name);
			removeGame(*sender);
		}
		else
			lprintf("allready stoped sharing the game!\n");
	}
	break;
	case YMT_STOP_PLAYING_GAME:
	{
		if (size!=4)
		{
			lprintf("bad YMT_STOP_JOINING_GAME message size (%d).\n", size);
			break;
		}
		send(ip, YMT_STOP_PLAYING_GAME);
		
		std::list<YOGClient *>::iterator sender;
		for (sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				lprintf("(%s) stop joining the game.\n", (*sender)->userName);
				if ((*sender)->playing)
				{
					lprintf("(ok)\n");
					Uint32 cuid=(*sender)->uid;
					for (std::list<YOGClient *>::iterator c=authentifiedClients.begin(); c!=authentifiedClients.end(); ++c)
						(*c)->updateClient(cuid, (Uint16)CUP_NOT_PLAYING);
					(*sender)->joinGameip.host=0;
					(*sender)->joinGameip.port=0;
					(*sender)->hostGameip.host=0;
					(*sender)->hostGameip.port=0;
					(*sender)->playing=false;
				}
				break;
			}
	}
	break;
	case YMT_GAMES_LIST:
	{
		if (size!=4)
		{
			lprintf("bad game list size (%d).\n", size);
			break;
		}
		
		bool good=false; // is he connected ?
		std::list<YOGClient *>::iterator sender;
		for (sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true;
				break;
			}
		if (good)
		{
			YOGClient *c=*sender;
			int nbGames=(int)data[1];
			lprintf("client %s received a %d games list.\n", c->userName, nbGames);
			for (int i=0; i<nbGames; i++)
			{
				std::list<Game *>::iterator game=c->games.begin();
				lprintf("%d game %s\n", i, (*game)->name);
				assert(game!=c->games.end());
				c->games.erase(game);
			}
		}
	}
	break;
	case YMT_UNSHARED_LIST:
	{
		if (size!=4)
		{
			lprintf("bad unshared list size (%d).\n", size);
			break;
		}
		
		bool good=false; // is he connected ?
		std::list<YOGClient *>::iterator sender;
		for (sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true;
				break;
			}
		if (good)
		{
			YOGClient *c=*sender;
			int nbUnshared=(int)data[1];
			lprintf("client %s received a %d unshare list.\n", c->userName, nbUnshared);
			for (int i=0; i<nbUnshared; i++)
			{
				std::list<Uint32>::iterator uid=c->unshared.begin();
				assert(uid!=c->unshared.end());
				c->unshared.erase(uid);
			}
		}
	}
	break;
	case YMT_CLIENTS_LIST:
	{
		if (size!=6)
		{
			lprintf("bad client list size (%d).\n", size);
			break;
		}
		
		bool good=false; // is he connected ?
		std::list<YOGClient *>::iterator sender;
		for (sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true;
				break;
			}
		if (good)
		{
			YOGClient *client=*sender;
			unsigned nbClients=(unsigned)getUint8(data, 4);
			Uint8 clientsPacketID=getUint8(data, 5);
			Uint8 diff=client->clientsPacketID-clientsPacketID;
			if (diff<=3)
			{
				int rri=(clientsPacketID&15);
				unsigned clscs=client->lastSentClients[rri].size();
				lprintf("client (%s) received a (%d) client list. (p-cpid=%d, c-cpid=%d)\n", client->userName, nbClients, clientsPacketID, client->clientsPacketID);
				
				if (client->lastSentClients[rri].size()!=nbClients)
					printf("warning! (clscs=%d) != (nbClients=%d)\n", client->lastSentClients[rri].size(), nbClients);
				else if (clscs)
				{
					std::list<YOGClient *>::iterator rc=client->lastSentClients[rri].begin();
					std::list<YOGClient *>::iterator rcend=client->lastSentClients[rri].end();
					std::list<YOGClient *>::iterator lc=client->clients.begin();
					std::list<YOGClient *>::iterator lcend=client->clients.end();
					while(lc!=lcend)
					{
						if (*lc==*rc)
						{
							client->clients.erase(lc);
							lc=client->clients.begin();
							rc++;
							if (rc==rcend)
								break;
						}
						else
							lc++;
					}
				
				}
			}
			else
				lprintf("warning! client (%s) sent a boguous or too old  client list. (p-cpid=%d, c-cpid=%d)\n", client->userName, clientsPacketID, client->clientsPacketID);
		}
	}
	break;
	case YMT_UPDATE_CLIENTS_LIST:
	{
		if (size!=6)
		{
			lprintf("bad left client list size (%d).\n", size);
			break;
		}
		
		bool good=false; // is he connected ?
		std::list<YOGClient *>::iterator sender;
		for (sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true;
				break;
			}
		if (good)
		{
			YOGClient *client=*sender;
			
			unsigned nbClients=getUint8(data, 4);
			Uint8 clientsUpdatePacketID=getUint8(data, 5);
			Uint8 diff=client->clientsUpdatePacketID-clientsUpdatePacketID;
			if (diff<=3)
			{
				int rri=(clientsUpdatePacketID&15);
				unsigned clscus=client->lastSentClientsUpdates[rri].size();
				lprintf("client (%s) received a (%d) update client list. (p-cupid=%d, c-cupid=%d)\n", client->userName, nbClients, clientsUpdatePacketID, client->clientsUpdatePacketID);
				if (client->lastSentClientsUpdates[rri].size()!=nbClients)
					printf("warning! (clscus=%d) != (nbClients=%d)\n", client->lastSentClientsUpdates[rri].size(), nbClients);
				else if (clscus)
				{
					std::list<ClientUpdate>::iterator rcup=client->lastSentClientsUpdates[rri].begin();
					std::list<ClientUpdate>::iterator rcupend=client->lastSentClientsUpdates[rri].end();
					std::list<ClientUpdate>::iterator lcup=client->clientsUpdates.begin();
					std::list<ClientUpdate>::iterator lcupend=client->clientsUpdates.end();
					while(lcup!=lcupend)
					{
						if (lcup->uid==rcup->uid)
						{
							client->clientsUpdates.erase(lcup);
							lcup=client->clientsUpdates.begin();
							rcup++;
							if (rcup==rcupend)
								break;
						}
						else
							lcup++;
					}
				
				}
			}
			else
				lprintf("warning! client (%s) sent a boguous or too old update client list. (p-cupid=%d). (c-cupid=%d)\n", client->userName, clientsUpdatePacketID, client->clientsUpdatePacketID);
		}
	}
	break;
	case YMT_CONNECTION_PRESENCE:
	{
		if (size>4)
		{
			lprintf("bad message size (%d).\n", size);
			break;
		}
		send(ip, YMT_CONNECTION_PRESENCE);
		for (std::list<YOGClient *>::iterator sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				break;
			}
	}
	break;
	case YMT_FLUSH_FILES:
	{
		int id=data[1];
		if (size!=8 || data[4]!=0xAB || data[5]!=(0xCD&id) || data[6]!=(0xEF|id) || data[7]!=0x12)
		{
			lprintf("Warning flush temptative (%d)\n", size);
			break;
		}
		if (id)
		{
			if (logServerFile)
				fclose(logServerFile);
			send(ip, YMT_FLUSH_FILES);
			char s[128];
			snprintf(s, 128, "YOGServer%d.log", id);
			logServerFile=fopen(s, "w");
		}
		else
		{
			fclose(logServerFile);
			logServerFile=NULL;
			lprintf("logServerFile closed\n");
		}
	}
	break;
	case YMT_CLOSE_YOG:
	{
		int id=data[1];
		if (size!=8 || data[4]!=0x12 || data[5]!=(0x23&id) || data[6]!=(0x34|id) || data[7]!=0x45)
		{
			lprintf("Warning closing temptative (size=%d), from ip=(%s)\n", size, Utilities::stringIP(ip));
			break;
		}
		bool good=false;
		for (std::list<YOGClient *>::iterator sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				good=(*sender==admin);
				break;
			}
		if (!good)
		{
			lprintf("Warning closing temptative not by admin, from ip=(%s)\n", Utilities::stringIP(ip));
			break;
		}
		if (id)
		{
			lprintf("YOG metaserver closing\n");
			send(ip, YMT_CLOSE_YOG);
			std::list<YOGClient *>::iterator client;
			for (client=authentifiedClients.begin(); client!=authentifiedClients.end(); ++client)
				 (*client)->send(YMT_CLOSE_YOG);
			running=false;
		}
	}
	break;
	case YMT_HOST_GAME_SOCKET:
	{
		Uint32 cuid=getUint32(data, 4);
		lprintf("YMT_HOST_GAME_SOCKET %d\n", cuid);
		for (std::list<YOGClient *>::iterator sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->ip.host==ip.host && (*sender)->sharingGame && (*sender)->uid==cuid)
			{
				if ((*sender)->hostGameip.host==0) // Is the game newly ready ?
					for (std::list<YOGClient *>::iterator client=authentifiedClients.begin(); client!=authentifiedClients.end(); ++client)
						(*client)->addGame((*sender)->sharingGame);
				if (!(*sender)->playing)
					for (std::list<YOGClient *>::iterator c=authentifiedClients.begin(); c!=authentifiedClients.end(); ++c)
						(*c)->updateClient(cuid, (Uint16)CUP_PLAYING);
				(*sender)->hostGameip=ip;
				(*sender)->playing=true;
				lprintf("Client (%s) has a hostGameip (%s)\n", (*sender)->userName, Utilities::stringIP(ip));
				if ((*sender)->joinedGame && (*sender)->joinedGame->host!=*sender)
					lprintf("Warning, Client (%s) has not joined his own game!\n", (*sender)->userName);
				(*sender)->send(YMT_HOST_GAME_SOCKET);
				break;
			}
	}
	break;
	case YMT_JOIN_GAME_SOCKET:
	{
		Uint32 cuid=getUint32(data, 4);//Client UID
		Uint32 guid=getUint32(data, 8);//Game UID
		lprintf("YMT_JOIN_GAME_SOCKET cuid=%d guid=%d\n", cuid, guid);
		for (std::list<YOGClient *>::iterator sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->ip.host==ip.host && (*sender)->uid==cuid)
			{
				if (!(*sender)->playing)
					for (std::list<YOGClient *>::iterator c=authentifiedClients.begin(); c!=authentifiedClients.end(); ++c)
						(*c)->updateClient(cuid, (Uint16)CUP_PLAYING);
				(*sender)->joinGameip=ip;
				(*sender)->playing=true;
				lprintf(" Client (%s) has a joinGameip (%s)\n", (*sender)->userName, Utilities::stringIP(ip));
				if ((*sender)->sharingGame && (*sender)->sharingGame->host!=*sender)
					lprintf(" Warning, Client (%s) has not joined his own game!\n", (*sender)->userName);
				
				bool found=false;
				for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); ++game)
					if ((*game)->uid==guid)
					{
						(*sender)->joinedGame=*game;
						(*game)->joiners.remove(*sender);
						(*game)->joiners.push_back(*sender);
						(*game)->host->joiners.remove(*sender);
						(*game)->host->joiners.push_back(*sender);
						int size=(*game)->host->joiners.size();
						if (size>=16)
							(*game)->host->joinersTimeout=0;
						else
							(*game)->host->joinersTimeout=16-size;
						lprintf(" joiners.size=%d, host->joiners.size=%d\n", (*game)->joiners.size(), (*game)->host->joiners.size());
						found=true;
						break;
					}
				if (found)
					(*sender)->send(YMT_JOIN_GAME_SOCKET);
				break;
			}
	}
	break;
	case YMT_PLAYERS_WANTS_TO_JOIN:
	{
		bool good=false; // is he connected ?
		std::list<YOGClient *>::iterator sender;
		for (sender=authentifiedClients.begin(); sender!=authentifiedClients.end(); ++sender)
			if ((*sender)->hasip(ip) && (*sender)->sharingGame)
			{
				(*sender)->TOTL=3;
				good=true;
				break;
			}
		if (good)
		{
			int n=(size-4)/4;
			if (n*4+4!=size)
			{
				lprintf("warning, bad YMT_PLAYERS_WANTS_TO_JOIN packet!\n");
				break;
			}
			int index=4;
			lprintf("(%s) received the %d players wants to join packet\n", (*sender)->userName, n);
			for (int i=0; i<n; i++)
			{
				Uint32 uid=getUint32(data, index);
				index+=4;
				std::list<YOGClient *> &joiners=(*sender)->joiners;
				for (std::list<YOGClient *>::iterator ji=joiners.begin(); ji!=joiners.end(); ++ji)
					if ((*ji)->uid==uid)
					{
						joiners.erase(ji);
						lprintf(" received the (%s) wants to join packet uid=(%d)\n", (*ji)->userName, uid);
						break;
					}
			}
		}
	}
	break;
	}
}

void YOGServer::run()
{
	running=true;
	lprintf("YOGServer is up and running.\n");
	while (running)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(YOG_MAX_PACKET_SIZE);
		assert(packet);

		while (SDLNet_UDP_Recv(socket, packet)==1)
		{
			/*lprintf("Packet received.\n");
			lprintf("packet=%d\n", (int)packet);
			lprintf("packet->channel=%d\n", packet->channel);
			lprintf("packet->len=%d\n", packet->len);
			lprintf("packet->maxlen=%d\n", packet->maxlen);
			lprintf("packet->status=%d\n", packet->status);
			lprintf("packet->address=%x,%d\n", packet->address.host, packet->address.port);
			lprintf("SDLNet_ResolveIP(ip)=%s\n", SDLNet_ResolveIP(&packet->address));*/
			lprintf("packet->data=[%d.%d.%d.%d]\n", packet->data[0], packet->data[1], packet->data[2], packet->data[3]);
			treatPacket(packet->address, packet->data, packet->len);
		}
		SDLNet_FreePacket(packet);
		
		// We look for resending events:
		for (std::list<YOGClient *>::iterator client=authentifiedClients.begin(); client!=authentifiedClients.end(); ++client)
		{
			YOGClient *c=*client;
			if (c->messages.size()>0 && c->messageTimeout--<=0)
			{
				std::list<Message>::iterator mit=c->messages.begin();
				if (c->messageTOTL--<=0)
				{
					lprintf("unable to deliver message (%s) to (%s)\n", mit->text, c->userName);
					c->messages.erase(mit);
					break;
				}
				else
				{
					c->messageTimeout=DEFAULT_NETWORK_TIMEOUT;
					c->send(*mit);
				}
			}
			
			if (c->privateReceipts.size()>0 && c->receiptTimeout--<=0)
			{
				std::list<PrivateReceipt>::iterator rit=c->privateReceipts.begin();
				if (c->receiptTOTL--<=0)
				{
					lprintf("unable to deliver receipt to (%s)\n", c->userName);
					for (std::list<char *>::iterator message=rit->awayMessages.begin(); message!=rit->awayMessages.end(); message++)
						free(*message);
					c->privateReceipts.erase(rit);
					break;
				}
				else
				{
					c->receiptTimeout=DEFAULT_NETWORK_TIMEOUT;
					c->send(*rit);
				}
			}
			
			if (c->sharingGame==NULL && c!=admin && c->games.size()>0)
			{
				if (c->gamesSize>0 && c->gamesTimeout--<=0 )
				{
					if (c->gamesTOTL--<=0)
					{
						lprintf("unable to deliver %d games to (%s)\n", c->games.size(), c->userName);
						c->gamesClear();
						break;
					}
					else
					{
						c->gamesTimeout=DEFAULT_NETWORK_TIMEOUT;
						c->sendGames();
					}
				}
				else
					c->computeGamesSize();
			}
			
			if (c->sharingGame==NULL && c!=admin && c->unshared.size()>0 && c->unsharedTimeout--<=0 )
			{
				if (c->unsharedTOTL--<=0)
				{
					lprintf("unable to deliver unshared games to (%s)\n", c->userName);
					c->unshared.clear();
					break;
				}
				else
					c->sendUnshared();
				c->unsharedTimeout=DEFAULT_NETWORK_TIMEOUT;
			}
			
			if (c->sharingGame==NULL && c!=admin && c->clients.size()>0 && c->clientsTimeout--<=0 )
			{
				if (c->clientsTOTL--<=0)
				{
					lprintf("unable to deliver %d clients to (%s)\n", c->clients.size(), c->userName);
					c->clients.clear();
					break;
				}
				else
					c->sendClients();
				c->clientsTimeout=DEFAULT_NETWORK_TIMEOUT;
			}
			
			if (c->sharingGame==NULL && c!=admin && c->clientsUpdates.size()>0 && c->clientsUpdatesTimeout--<=0)
			{
				if (c->clientsUpdatesTOTL--<=0)
				{
					lprintf("unable to deliver leftClients to (%s)\n", c->userName);
					c->clientsUpdates.clear();//TODO: handle situation?
					break;
				}
				else
					c->sendClientsUpdates();
				c->clientsUpdatesTimeout=DEFAULT_NETWORK_TIMEOUT;
			}
			
			if (c->sharingGame && c->joiners.size() && c->joinersTimeout--<0)
			{
				int n=c->joiners.size();
				int size=4+n*10;
				Uint8 data[size];
				data[0]=YMT_PLAYERS_WANTS_TO_JOIN;
				data[1]=0;
				data[2]=0;
				data[3]=0;
				int index=4;
				std::list<YOGClient *> &joiners=c->joiners;
				for (std::list<YOGClient *>::iterator ji=joiners.begin(); ji!=joiners.end(); ++ji)
				{
					addUint32(data, (*ji)->uid, index);
					index+=4;
					addUint32(data, (*ji)->joinGameip.host, index);
					index+=4;
					addUint16(data, (*ji)->joinGameip.port, index);
					index+=2;
					lprintf("(%s)", (*ji)->userName);
				}
				lprintf(" wants to join game to (%s).\n", c->userName);
				assert(index==size);
				c->send(data, size);
				c->joinersTimeout=DEFAULT_NETWORK_TIMEOUT;
			}
			
		}
		
		// We look for disconnected clients:
		for (std::list<YOGClient *>::iterator client=authentifiedClients.begin(); client!=authentifiedClients.end(); ++client)
		{
			YOGClient *c=*client;
			if (c->timeout--<=0)
			{
				if (c->TOTL--<=0)
				{
					lprintf("Client %s timed out.\n", (*client)->userName);
					deconnectClient(*client);
					authentifiedClients.erase(client);
					unconnectedClients.push_back(*client);
					break;
				}
				else
					c->timeout=LONG_NETWORK_TIMEOUT;
			}
		}
		
		
		SDL_Delay(40);
	}
	lprintf("YOGServer is down and exiting.\n");
}


void YOGServer::lprintf(const char *msg, ...)
{
	va_list arglist;
	char output[256];

	va_start(arglist, msg);
	vsnprintf(output, 256, msg, arglist);
	va_end(arglist);
	output[255]=0;
	if (strncmp(YOG_SERVER_IP, "192.168", 7)==0)
		printf("%s", output);
	
	if (logServerFile)
	{
		fputs(output, logServerFile);
		int fflushRv=fflush(logServerFile);
		assert(fflushRv==0);
	}
	
	int i;
	for (i=0; i<256; i++)
		if (output[i]=='\n' || output[i]==0)
		{
			output[i]=0;
			break;
		}
	if (admin)
		admin->send(YMT_ADMIN_MESSAGE, (Uint8 *)output, i+1);
}

int main(int argc, char *argv[])
{
	admin=NULL;
	logServerFile=fopen("YOGServer.log", "w");
	
	YOGServer yogServer;
	if (yogServer.init())
		yogServer.run();
	else
		return 1;
	return 0;
}
