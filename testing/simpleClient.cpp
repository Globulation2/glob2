/*
 *  Ysagoon Online Gaming
 *  Meta Server with chat for Ysagoon game (first is glob2)
 *  (c) 2002 Luc-Olivier de Charrière <<NuageBleu at gmail dot com>>
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
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>

#include <SDL/SDL.h>
#include <SDL/SDL_endian.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_net.h>

#include "../src/YOGConsts.h"
#include "../src/Utilities.h"
#include "Marshaling.h"
#include "../gnupg/sha1.c"

namespace simpleClient
{
	UDPsocket socket;
	IPaddress serverIP;
	bool running;
	Uint8 lastMessageID=0;
	int timeout=0;
	int TOTL=3;
	bool connected=false;
	bool authenticated=false;
	char passWord[32];
	bool asAdmin=false;

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
		
		TOTL=3;
		timeout=350;
		
		switch (data[0])
		{
		case YMT_BAD:
			printf("bad packet.\n");
		break;
		case YMT_PRIVATE_MESSAGE:
		case YMT_MESSAGE:
		{
			char s[256];
			strncpy(s, (char *)data+4, 256);
			s[255]=0;
			printf("client:%s\n", s);
			send(YMT_MESSAGE, data[1]);
		}
		break;
		case YMT_ADMIN_MESSAGE:
		{
			char s[256];
			strncpy(s, (char *)data+4, 256);
			s[255]=0;
			printf("%s\n", s);
		}
		break;
		case YMT_CONNECTION_PRESENCE:
		break;
		case YMT_CONNECTING:
			if (size==36)
			{
				connected=true;
				printf("connected to YOG...\n");
				char xorpassw[32];
				memcpy(xorpassw, (char *)data+4, 32);
				
				unsigned char xored[32];
					for (int i=0; i<32; i++)
						xored[i]=passWord[i]^xorpassw[i];
				
				unsigned char computedDigest[20];
				SHA1_CTX context;
				SHA1Init(&context);
				SHA1Update(&context, xored, 32);
				SHA1Final(computedDigest, &context);
				
				printf("authenticating to YOG with asAdmin=%d, passWord=(%s)...\n", asAdmin, passWord);
				if (asAdmin)
					send(YMT_AUTHENTICATING, 3, (Uint8 *)computedDigest, 20);
				else
					send(YMT_AUTHENTICATING, 2, (Uint8 *)computedDigest, 20);
			}
		break;
		case YMT_AUTHENTICATING:
			authenticated=true;
			printf("authenticated with YOG\n");
		break;
		case YMT_CONNECTION_REFUSED:
			printf("YMT_CONNECTION_REFUSED [");
			for (int i=0; i<size; i++)
				printf("%d, ", data[i]);
			printf("]\n");
		break;
		case YMT_DECONNECTING:
			connected=false;
			authenticated=false;
		break;
		case YMT_CLIENTS_LIST:
			int nbClients=(int)data[4];
			Uint8 clientsPacketID=data[5];
			printf("YMT_CLIENTS_LIST nbClients=%d, clientsPacketID=%d\n", nbClients, clientsPacketID);
			int index=6;
			for (int i=0; i<nbClients; i++)
			{
				Uint32 cuid=getUint32(data, index);
				index+=4;
				char *name=(char *)data+index;
				int l=Utilities::strmlen(name, 32);
				index+=l;
				bool playing=(bool)data[index];
				index++;
				bool away=(bool)data[index];
				index++;
				printf(" %d (%s) %d %d\n", cuid, name, playing, away);
			}
			Uint8 data[2];
			data[0]=nbClients;
			data[1]=clientsPacketID;
			send(YMT_CLIENTS_LIST, data, 2);
		break;
		}
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
				/*printf("\nPacket received.\n");
				printf("packet=%d\n", (int)packet);
				printf("packet->channel=%d\n", packet->channel);
				printf("packet->len=%d\n", packet->len);
				printf("packet->maxlen=%d\n", packet->maxlen);
				printf("packet->status=%d\n", packet->status);
				printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);
				printf("SDLNet_ResolveIP(ip)=%s\n", SDLNet_ResolveIP(&packet->address));*/
				printf("packet->data=[%d.%d.%d.%d]\n", packet->data[0], packet->data[1], packet->data[2], packet->data[3]);
				treatPacket(packet->address.host, packet->address.port, packet->data, packet->len);
			}

			// get first timer
			fd_set rfds;
			struct timeval tv;
			int retval;

			// Watch stdin (fd 0) to see when it has input.
			FD_ZERO(&rfds);
			FD_SET(0, &rfds);
			// Wait up to one second.
			tv.tv_sec = 0;
			tv.tv_usec = 0;

			retval = select(1, &rfds, NULL, NULL, &tv);
			// Don't rely on the value of tv now!

			if (retval)
			{
				char token[32][256];
				char s[1024];
				fgets(s, 1024, stdin);
				Utilities::staticTokenize(s, 1024, token);
				if (strcmp(token[0], "bad")==0)
				{
					send(YMT_BAD);
				}
				else if (strcmp(token[0], "admin")==0)
				{
					if (token[1])
					{
						char data[32+4];
						data[0]=0;
						data[1]=0;
						data[2]=0;
						data[3]=YOG_PROTOCOL_VERSION;
						strncpy(data+4, token[1], 32);
						data[4+31]=0;
						asAdmin=true;
						send(YMT_CONNECTING, 1, (Uint8 *)data, 32+4);
						strncpy(passWord, token[2], 32);
					}
				}
				else if (strcmp(token[0], "connect")==0)
				{
					if (token[1])
					{
						char data[32+4];
						data[0]=0;
						data[1]=0;
						data[2]=0;
						data[3]=YOG_PROTOCOL_VERSION;
						strncpy(data+4, token[1], 32);
						data[31+4]=0;
						asAdmin=false;
						send(YMT_CONNECTING, (Uint8 *)data, 32+4);
					}
				}
				else if (strcmp(token[0], "deconnect")==0)
				{
					send(YMT_DECONNECTING);
				}
				else if (strcmp(token[0], "close")==0)
				{
					Uint8 id=1;
					Uint8 data[4];
					data[0]=0x12;
					data[1]=0x23&id;
					data[2]=0x34|id;
					data[3]=0x45;
					send(YMT_CLOSE_YOG, id, data, 4);
				}
				else if (strcmp(token[0], "flush")==0)
				{
					if (token[1])
					{
						char name[32];
						memset(name, 0, 32);
						strncpy(name, token[1], 32);
						name[31]=0;
						Uint8 id=atoi(name);
						Uint8 data[4];
						data[0]=0xAB;
						data[1]=0xCD&id;
						data[2]=0xEF|id;
						data[3]=0x12;
						send(YMT_FLUSH_FILES, id, data, 4);
					}
				}
				else if (strcmp(token[0], "exit")==0)
				{
					running=false;
				}
				else
				{
					lastMessageID++;
					for (int i=0; i<1024; i++)
						if (s[i]=='\n' || s[i]==0)
						{
							s[i]=0;
							break;
						}
					send(YMT_SEND_MESSAGE, lastMessageID, (Uint8 *)s, Utilities::strmlen(s, 256));
				}
			}

			SDLNet_FreePacket(packet);
			
			if (authenticated && timeout--<=0)
			{
				timeout=350;
				if (TOTL--<=0)
				{
					printf("YOG is down.\n");
					connected=false;
					authenticated=false;
				}
				else
					send(YMT_CONNECTION_PRESENCE);
			}
			
			SDL_Delay(40);
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
	
	simpleClient::run();
	
	printf("simpleClient is quitting.\n");
	
	simpleClient::close();
	
	return 0;
}
