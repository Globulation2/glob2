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
#include "YOGClient.h"

YOGServer::YOGServer()
{
	socket=NULL;
	running=false;
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
		fprintf(logServer, "failed to open socket at port %d.\n", YOG_SERVER_PORT);
		return false;
	}
	return true;
}

void YOGServer::send(IPaddress ip, Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size);
	if (packet==NULL)
	{
		fprintf(logServer, "Failed to allocate packet!\n");
		return;
	}
	packet->len=size;
	memcpy((char *)packet->data, data, size);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		fprintf(logServer, "Failed to send the packet!\n");
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
		fprintf(logServer, "Failed to allocate packet!\n");
		return;
	}
	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		fprintf(logServer, "Failed to send the packet!\n");
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
		fprintf(logServer, "Failed to allocate packet!\n");
		return;
	}
	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		fprintf(logServer, "Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOGServer::sendGameList(YOGClient *client)
{
	int nbGames=0;
	for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); ++game)
		if ((*game)->host->gameip.host && (*game)->host->gameip.port)
			nbGames++;
	int size=(4+2+32+128)*nbGames+4;
	Uint8 data[size];
	addUint32(data, nbGames, 0); // This is redundancy
	int index=4;
	int tot=0;
	for (std::list<Game *>::iterator game=games.begin(); game!=games.end(); ++game)
		if ((*game)->host->gameip.host && (*game)->host->gameip.port)
		{
			addUint32(data, (*game)->host->gameip.host, index);
			index+=4;
			addUint16(data, (*game)->host->gameip.port, index);
			index+=2;
			int l;
			l=strlen((*game)->host->userName)+1;
			assert(l<=32);
			//fprintf(logServer, "(%s)l=%d, ", (*game)->host->userName, l);
			memcpy(data+index, (*game)->host->userName, l);
			index+=l;

			l=strlen((*game)->name)+1;
			//fprintf(logServer, "(%s)l=%d.\n", (*game)->name, l);
			assert(l<=128);
			memcpy(data+index, (*game)->name, l);
			index+=l;
			if (++tot>=16)
				break; //TODO: allow more than 16 shared games !
			fprintf(logServer, ".");
		}
	client->send(YMT_REQUEST_SHARED_GAMES_LIST, data, index);
	fprintf(logServer, "sent %d games list to %s\n", nbGames, client->userName);
}

void YOGServer::treatPacket(IPaddress ip, Uint8 *data, int size)
{
	//fprintf(logServer, "packet type (%d, %d) received by ip=%d.%d.%d.%d port=%d\n", data[0], data[1], (ip.host>>0)&0xFF, (ip.host>>8)&0xFF, (ip.host>>16)&0xFF, (ip.host>>24)&0xFF, ip.port);
	if (data[2]!=0 || data[3]!=0)
	{
		fprintf(logServer, "bad packet\n");
		return;
	}
	switch (data[0])
	{
	case YMT_BAD:
		fprintf(logServer, "bad packet.\n");
	break;
	case YMT_CONNECTING:
	{
		if (size>4+32)
		{
			fprintf(logServer, "bad message size (%d).\n", size);
			break;
		}
		send(ip, YMT_CONNECTING);
		bool connected=false;
		std::list<YOGClient *>::iterator client;
		for (client=clients.begin(); client!=clients.end(); ++client)
			if ((*client)->hasip(ip))
			{
				(*client)->TOTL=3;
				connected=true;
				break;
			}
		if (!connected)
		{
			char userName[32];
			memset(userName, 0, 32);
			strncpy(userName, (char *)data+4, 32);
			if (userName[31]!=0)
				fprintf(logServer, "warning, non-zero ending string!\n");
			userName[31]=0;
			
			YOGClient *yogClient=new YOGClient(ip, socket, userName);
			clients.push_back(yogClient);
			
			fprintf(logServer, "new client connected as \"%s\", from %d.%d.%d.%d:%d\n", userName, (ip.host>>0)&0xFF, (ip.host>>8)&0xFF, (ip.host>>16)&0xFF, (ip.host>>24)&0xFF, ip.port);
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
				fprintf(logServer, "client \"%s\" deconnected.\n", (*client)->userName);
				if ((*client)->sharingGame)
				{
					games.remove((*client)->sharingGame);
					delete (*client)->sharingGame;
				}
					(*client)->sharingGame=NULL;
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
			fprintf(logServer, "bad message size (%d).\n", size);
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
			// Here we have to send this message to all clients!
			YOGClient *c=*sender;
			c->lastSentMessageID=messageID;
			Message m;
			strncpy(m.text, (char *)data+4, 256);
			if (m.text[size-4]!=0)
				fprintf(logServer, "warning, non-zero ending string!\n");
			m.text[size-4]=0;
			m.textLength=size-4;
			m.timeout=0;
			m.TOTL=3;
			strncpy(m.userName, c->userName, 32);
			if (m.userName[31]!=0)
				fprintf(logServer, "warning, non-zero ending userName!\n");
			m.userName[31]=0;
			fprintf(logServer, "%s:%s\n", m.userName, m.text);
			m.userNameLength=strlen(m.userName)+1;
			for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
				if ((*client)->messages.size()<(256-2))
				{
					m.messageID=++(*client)->lastMessageID;
					(*client)->messages.push_back(m);
				}
				else
					fprintf(logServer, "Client %s is being flooded!\n", (*client)->userName);
		}
	}
	break;
	case YMT_MESSAGE:
	{
		if (size!=4)
		{
			fprintf(logServer, "bad message size (%d).\n", size);
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
		if (good)
		{
			std::list<Message>::iterator mit=(*sender)->messages.begin();
			if (mit->messageID==messageID)
			{
				fprintf(logServer, "message (%s) delivered to (%s)\n", mit->text, (*sender)->userName);
				(*sender)->messages.erase(mit);
			}
		}
	}
	break;
	case YMT_SHARING_GAME:
	{
		if (size>4+128)
		{
			fprintf(logServer, "bad message size (%d).\n", size);
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
				fprintf(logServer, "warning, non-zero ending game name!\n");
			game->name[127]=0;
			game->host=host;
			games.push_back(game);
			host->sharingGame=game;
			fprintf(logServer, "%s is hosting a game called %s.\n", host->userName, game->name);
		}
	}
	break;
	case YMT_STOP_SHARING_GAME:
	{
		if (size>4)
		{
			fprintf(logServer, "bad message size (%d).\n", size);
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
			games.erase(game);
			fprintf(logServer, "%s stop hosting the game called %s.\n", host->userName, (*game)->name);
			host->sharingGame=NULL;
			delete (*game);
		}
	}
	break;
	case YMT_REQUEST_SHARED_GAMES_LIST:
	{
		if (size>4)
		{
			fprintf(logServer, "bad message size (%d).\n", size);
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
			sendGameList(*sender);
	}
	break;
	case YMT_CONNECTION_PRESENCE:
	{
		if (size>4)
		{
			fprintf(logServer, "bad message size (%d).\n", size);
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
			fprintf(logServer, "Warning flush temptative (%d)\n", size);
			break;
		}
		if (id)
		{
			fclose(logClient);
			fclose(logServer);
			send(ip, YMT_FLUSH_FILES);
			char s[128];
			snprintf(s, 128, "YOGClient%d.log", id);
			logClient=fopen(s, "w");
			snprintf(s, 128, "YOGServer%d.log", id);
			logServer=fopen(s, "w");
		}
	}
	break;
	case YMT_CLOSE_YOG:
	{
		int id=data[1];
		if (size!=8 || data[4]!=0x12 || data[5]!=(0x23&id) || data[6]!=(0x34|id) || data[7]!=0x45)
		{
			fprintf(logServer, "Warning closing temptative (%d)\n", size);
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
		//send(ip, YMT_GAME_SOCKET);
		bool good=false;
		//send(ip, YMT_GAME_SOCKET);TODO: to which socket do we send it ?
		for (std::list<YOGClient *>::iterator sender=clients.begin(); sender!=clients.end(); ++sender)
			if ((*sender)->ip.host==ip.host && (*sender)->sharingGame)
			{
				//TODO: check username too
				(*sender)->gameip=ip;
				fprintf(logServer, "Client %s has a gameip (%d.%d.%d.%d:%d)\n", (*sender)->userName, (ip.host>>0)&0xFF, (ip.host>>8)&0xFF, (ip.host>>16)&0xFF, (ip.host>>24)&0xFF, ip.port);
				(*sender)->send(YMT_GAME_SOCKET);
				if ((*sender)->gameip.host==0) // is it new ?
					good=true;
				break;
			}
		if (good)
		{
			for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
				sendGameList(*client);
		}
	}
	break;
	}
}

void YOGServer::run()
{
	running=true;
	fprintf(logServer, "YOGServer is up and running.\n");
	while (running)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(YOG_MAX_PACKET_SIZE);
		assert(packet);

		while (SDLNet_UDP_Recv(socket, packet)==1)
		{
			/*fprintf(logServer, "Packet received.\n");
			fprintf(logServer, "packet=%d\n", (int)packet);
			fprintf(logServer, "packet->channel=%d\n", packet->channel);
			fprintf(logServer, "packet->len=%d\n", packet->len);
			fprintf(logServer, "packet->maxlen=%d\n", packet->maxlen);
			fprintf(logServer, "packet->status=%d\n", packet->status);
			fprintf(logServer, "packet->address=%x,%d\n", packet->address.host, packet->address.port);
			fprintf(logServer, "SDLNet_ResolveIP(ip)=%s\n", SDLNet_ResolveIP(&packet->address));
			fprintf(logServer, "packet->data=[%d.%d.%d.%d]\n", packet->data[0], packet->data[1], packet->data[2], packet->data[3]);*/
			treatPacket(packet->address, packet->data, packet->len);
		}
		SDLNet_FreePacket(packet);
		
		// We look for resending events:
		for (std::list<YOGClient *>::iterator client=clients.begin(); client!=clients.end(); ++client)
		{
			YOGClient *c=*client;
			if (c->messages.size()>0)
			{
				std::list<Message>::iterator mit=c->messages.begin();
				if (mit->timeout--<=0)
					if (mit->TOTL--<=0)
					{
						fprintf(logServer, "unable to deliver message (%s) to (%s)\n", mit->text, c->userName);
						c->messages.erase(mit);
						break;
					}
					else
					{
						mit->timeout=DEFAULT_NETWORK_TIMEOUT;
						c->send(*mit);
					}
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
					fprintf(logServer, "Client %s timed out.\n", (*client)->userName);
					if ((*client)->sharingGame)
					{
						games.remove((*client)->sharingGame);
						delete (*client)->sharingGame;
					}
					(*client)->sharingGame=NULL;
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
	fprintf(logServer, "YOGServer is down and exiting.\n");
}

int main(int argc, char *argv[])
{
	//logClient=fopen("YOGClient.log", "w");
	//logServer=fopen("YOGServer.log", "w");
	logClient=stdout;
	logServer=stdout;
	
	YOGServer yogServer;
	if (yogServer.init())
		yogServer.run();
	else
		return 1;
	return 0;
}
