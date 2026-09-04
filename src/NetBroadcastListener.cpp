// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "NetBroadcastListener.h"
#include "NetConsts.h"
#include "Stream.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <SDLCompat.h>
#include <iostream>
#include <sstream>

using namespace GAGCore;

NetBroadcastListener::NetBroadcastListener()
{
	enableListening();
	lastTime = SDL_GetTicks64();
}



NetBroadcastListener::~NetBroadcastListener()
{
	disableListening();
}



void NetBroadcastListener::update()
{
	if(socket)
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
			for(unsigned int i=0; i<addresses.size(); ++i)
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
		
		Uint64 time = std::max<Sint64>(0, static_cast<Sint64>(SDL_GetTicks64()) - static_cast<Sint64>(lastTime));
		for(unsigned int i=0; i<timeouts.size();)
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
		lastTime = SDL_GetTicks64();
	}
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



void NetBroadcastListener::enableListening()
{
	socket = SDLNet_UDP_Open(LAN_BROADCAST_PORT);
}



void NetBroadcastListener::disableListening()
{
	SDLNet_UDP_Close(socket);
}



