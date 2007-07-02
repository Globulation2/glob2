/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "NetBroadcaster.h"
#include "NetConsts.h"
#include "Stream.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <iostream>
using namespace GAGCore;


NetBroadcaster::NetBroadcaster(LANGameInformation& info)
	: info(info), timer(0)
{
	socket=SDLNet_UDP_Open(LAN_BROADCAST_PORT);
	if(!socket)
	{
		printf("SDLNet_UDP_Open: %s\n", SDLNet_GetError());
		exit(2);
	}
	IPaddress address;
	address.host = INADDR_BROADCAST;
	address.port = LAN_BROADCAST_PORT;
	SDLNet_UDP_Bind(socket, 1, &address);
}


	
NetBroadcaster::~NetBroadcaster()
{
	SDLNet_UDP_Unbind(socket, 1);
	SDLNet_UDP_Close(socket);
}


	
void NetBroadcaster::broadcast(LANGameInformation& ainfo)
{
	info = ainfo;
}


	
void NetBroadcaster::update()
{
	if(timer == 0)
	{
		MemoryStreamBackend* msb = new MemoryStreamBackend;
		BinaryOutputStream* bos = new BinaryOutputStream(msb);
		info.encodeData(bos);
		
		msb->seekFromEnd(0);
		Uint32 length = msb->getPosition();
		msb->seekFromStart(0);

		UDPpacket* packet = SDLNet_AllocPacket(length+2);
		packet->len = length+2;
		SDLNet_Write16(length, packet->data);
		msb->read(packet->data+2, length);
		int result = SDLNet_UDP_Send(socket, 1, packet);
		if(!result)
		{
			printf("SDLNet_UDP_Send: %s\n", SDLNet_GetError());
		}
		
		delete bos;
		SDLNet_FreePacket(packet);
		timer =40;
	}
	else
	{
		timer -= 1;
	}
}
