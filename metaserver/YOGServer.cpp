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

#include "YOGServer.h"

YOGServer::YOGServer()
{
	socket=NULL;
	running=false;
}

YOGServer::~YOGServer()
{
	if (socket)
		SDLNet_UDP_Close(socket);
}

bool YOGServer::init()
{
	socket=SDLNet_UDP_Open(YOG_SERVER_PORT);
	if (!socket)
	{
		printf("failed to open socket at port %d.\n", YOG_SERVER_PORT);
		return false;
	}
	return true;
}

void YOGServer::send(IPaddress ip, Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size);
	if (packet==NULL)
		printf("Failed to alocate packet!\n");

	packet->len=size;
	memcpy((char *)packet->data, data, size);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		printf("Failed to send the packet!\n");
}

void YOGServer::send(IPaddress ip, char v)
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
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		printf("Failed to send the packet!\n");
}

void YOGServer::treatPacket(IPaddress ip, Uint8 *data, int size)
{
	printf("packet received by ip=%d.%d.%d.%d port=%d\n", (ip.host>>24)&0xFF, (ip.host>>16)&0xFF, (ip.host>>8)&0xFF, (ip.host>>0)&0xFF, ip.port);
	if (data[1]!=0 || data[2]!=0 || data[3]!=0)
	{
		printf("bad packet.\n");
		return;
	}
	switch (data[0])
	{
	case YMT_BAD:
		printf("bad packet.\n");
	break;
	case YMT_CONNECTING:
	{
		send(ip, YMT_CONNECTING);
		YOGClient *yogClient=new YOGClient(ip, socket);
		clients.push_back(yogClient);
	}
	break;
	case YMT_DECONNECTING:
	{
		send(ip, YMT_DECONNECTING);
		std::vector<YOGClient *>::iterator client;
		for (client=clients.begin(); client!=clients.end(); ++client)
			if ((*client)->ip.host==ip.host && (*client)->ip.port==ip.port)
			{
				clients.erase(client);
				break;
			}
	}
	break;
	case YMT_CLOSE_YOG:
		//send(ip, YMT_CLOSE_YOG);
		std::vector<YOGClient *>::iterator client;
		for (client=clients.begin(); client!=clients.end(); ++client)
			 (*client)->send(YMT_CLOSE_YOG);
		running=false;
	break;
	}
}

void YOGServer::run()
{
	running=true;
	printf("YOGServer is up and running.\n");
	while (running)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(8);
		assert(packet);

		while (SDLNet_UDP_Recv(socket, packet)==1)
		{
			/*printf("Packet received.\n");
			printf("packet=%d\n", (int)packet);
			printf("packet->channel=%d\n", packet->channel);
			printf("packet->len=%d\n", packet->len);
			printf("packet->maxlen=%d\n", packet->maxlen);
			printf("packet->status=%d\n", packet->status);
			printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);
			printf("SDLNet_ResolveIP(ip)=%s\n", SDLNet_ResolveIP(&packet->address));*/
			printf("packet->data=[%d.%d.%d.%d]\n", packet->data[0], packet->data[1], packet->data[2], packet->data[3]);
			treatPacket(packet->address, packet->data, packet->len);
		}
		SDLNet_FreePacket(packet);
	}
	printf("YOGServer is down and exiting.\n");
}


int main(int argc, char *argv[])
{
	YOGServer yogServer;
	if (yogServer.init())
		yogServer.run();
	else
		return 1;
	return 0;
}
