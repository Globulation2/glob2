/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
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
#include "SDLGraphicContext.h" // for SDL_GetTicks64 fallback
#include <iostream>
#include "boost/lexical_cast.hpp"

using namespace GAGCore;


NetBroadcaster::NetBroadcaster(LANGameInformation& info)
	: info(info), timer(10)
{
	enableBroadcasting();
}


	
NetBroadcaster::~NetBroadcaster()
{
	disableBroadcasting();
}


	
void NetBroadcaster::broadcast(LANGameInformation& ainfo)
{
	info = ainfo;
}


	
void NetBroadcaster::update()
{
	if(socket)
	{
		Uint64 time = SDL_GetTicks64();
		if((time - lastTime) >= 500 )
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
			int result = SDLNet_UDP_Send(socket, 0, packet);
			if(!result)
			{
				printf("SDLNet_UDP_Send: %s\n", SDLNet_GetError());
			}
			
			result = SDLNet_UDP_Send(localsocket, 0, packet);
			if(!result)
			{
				printf("SDLNet_UDP_Send: %s\n", SDLNet_GetError());
			}
			

			delete bos;
			SDLNet_FreePacket(packet);
			
			lastTime = lastTime + 500;
			timer -= 1;
		}
	}
}



void NetBroadcaster::disableBroadcasting()
{
	SDLNet_UDP_Unbind(socket, 0);
	SDLNet_UDP_Close(socket);
	
	SDLNet_UDP_Unbind(localsocket, 0);
	SDLNet_UDP_Close(localsocket);
}



void NetBroadcaster::enableBroadcasting()
{
	socket=SDLNet_UDP_Open(0);
	if(!socket)
	{
		printf("SDLNet_UDP_Open: %s\n", SDLNet_GetError());
		exit(2);
	}
	localsocket=SDLNet_UDP_Open(0);
	if(!localsocket)
	{
		printf("SDLNet_UDP_Open: %s\n", SDLNet_GetError());
		exit(2);
	}
	IPaddress address;
	address.port = LAN_BROADCAST_PORT;
	//192.168.255.255
	address.host = 0xFFFFA8C0;
	SDLNet_UDP_Bind(socket, 0, &address);
	
	IPaddress localaddress;
	SDLNet_ResolveHost(&localaddress, "127.0.0.1", LAN_BROADCAST_PORT);
	SDLNet_UDP_Bind(localsocket, 0, &localaddress);
	
	lastTime = SDL_GetTicks64();
}


