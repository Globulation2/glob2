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


#include "../src/Marshaling.h"
#include "YOGServer.h"

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
			if (s[2]==' ')
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
				
				(*client)->messages.push_back(m);
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

void YOGServer::treatPacket(IPaddress ip, Uint8 *data, int size)
{
	//lprintf("packet type (%d, %d) received by ip=%d.%d.%d.%d port=%d\n", data[0], data[1], (ip.host>>0)&0xFF, (ip.host>>8)&0xFF, (ip.host>>16)&0xFF, (ip.host>>24)&0xFF, ip.port);
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
		if (size>4+32)
		{
			lprintf("bad message size (%d).\n", size);
			break;
		}
		bool connected=false;
		bool sameName=false;
		std::list<YOGClient *>::iterator client;
		for (client=clients.begin(); client!=clients.end(); ++client)
			if ((*client)->hasip(ip))
			{
				(*client)->TOTL=3;
				connected=true;
			}
			else if (strncmp((*client)->userName, (char *)data+4, 32)==0)
				sameName=true;
		
		if (sameName)
			send(ip, YMT_CONNECTION_REFUSED);
		else
			send(ip, YMT_CONNECTING);
		
		if (!connected && !sameName)
		{
			char userName[32];
			memset(userName, 0, 32);
			strncpy(userName, (char *)data+4, 32);
			userName[31]=0;
			
			YOGClient *yogClient=new YOGClient(ip, socket, userName);
			
			for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
				yogClient->addClient(*c);
			
			clients.push_back(yogClient);
			
			for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); game++)
				yogClient->addGame(*game);
			
			for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
				(*c)->addClient(yogClient);
			
			
			if (data[1]==1)
			{
				admin=yogClient;
				lprintf("new admin connected as \"%s\", from %d.%d.%d.%d:%d\n", userName, (ip.host>>0)&0xFF, (ip.host>>8)&0xFF, (ip.host>>16)&0xFF, (ip.host>>24)&0xFF, ip.port);
			}
			else
				lprintf("new client connected as \"%s\", from %d.%d.%d.%d:%d\n", userName, (ip.host>>0)&0xFF, (ip.host>>8)&0xFF, (ip.host>>16)&0xFF, (ip.host>>24)&0xFF, ip.port);
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
				if ((*client)->sharingGame)
				{
					Uint32 uid=(*client)->sharingGame->uid;
					for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
					{
						(*c)->removeGame(uid);
						(*c)->removeUselessGames();
					}
					games.remove((*client)->sharingGame);
					delete (*client)->sharingGame;
				}
				(*client)->sharingGame=NULL;
				
				Uint32 uid=(*client)->uid;
				for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
				{
					(*c)->removeClient(uid);
					(*c)->removeUselessClients();
				}
				
				if (*client==admin)
					admin=NULL;
				delete (*client);
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
		if (good & (*sender)->messages.size()>0)
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
		send(ip, YMT_SHARING_GAME); // We allways confirm client that we received the packet.
		
		bool good=false;
		std::list<YOGClient *>::iterator sender;
		for (sender=clients.begin(); sender!=clients.end(); ++sender)
			if ((*sender)->hasip(ip))
			{
				(*sender)->TOTL=3;
				good=true; // ok, he's connected
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
			game->uid=gameUID++;
			games.push_back(game);
			host->sharingGame=game;
			lprintf("%s is hosting a game called %s.\n", host->userName, game->name);
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
			YOGClient *host=*sender;
			std::list<Game *>::iterator game;
			bool good=false;
			for (game=games.begin(); game!=games.end(); ++game)
				if ((*game)->host==host)
				{
					good=true;
					break;
				}
			assert(good); // This is a database consistency.
			Uint32 uid=(*game)->uid;
			for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
			{
				(*c)->removeGame(uid);
				(*c)->removeUselessGames();
			}
			games.erase(game);
			lprintf("%s stop hosting the game called %s.\n", host->userName, (*game)->name);
			host->sharingGame=NULL;
			host->gameip.host=0;
			delete (*game);
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
				assert(c!=client->clients.end());
				client->clients.erase(c);
			}
		}
	}
	break;
	case YMT_LEFT_CLIENTS_LIST:
	{
		if (size!=4)
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
			int nbClients=(int)data[1];
			lprintf("client %s received a %d left client list.\n", client->userName, nbClients);
			for (int i=0; i<nbClients; i++)
			{
				std::list<Uint32>::iterator uid=client->leftClients.begin();
				assert(uid!=client->leftClients.end());
				client->leftClients.erase(uid);
			}
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
	case YMT_GAME_SOCKET:
	{
		//send(ip, YMT_GAME_SOCKET);TODO: to which socket do we send it ?
		for (std::list<YOGClient *>::iterator sender=clients.begin(); sender!=clients.end(); ++sender)
			if ((*sender)->ip.host==ip.host && (*sender)->sharingGame)//TODO: check username too?
			{
				if ((*sender)->gameip.host==0) // is it new ?
					for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
						(*client)->addGame((*sender)->sharingGame);
				(*sender)->gameip=ip;
				lprintf("Client %s has a gameip (%d.%d.%d.%d:%d)\n", (*sender)->userName, (ip.host>>0)&0xFF, (ip.host>>8)&0xFF, (ip.host>>16)&0xFF, (ip.host>>24)&0xFF, ip.port);
				(*sender)->send(YMT_GAME_SOCKET);
				break;
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
			
			
			if (c->sharingGame==NULL && c!=admin && c->games.size()>0 && c->gamesTimeout--<=0 )
				if (c->gamesTOTL--<=0)
				{
					lprintf("unable to deliver %d games to (%s)\n", c->games.size(), c->userName);
					c->games.clear();
					break;
				}
				else
				{
					c->gamesTimeout=DEFAULT_NETWORK_TIMEOUT;
					c->sendGames();
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
			
			if (c->sharingGame==NULL && c!=admin && c->leftClients.size()>0 && c->leftClientsTimeout--<=0 )
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
		
		// We look for disconnected clients:
		for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
		{
			YOGClient *c=*client;
			if (c->timeout--<=0)
			{
				if (c->TOTL--<=0)
				{
					lprintf("Client %s timed out.\n", (*client)->userName);
					if ((*client)->sharingGame)
					{
						Uint32 uid=(*client)->sharingGame->uid;
						for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
						{
							(*c)->removeGame(uid);
							(*c)->removeUselessGames();
						}
						games.remove((*client)->sharingGame);
						delete (*client)->sharingGame;
					}
					(*client)->sharingGame=NULL;
					
					Uint32 uid=(*client)->uid;
					for (std::list<YOGClient *>::iterator c=clients.begin(); c!=clients.end(); ++c)
					{
						(*c)->removeClient(uid);
						(*c)->removeUselessClients();
					}
					
					if (*client==admin)
						admin=NULL;
					delete (*client);
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
	
	if (logServer)
		fputs(output, logServer);
	//printf(output);
	
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
