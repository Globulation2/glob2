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

#include "NetBroadcastListener.h"
#include "NetConsts.h"
#include "Stream.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <iostream>
#include <sstream>

using namespace GAGCore;

NetBroadcastListener::NetBroadcastListener()
{
	socket = SDLNet_UDP_Open(LAN_BROADCAST_PORT);
	lastTime = SDL_GetTicks();
}



NetBroadcastListener::~NetBroadcastListener()
{
	SDLNet_UDP_Close(socket);
}



void NetBroadcastListener::update()
{
	UDPpacket* packet = SDLNet_AllocPacket(1024);
	int result = SDLNet_UDP_Recv(socket, packet);
	while(result == 1)
	{
		Uint16 length = SDLNet_Read16(packet->data);
		MemoryStreamBackend* msb = new MemoryStreamBackend(packet->data+2, length);
		msb->seekFromStart(0);
		BinaryInputStream* bis = new BinaryInputStream(msb);

		LANGameInformation info;
		info.decodeData(bis);
		
		bool found = false;
		for(int i=0; i<addresses.size(); ++i)
		{
			if(addresses[i].host == packet->address.host)
			{
				games[i] = info;
				timeouts[i] = 1500;
				found = true;
				break;
			}
		}

		if(!found)
		{
			games.push_back(info);
			timeouts.push_back(1500);
			addresses.push_back(packet->address);
		}
		
		delete bis;
		result = SDLNet_UDP_Recv(socket, packet);
	}
	
	int time = SDL_GetTicks() - lastTime;
	for(int i=0; i<timeouts.size();)
	{
		timeouts[i] -= time;
		if(timeouts[i] <= 0)
		{
			timeouts.erase(timeouts.begin() + i);
			games.erase(games.begin() + i);
			addresses.erase(addresses.begin() + i);
		}
		else
		{
			++i;
		}
	}
	lastTime = SDL_GetTicks();
}


const std::vector<LANGameInformation>& NetBroadcastListener::getLANGames()
{
	return games;
}



std::string NetBroadcastListener::getIPAddress(size_t num)
{
	std::stringstream s;
	Uint8* address = reinterpret_cast<Uint8*>(&addresses[num].host);
	s<<int(address[0])<<".";
	s<<int(address[1])<<".";
	s<<int(address[2])<<".";
	s<<int(address[3]);
	return s.str();
}

