// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "NetBroadcaster.h"
#include "NetConsts.h"
#include "Order.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <SDL3/SDL.h>
#include <vector>

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
		Uint64 time = SDL_GetTicks();
		if((static_cast<Sint64>(time) - static_cast<Sint64>(lastTime)) >= 500 )
		{
			MemoryStreamBackend* msb = new MemoryStreamBackend;
			BinaryOutputStream* bos = new BinaryOutputStream(msb);
			info.encodeData(bos);
			
			msb->seekFromEnd(0);
			Uint32 length = msb->getPosition();
			msb->seekFromStart(0);


            if (length <= 65535) {
                std::vector<Uint8> packet(length + NET_FRAME_LENGTH_PREFIX_BYTES);
                packet[0] = length >> 8; packet[1] = length & 255;
                msb->read(packet.data() + NET_FRAME_LENGTH_PREFIX_BYTES, length);
                NET_SendDatagram(socket, nullptr, LAN_BROADCAST_PORT, packet.data(), packet.size());
                if (localaddress) NET_SendDatagram(socket, localaddress, LAN_BROADCAST_PORT, packet.data(), packet.size());
            }
            delete bos;

			lastTime = lastTime + 500;
			timer -= 1;
		}
	}
}



void NetBroadcaster::disableBroadcasting()
{
    if (socket) NET_DestroyDatagramSocket(socket);
    if (localaddress) NET_UnrefAddress(localaddress);
    socket = nullptr; localaddress = nullptr;
}

void NetBroadcaster::enableBroadcasting()
{
#ifdef __EMSCRIPTEN__
    // Browser transport is WebSocket-only; no native LAN datagram support.
    return;
#endif
    disableBroadcasting();
    NET_Address *ipv4 = NET_ResolveHostname("0.0.0.0");
    if (ipv4 && NET_WaitUntilResolved(ipv4, 1000) == NET_SUCCESS) {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetBooleanProperty(props, NET_PROP_DATAGRAM_SOCKET_ALLOW_BROADCAST_BOOLEAN, true);
        socket = NET_CreateDatagramSocket(ipv4, 0, props);
        SDL_DestroyProperties(props);
    }
    if (ipv4) NET_UnrefAddress(ipv4);
    localaddress = NET_ResolveHostname("127.0.0.1");
    if (localaddress && NET_WaitUntilResolved(localaddress, 1000) != NET_SUCCESS) {
        NET_UnrefAddress(localaddress); localaddress = nullptr;
    }
    lastTime = SDL_GetTicks();
}
