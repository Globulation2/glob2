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

using namespace GAGCore;

NetBroadcastListener::NetBroadcastListener()
{
	socket = SDLNet_UDP_Open(0);
}



NetBroadcastListener::~NetBroadcastListener()
{
	SDLNet_UDP_Close(socket);
}



void NetBroadcastListener::update()
{
	UDPpacket* packet = SDLNet_AllocPacket(1024);
	int result = SDLNet_UDP_Recv(socket, packet);
	std::cout<<"result="<<result<<std::endl;
	while(result == 1)
	{
		Uint16 length = SDLNet_Read16(packet->data);
		MemoryStreamBackend* msb = new MemoryStreamBackend(packet->data+2, length);
		msb->seekFromStart(0);
		BinaryInputStream* bis = new BinaryInputStream(msb);

		LANGameInformation info;
		info.decodeData(bis);

		std::cout<<"Recieved a broadcast!"<<std::endl;

		delete bis;
	}
}


