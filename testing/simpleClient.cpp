/*
 *  Ysagoon Online Gaming
 *  Meta Server with chat for Ysagoon game (first is glob2)
 *  (c) 2002 Luc-Olivier de Charrière <nuage@ysagoon.com>
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

#define YOG_PORT 7008
#define YOG_HOST "192.168.1.5"
#define ANY_PORT 0

namespace simpleClient
{

	enum TO_YOG_DATA_TYPE
	{
		TOY_BAD=0,
		TOY_CONNECTION_REQUEST=1,
		TOY_DECONNECTION_REQUEST=2,
		TOY_CLOSE_YOG=3
	};

	enum FROM_YOG_DATA_TYPE
	{
		FOY_BAD=0,
		FOY_CONNECTED=1,
		FOY_DECONNECTED=2
	};

	UDPsocket socket;
	IPaddress serverIP;
	bool running;

	bool init()
	{
		socket=SDLNet_UDP_Open(0);
		if (!socket)
		{
			printf("failed to open a socket!\n");
			return false;
		}

		int rv=SDLNet_ResolveHost(&serverIP, YOG_HOST, YOG_PORT);
		if (rv==-1)
		{
			printf("failed to resolve YOG host name!\n");
			return false;
		}

		return true;
	}

	void send(Uint32 ip, Uint16 port, Uint8 *data, int size)
	{
		UDPpacket *packet=SDLNet_AllocPacket(size);
		if (packet==NULL)
			printf("Failed to alocate packet!\n");

		packet->len=size;
		memcpy((char *)packet->data, data, size);
		packet->address.host=ip;
		packet->address.port=port;
		packet->channel=-1;
		int rv=SDLNet_UDP_Send(socket, -1, packet);
		if (rv!=1)
			printf("Failed to send the packet!\n");
	}

	void send(Uint32 ip, Uint16 port, char v)
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
		packet->address.host=ip;
		packet->address.port=port;
		packet->channel=-1;
		int rv=SDLNet_UDP_Send(socket, -1, packet);
		if (rv!=1)
			printf("Failed to send the packet!\n");
	}

	void send(char v)
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

	void treatPacket(Uint32 ip, Uint16 port, Uint8 *data, int size)
	{
		/*printf("packet received by ip=%d.%d.%d.%d port=%d\n", (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, (ip>>0)&0xFF, port);
		if (data[1]!=0 || data[2]!=0 || data[3]!=0)
		{
			printf("bad packet.\n");
			return;
		}
		switch (data[0])
		{
		case TOY_BAD:
			printf("bad packet.\n");
		break;
		case TOY_CONNECTION_REQUEST:
			send(ip, port, FOY_CONNECTED);
		break;
		case TOY_DECONNECTION_REQUEST:
			send(ip, port, FOY_DECONNECTED);
		break;
		case TOY_CLOSE_YOG:
			running=false;
		break;
		}*/

	}

	void run()
	{
		running=true;
		while (running)
		{
			UDPpacket *packet=NULL;
			packet=SDLNet_AllocPacket(8);
			assert(packet);

			while (SDLNet_UDP_Recv(socket, packet)==1)
			{
				treatPacket(packet->address.host, packet->address.port, packet->data, packet->len);
				printf("Packet received.\n");
				printf("packet=%d\n", (int)packet);
				printf("packet->channel=%d\n", packet->channel);
				printf("packet->len=%d\n", packet->len);
				printf("packet->maxlen=%d\n", packet->maxlen);
				printf("packet->status=%d\n", packet->status);
				printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);
				printf("SDLNet_ResolveIP(ip)=%s\n", SDLNet_ResolveIP(&packet->address));
				printf("packet->data=[%d.%d.%d.%d]\n", packet->data[0], packet->data[1], packet->data[2], packet->data[3]);
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
				char s[32];
				fgets(s, 32, stdin);
				int l=strlen(s);
				if ((l>1)&&(s[l-1]=='\n'))
					s[l-1]=0;

				if (strncmp(s, "bad", 3)==0)
				{
					send(TOY_BAD);
				}
				else if (strncmp(s, "connect", 7)==0)
				{
					send(TOY_CONNECTION_REQUEST);
				}
				else if (strncmp(s, "deconnect", 9)==0)
				{
					send(TOY_DECONNECTION_REQUEST);
				}
				else if (strncmp(s, "close", 9)==0)
				{
					send(TOY_CLOSE_YOG);
				}
				else if (strncmp(s, "exit", 9)==0)
				{
					running=false;
				}
				else
					printf("Glob2::runHostServer():not understood (%s).\n", s);
			}

			SDLNet_FreePacket(packet);
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
