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


#include "../src/Marshaling.h"
#include "YOGServer.h"
#include "../src/Utilities.h"

FILE *logServer;
YOGClient *admin;

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
	for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
		delete (*client);
	for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); ++game)
		delete (*game);
}

bool YOGServer::init()
{
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

void YOGServer::executeCommand(YOGClient *sender, char *s)
{
	if (s[0]!='/')
		return;
	switch (s[1])
	{
		case 'w':
		case 'm':
		{
			if ((s[2]==' ') || ((s[2]=='s') && (s[3]=='g') && (s[4]==' ')))
			{
				int n;
				for (n=0; n<32; n++)
					if (s[n+3]==' ' || s[n+3]==0)
						break;
				char userName[32];
				memcpy(userName, s+3, n);
				userName[n]=0;
				
				// We look for a client with this userName:
				bool found=false;
				std::list<YOGClient *>::iterator client;
				for (client=clients.begin(); client!=clients.end(); ++client)
					if (strncmp((*client)->userName, userName, 32)==0)
						{
							found=true;
							break;
						}
				if (!found)
					return;
				
				Message m;
				int l;
				l=strmlen(s+n+4, 256-n-4);
				memcpy(m.text, s+n+4, l);
				m.text[l-1]=0;
				m.textLength=l;

				l=strmlen(sender->userName, 32);
				memcpy(m.userName, sender->userName, l);
				m.userName[l-1]=0;
				m.userNameLength=l;
				
				m.messageType=YMT_PRIVATE_MESSAGE;
				m.messageID=++(*client)->lastMessageID;
				(*client)->messages.push_back(m);
				
				l=strmlen((*client)->userName, 32);
				memcpy(m.userName, (*client)->userName, l);
				m.userName[l-1]=0;
				m.userNameLength=l;
				m.messageType=YMT_PRIVATE_RECEIPT;
				m.messageID=++sender->lastMessageID;//TODO: we could save a lot of brandwith if we clervery uses "lastMessageID".
				sender->messages.push_back(m);
			}
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
		assert(host->joinedGame==game);
	
	Uint32 uid=game->uid;
	std::list<YOGClient *> &joiners=game->joiners;
	for (std::list<YOGClient *>::iterator c=joiners.begin(); c!=joiners.end(); ++c)
	{
		assert((*c)->joinedGame==game);
		(*c)->joinedGame=NULL;
		lprintf("removed (%s) frome game (%s).\n", (*c)->userName, game->name);
	}
	
	for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
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
	for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
	{
		(*c)->removeClient(uid);
		(*c)->removeUselessClients();
	}

	if (client==admin)
		admin=NULL;
	delete client;
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
		if (size>8+32 ||  size <8+2)
		{
			lprintf("bad YMT_CONNECTING message size (%d).\n", size);
			break;
		}
		bool connected=false;
		bool sameName=false;
		Uint32 uid=0;
		Uint32 protocolVersion=getUint32(data, 4);
		bool compatibleProtocol=(protocolVersion==YOG_PROTOCOL_VERSION);
		
		std::list<YOGClient *>::iterator client;
		for (client=clients.begin(); client!=clients.end(); ++client)
			if ((*client)->hasip(ip))
			{
				(*client)->TOTL=3;
				uid=(*client)->uid;
				connected=true;
			}
			else if (strncmp((*client)->userName, (char *)data+8, 32)==0)
				sameName=true;
		
		if (!connected && !sameName && compatibleProtocol)
		{
			char userName[32];
			memset(userName, 0, 32);
			strncpy(userName, (char *)data+8, 32);
			userName[31]=0;
			
			YOGClient *yogClient=new YOGClient(ip, socket, userName);
			
			for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
				yogClient->addClient(*c);
			
			clients.push_back(yogClient);
			
			for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); game++)
				yogClient->addGame(*game);
			
			for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
				(*c)->addClient(yogClient);
			
			uid=yogClient->uid;
			
			if (data[1]==1)
			{
				admin=yogClient;
				lprintf("new admin connected as (%s), from (%s)\n", userName, Utilities::stringIP(ip));
			}
			else
				lprintf("new client connected as (%s), from (%s), uid=(%d)\n", userName, Utilities::stringIP(ip), uid);
		}
		
		if (sameName || !compatibleProtocol)
		{
			Uint8 data[8];
			data[0]=YMT_CONNECTION_REFUSED;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			
			data[4]=sameName;
			data[5]=YOG_PROTOCOL_VERSION;
			data[6]=0;
			data[7]=0;
			send(ip, data, 8);
		}
		else
		{
			Uint8 data[8];
			data[0]=YMT_CONNECTING;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			addSint32(data, uid, 4);
			send(ip, data, 8);
		}
	}
	break;
	case YMT_DECONNECTING:
	{
		send(ip, YMT_DECONNECTING);
		std::list<YOGClient *>::iterator client;
		for (client=clients.begin(); client!=clients.end(); ++client)
			if ((*client)->hasip(ip))
			{
				lprintf("client \"%s\" deconnected.\n", (*client)->userName);
				deconnectClient(*client);
				clients.erase(client);
				break;
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
		for (sender=clients.begin(); sender!=clients.end(); ++sender)
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
				executeCommand(c, s);
			}
			else
			{
				Message m;
				int l;

				l=strmlen(s, 256);
				memcpy(m.text, s, l);
				m.text[l-1]=0;
				m.textLength=l;

				l=strmlen(c->userName, 32);
				memcpy(m.userName, c->userName, l);
				m.userName[l-1]=0;
				m.userNameLength=l;
				if (c==admin)
					m.messageType=YMT_ADMIN_MESSAGE;
				else
					m.messageType=YMT_MESSAGE;
				
				// Here we have to send this message to all clients!
				lprintf("%d:%d %s:%s\n", m.textLength, m.userNameLength, m.userName, m.text);
				for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
					if ((*client!=admin) || (c!=admin))
					{
						if ((*client)->messages.size()<(256-2))
						{
							m.messageID=++(*client)->lastMessageID;
							(*client)->messages.push_back(m);
						}
						else
							lprintf("Client %s is being flooded!\n", (*client)->userName);
					}
			}
		}
	}
	break;
	case YMT_MESSAGE:
	{
		if (size!=4)
		{
			lprintf("bad message size (%d).\n", size);
			break;
		}
		Uint8 messageID=data[1];
		bool good=false;
		std::list<YOGClient *>::iterator sender;
		for (sender=clients.begin(); sender!=clients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true; // ok, he's connected
				break;
			}
		if (good && (*sender)->messages.size()>0)
		{
			std::list<Message>::iterator mit=(*sender)->messages.begin();
			if (mit->messageID==messageID)
			{
				lprintf("message (%s) delivered to (%s)\n", mit->text, (*sender)->userName);
				(*sender)->messages.erase(mit);
				(*sender)->messageTOTL=3;
				(*sender)->messageTimeout=1;//TODO:here you can improve the TCP/IP friendlyness
			}
		}
	}
	break;
	case YMT_SHARING_GAME:
	{
		if (size>4+128)
		{
			lprintf("bad message size (%d).\n", size);
			break;
		}
		
		bool good=false;
		bool dosend=false;
		std::list<YOGClient *>::iterator sender;
		for (sender=clients.begin(); sender!=clients.end(); ++sender)
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
			memset(game->name, 128, 0);
			strncpy(game->name, (char *)data+4, 128);
			if (game->name[127]!=0)
				lprintf("warning, non-zero ending game name!\n");
			game->name[127]=0;
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
			char data[8];
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
		for (sender=clients.begin(); sender!=clients.end(); ++sender)
			if ((*sender)->hasip(ip) && (*sender)->sharingGame)
			{
				(*sender)->TOTL=3;
				good=true;
				break;
			}
		if (good)
		{
			lprintf("%s stop hosting the game called %s.\n", (*sender)->userName, (*sender)->sharingGame->name);
			removeGame(*sender);
		}
		else
			lprintf("allready stoped sharing the game!\n");
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
		for (sender=clients.begin(); sender!=clients.end(); ++sender)
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
		for (sender=clients.begin(); sender!=clients.end(); ++sender)
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
		if (size!=4)
		{
			lprintf("bad client list size (%d).\n", size);
			break;
		}
		
		bool good=false; // is he connected ?
		std::list<YOGClient *>::iterator sender;
		for (sender=clients.begin(); sender!=clients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true;
				break;
			}
		if (good)
		{
			YOGClient *client=*sender;
			int nbClients=(int)data[1];
			lprintf("client %s received a %d client list.\n", client->userName, nbClients);
			for (int i=0; i<nbClients; i++)
			{
				std::list<YOGClient *>::iterator c=client->clients.begin();
				//TODO: add a system which prevents assert(c!=client->clients.end());
				if (c==client->clients.end())
				{
					lprintf("critical error, i=%d, userName=%s nbClients=%d.\n", i, client->userName, nbClients);
					break;
				}
				client->clients.erase(c);
			}
		}
	}
	break;
	case YMT_LEFT_CLIENTS_LIST:
	{
		if (size!=8)
		{
			lprintf("bad left client list size (%d).\n", size);
			break;
		}
		
		bool good=false; // is he connected ?
		std::list<YOGClient *>::iterator sender;
		for (sender=clients.begin(); sender!=clients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true;
				break;
			}
		if (good)
		{
			YOGClient *client=*sender;
			
			Uint32 leftClientPacketID=getUint32(data, 4);
			Uint32 diff=client->leftClientPacketID-leftClientPacketID;
			if (diff==0 || diff==1 || diff==2 || diff==3)
			{
				int rri=leftClientPacketID&0x3;
				lprintf("client %s received a %d left client list.\n", client->userName, client->lastLeftClientsSent[rri].size());
				for (std::list<Uint32>::iterator uid=client->lastLeftClientsSent[rri].begin(); uid!=client->lastLeftClientsSent[rri].end(); uid++)
				{
					client->leftClients.remove(*uid);
					//lprintf("removed uid=%d.\n", *uid);
				}
			}
			else
				lprintf("warning! client %s sent a boguous left client list. (lcpi=%d). (c->lcpi=%d)\n", client->userName, leftClientPacketID, client->leftClientPacketID);
			
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
		for (std::list<YOGClient *>::iterator sender=clients.begin(); sender!=clients.end(); ++sender)
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
			if (logServer)
				fclose(logServer);
			send(ip, YMT_FLUSH_FILES);
			char s[128];
			snprintf(s, 128, "YOGServer%d.log", id);
			logServer=fopen(s, "w");
		}
		else
		{
			fclose(logServer);
			logServer=NULL;
			lprintf("logServer closed\n");
		}
	}
	break;
	case YMT_CLOSE_YOG:
	{
		int id=data[1];
		if (size!=8 || data[4]!=0x12 || data[5]!=(0x23&id) || data[6]!=(0x34|id) || data[7]!=0x45)
		{
			lprintf("Warning closing temptative (%d)\n", size);
			break;
		}
		if (id)
		{
			send(ip, YMT_CLOSE_YOG);
			std::list<YOGClient *>::iterator client;
			for (client=clients.begin(); client!=clients.end(); ++client)
				 (*client)->send(YMT_CLOSE_YOG);
			running=false;
		}
	}
	break;
	case YMT_HOST_GAME_SOCKET:
	{
		Uint32 cuid=getUint32(data, 4);
		lprintf("YMT_HOST_GAME_SOCKET %d\n", cuid);
		for (std::list<YOGClient *>::iterator sender=clients.begin(); sender!=clients.end(); ++sender)
			if ((*sender)->ip.host==ip.host && (*sender)->sharingGame && (*sender)->uid==cuid)
			{
				if ((*sender)->hostGameip.host==0) // Is the game newly ready ?
					for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
						(*client)->addGame((*sender)->sharingGame);
				(*sender)->hostGameip=ip;
				lprintf("Client %s has a hostGameip (%s)\n", (*sender)->userName, Utilities::stringIP(ip));
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
		for (std::list<YOGClient *>::iterator sender=clients.begin(); sender!=clients.end(); ++sender)
			if ((*sender)->ip.host==ip.host && (*sender)->uid==cuid)
			{
				(*sender)->joinGameip=ip;
				lprintf("Client %s has a joinGameip (%s)\n", (*sender)->userName, Utilities::stringIP(ip));
				
				(*sender)->send(YMT_JOIN_GAME_SOCKET);
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
						break;
					}
				break;
			}
	}
	break;
	case YMT_PLAYERS_WANTS_TO_JOIN:
	{
		bool good=false; // is he connected ?
		std::list<YOGClient *>::iterator sender;
		for (sender=clients.begin(); sender!=clients.end(); ++sender)
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
						lprintf("(%s) received the (%s) wants to join packet uid=(%d)\n", (*sender)->userName, (*ji)->userName, uid);
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
			lprintf("SDLNet_ResolveIP(ip)=%s\n", SDLNet_ResolveIP(&packet->address));
			lprintf("packet->data=[%d.%d.%d.%d]\n", packet->data[0], packet->data[1], packet->data[2], packet->data[3]);*/
			treatPacket(packet->address, packet->data, packet->len);
		}
		SDLNet_FreePacket(packet);
		
		// We look for resending events:
		for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
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
				if (c->unsharedTOTL--<=0)
				{
					lprintf("unable to deliver unshared games to (%s)\n", c->userName);
					c->unshared.clear();
					break;
				}
				else
				{
					c->unsharedTimeout=DEFAULT_NETWORK_TIMEOUT;
					c->sendUnshared();
				}
			
			
			if (c->sharingGame==NULL && c!=admin && c->clients.size()>0 && c->clientsTimeout--<=0 )
				if (c->clientsTOTL--<=0)
				{
					lprintf("unable to deliver %d clients to (%s)\n", c->clients.size(), c->userName);
					c->clients.clear();
					break;
				}
				else
				{
					c->clientsTimeout=DEFAULT_NETWORK_TIMEOUT;
					c->sendClients();
				}
			
			if (c->sharingGame==NULL && c!=admin)
			{
				if (c->leftClients.size()>0)
				{
					if (c->leftClientsTimeout--<=0)
					{
						if (c->leftClientsTOTL--<=0)
						{
							lprintf("unable to deliver leftClients to (%s)\n", c->userName);
							c->leftClients.clear();
							break;
						}
						else
						{
							c->leftClientsTimeout=DEFAULT_NETWORK_TIMEOUT;
							c->sendLeftClients();
						}
					}
				}
				else
				{
					c->leftClientsTimeout=0;
					c->leftClientsTOTL=3;
				}
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
		for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
		{
			YOGClient *c=*client;
			if (c->timeout--<=0)
			{
				if (c->TOTL--<=0)
				{
					lprintf("Client %s timed out.\n", (*client)->userName);
					deconnectClient(*client);
					clients.erase(client);
					break;
				}
				else
					c->timeout=LONG_NETWORK_TIMEOUT;
			}
		}
			
		
		SDL_Delay(50);
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
	//printf("%s", output);
	
	if (logServer)
		fputs(output, logServer);
	
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

int YOGServer::strmlen(const char *s, int max)
{
	for (int i=0; i<max; i++)
		if (*(s+i)==0)
			return i+1;
	return max;
}

int main(int argc, char *argv[])
{
	admin=NULL;
	logServer=fopen("YOGServer.log", "w");
	
	YOGServer yogServer;
	if (yogServer.init())
		yogServer.run();
	else
		return 1;
	return 0;
}
