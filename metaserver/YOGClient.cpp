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

YOGClient::YOGClient(IPaddress ip, UDPsocket socket, char userName[32])
{
	this->ip=ip;
	gameip.host=0;
	gameip.port=0;
	this->socket=socket;
	memcpy(this->userName, userName, 32);
	
	lastSentMessageID=0;
	lastMessageID=0;
	
	sharingGame=NULL;
	
	timeout=0;
	TOTL=3;
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
		fprintf(logClient, "Failed to allocate packet!\n");

	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		fprintf(logClient, "Failed to send the packet!\n");
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
		fprintf(logClient, "Failed to allocate packet!\n");

	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		fprintf(logClient, "Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOGClient::send(Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size);
	if (packet==NULL)
		fprintf(logClient, "Failed to allocate packet!\n");

	packet->len=size;
	memcpy((char *)packet->data, data, size);
	packet->address=ip;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		fprintf(logClient, "Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOGClient::send(YOGMessageType v, Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size+4);
	if (packet==NULL)
	{
		fprintf(logClient, "Failed to allocate packet!\n");
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
		fprintf(logClient, "Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOGClient::send(YOGMessageType v, Uint8 id, Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size+4);
	if (packet==NULL)
	{
		fprintf(logClient, "Failed to allocate packet!\n");
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
		fprintf(logClient, "Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOGClient::send(const Message &m)
{
	int size=4+m.textLength+m.userNameLength;
	UDPpacket *packet=SDLNet_AllocPacket(size);
	if (packet==NULL)
	{
		fprintf(logClient, "Failed to allocate packet!\n");
		return;
	}
	packet->len=size;
	{
		Uint8 data[4];
		data[0]=YMT_MESSAGE;
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
		fprintf(logClient, "Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}
