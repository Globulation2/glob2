/*
 *  Ysagoon Online Gaming
 *  Meta Server with chat for Ysagoon game (first is glob2)
 *  (c) 2004 Stéphane Magnenat <<stephane at magnenat dot net>>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <SDL/SDL.h>
#include <SDL/SDL_endian.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_net.h>
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include "../src/YOGConsts.h"
#include <string>
#include <map>
#include <cctype>

// shake player each 20 minuts
#define PLAYER_SHAKING_TIME 20
// react once on 8 times
#define PROB_PSM 7 

namespace Utilities
{
	int strmlen(const char *s, int max)
	{
		for (int i=0; i<max; i++)
			if (*(s+i)==0)
				return i+1;
		return max;
	}
}

namespace simpleClient
{
	UDPsocket socket;
	IPaddress serverIP;
	bool running;
	Uint8 lastMessageID=0;
	int timeout=0;
	int TOTL=3;
	bool connected=false;
	unsigned now=0;
	
	struct PlayerInfo
	{
		unsigned lastLogin; // not used now
		unsigned lastSpoken;
		unsigned lastShaked;
		
		PlayerInfo() { lastLogin=lastSpoken=lastShaked=0; }
	};
	
	typedef std::map<std::string, PlayerInfo> PlayersMap;
	PlayersMap players;
	
	typedef std::map<std::string, std::string> StringMap;
	StringMap sm;
	StringMap psm;
	std::string me;
	
	void initSM()
	{
		sm["yog"] = "Yes, this is YOG, Ysagoon Online Game";
		sm["hello"] = "Hi, how are you today ?";
		sm["hi!"] = "Hello, how do you feel ?";
		sm[" bot"] = "Yes, I'm a bot, but I'm nice :-)";
		sm["robot"] = "Do you like Asimov ?";
		sm[" you"] = "I'm fine, thanks.";
		sm[" bad"] = "I do not like sad things.";
		sm[" faq"] = "You FAQ is http://ysagoon.com/glob2";
		sm[" web"] = "The web site is http://ysagoon.com/glob2";
		sm[" forum"] = "The forum is http://forum.ysagoon.com/?c=2";
		sm[" wiki"] = "The wiki is http://wiki.ysagoon.com/Glob2";
		sm[" globulation"] = "Globulation 1 was cool, but the 2 is better !";
		sm[" glob2"] = "Glob2 is my favourite game ;-)";
		sm[" human"] = "I like humans";
		sm[" game"] = "You should play games on YOG, it is cool";
		sm["tcho"] = "Salut, noble barbare";
		sm["trt"] = "What do you mean by trt, dejan ?";
		sm["good "] = "Fine, thanks !";
		sm["and you"] = "I'm not leaking ;-)";
		sm["is anyone"] = "Yes, I'm here, even if I'm only a bot.";
		sm["is there anyone"] = "Yes, I'm here, I'm a nice bot.";
		sm["chaterYog"] = "I'm chaterYog, perhaps you'll feel happy speaking with me. I'm nice and trustable";
	}
	
	void initPSM()
	{
		psm["lol"] = ";-)";
		psm["play"] = "I am not sufficiently advanced to play.";
	}

	bool init()
	{
		socket=SDLNet_UDP_Open(0);
		if (!socket)
		{
			printf("failed to open a socket!\n");
			return false;
		}

		int rv=SDLNet_ResolveHost(&serverIP, YOG_SERVER_IP, YOG_SERVER_PORT);
		if (rv==-1)
		{
			printf("failed to resolve YOG host name!\n");
			return false;
		}
		
		lastMessageID=0;
		
		return true;
	}
	
	void send(YOGMessageType v, Uint8 *data, int size)
	{
		UDPpacket *packet=SDLNet_AllocPacket(size+4);
		if (packet==NULL)
			printf("Failed to alocate packet!\n");
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
		packet->address=serverIP;
		packet->channel=-1;
		int rv=SDLNet_UDP_Send(socket, -1, packet);
		if (rv!=1)
			printf("Failed to send the packet!\n");
	}
	
	void send(YOGMessageType v, Uint8 id, Uint8 *data, int size)
	{
		UDPpacket *packet=SDLNet_AllocPacket(size+4);
		if (packet==NULL)
			printf("Failed to alocate packet!\n");
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
		packet->address=serverIP;
		packet->channel=-1;
		int rv=SDLNet_UDP_Send(socket, -1, packet);
		if (rv!=1)
			printf("Failed to send the packet!\n");
	}

	void send(YOGMessageType v)
	{
		Uint8 data[4];
		data[0]=v;
		data[1]=0;
		data[2]=0;
		data[3]=0;
		UDPpacket *packet=SDLNet_AllocPacket(4);
		if (packet==NULL)
			printf("Failed to alocate packet!\n");

		packet->len=4;
		memcpy((char *)packet->data, data, 4);
		packet->address=serverIP;
		packet->channel=-1;
		int rv=SDLNet_UDP_Send(socket, -1, packet);
		if (rv!=1)
			printf("Failed to send the packet!\n");
	}

	void send(YOGMessageType v, char id)
	{
		Uint8 data[4];
		data[0]=v;
		data[1]=id;
		data[2]=0;
		data[3]=0;
		UDPpacket *packet=SDLNet_AllocPacket(4);
		if (packet==NULL)
			printf("Failed to alocate packet!\n");

		packet->len=4;
		memcpy((char *)packet->data, data, 4);
		packet->address=serverIP;
		packet->channel=-1;
		int rv=SDLNet_UDP_Send(socket, -1, packet);
		if (rv!=1)
			printf("Failed to send the packet!\n");
	}
	

	void treatPacket(Uint32 ip, Uint16 port, Uint8 *data, int size)
	{
		//printf("packet received by ip=%d.%d.%d.%d port=%d\n", (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, (ip>>0)&0xFF, port);
		if (data[2]!=0 || data[3]!=0)
		{
			printf("bad packet.\n");
			return;
		}
		switch (data[0])
		{
		case YMT_BAD:
			printf("bad packet.\n");
		break;
		case YMT_PRIVATE_MESSAGE:
		case YMT_MESSAGE:
		{
			send(YMT_MESSAGE, data[1]);
			
			std::string msg((char *)data+4);
			int l=Utilities::strmlen((char *)data+4, 256);
			std::string user((char *)data+4+l);
			std::string msgLower = msg;
			transform (msgLower.begin(), msgLower.end(), msgLower.begin(), tolower);
			
			printf("user is %s msg is %s\n", user.c_str(), msg.c_str());
			if (user != me)
			{
				players[user].lastSpoken = now;
				players[user].lastShaked = now;
				
				for (StringMap::const_iterator i=sm.begin(); i!=sm.end(); ++i)
				{
					if (msgLower.find(i->first) != std::string::npos)
					{
						send(YMT_SEND_MESSAGE, lastMessageID++, (Uint8 *)i->second.c_str(), i->second.length()+1);
					}
				}
				
				if (rand()%PROB_PSM)
				{
					for (StringMap::const_iterator i=psm.begin(); i!=psm.end(); ++i)
					{
						if (msgLower.find(i->first) != std::string::npos)
						{
							send(YMT_SEND_MESSAGE, lastMessageID++, (Uint8 *)i->second.c_str(), i->second.length()+1);
						}
					}
				}
			}
		}
		break;
		case YMT_UPDATE_CLIENTS_LIST:
		{
			// TODO
		}
		break;
		case YMT_ADMIN_MESSAGE:
		{
			char s[256];
			strncpy(s, (char *)data+4, 256);
			s[255]=0;
			printf("dump:%s\n", s);
		}
		break;
		case YMT_CONNECTION_PRESENCE:
			TOTL=3;
			timeout=350;
		break;
		case YMT_CONNECTING:
			connected=true;
			printf("connected to YOG.\n");
		break;
		}

	}

	void connect()
	{
		char data[32+4];
		data[0]=0;
		data[1]=0;
		data[2]=0;
		data[3]=4;
		strncpy(data+4, me.c_str(), 32);
		data[31+4]=0;
		send(YMT_CONNECTING, (Uint8 *)data, 32+4);
	}
	
	void shakePlayer(const char *p, const char *s)
	{
		char buffer[256];
		snprintf(buffer, 256, "%s%s", p, s);
		buffer[255]=0;
		send(YMT_SEND_MESSAGE, lastMessageID++, (Uint8 *)buffer, strlen(buffer)+1);
	}
	
	void run()
	{
		running=true;
		while (running)
		{
			UDPpacket *packet=NULL;
			packet=SDLNet_AllocPacket(4+256);
			assert(packet);

			while (SDLNet_UDP_Recv(socket, packet)==1)
			{
				treatPacket(packet->address.host, packet->address.port, packet->data, packet->len);
				/*printf("Packet received.\n");
				printf("packet=%d\n", (int)packet);
				printf("packet->channel=%d\n", packet->channel);
				printf("packet->len=%d\n", packet->len);
				printf("packet->maxlen=%d\n", packet->maxlen);
				printf("packet->status=%d\n", packet->status);
				printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);
				printf("SDLNet_ResolveIP(ip)=%s\n", SDLNet_ResolveIP(&packet->address));*/
				//printf("packet->data=[%d.%d.%d.%d]\n", packet->data[0], packet->data[1], packet->data[2], packet->data[3]);
			}
			
			SDLNet_FreePacket(packet);
			
			if (connected && timeout--<=0)
			{
				timeout=350;
				if (TOTL--<=0)
				{
					printf("YOG is down.\n");
					connected=false;
				}
				else
					send(YMT_CONNECTION_PRESENCE);
			}
			
			for (PlayersMap::iterator i=players.begin(); i!=players.end(); i++)
			{
				if (now > i->second.lastShaked + 25*60*PLAYER_SHAKING_TIME)
				{
					i->second.lastShaked=now;
					
					switch (rand()&3)
					{
						case 0: shakePlayer(i->first.c_str(), ", where are you ?"); break;
						case 1: shakePlayer(i->first.c_str(), ", are you sleeping ?"); break;
						case 2: shakePlayer(i->first.c_str(), ", are you alive ?"); break;
						case 3: shakePlayer(i->first.c_str(), ", please, speak to me :-)"); break;
					}
				}
			}
			
			SDL_Delay(40);
			now ++ ;
		}
	}
	
	void close()
	{
		SDLNet_UDP_Close(socket);
	}
	
}

int main(int argc, char *argv[])
{
	bool good=simpleClient::init();
	
	if (!good)
		return 1;
		
	printf("simpleClient is initialised.\n");
	
	printf("* loading map...\n");
	
	simpleClient::initSM();
	
	simpleClient::initPSM();
	
	printf("* connecting...\n");
	
	if (argc>1)
		simpleClient::me = argv[1];
	else
		simpleClient::me = "chaterYog";
		
	simpleClient::connect();
	
	printf("simpleClient is running.\n");
		
	simpleClient::run();
	
	printf("simpleClient is quitting.\n");
	
	simpleClient::close();
	
	return 0;
}
