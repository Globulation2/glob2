// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "NetBroadcastListener.h"
#include "NetConsts.h"
#include "Order.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <exception>

using namespace GAGCore;

NetBroadcastListener::NetBroadcastListener()
{
	enableListening();
	lastTime = SDL_GetTicks();
}



NetBroadcastListener::~NetBroadcastListener()
{
	disableListening();
}



void NetBroadcastListener::update()
{
	if(socket)
	{

        NET_Datagram *packet = nullptr;
        while (NET_ReceiveDatagram(socket, &packet) && packet) {
            if (packet->buflen < NET_FRAME_LENGTH_PREFIX_BYTES) {
                NET_DestroyDatagram(packet); packet = nullptr; continue;
            }
            const unsigned length = (unsigned(packet->buf[0]) << 8) | packet->buf[1];
            if (!length || length != unsigned(packet->buflen - NET_FRAME_LENGTH_PREFIX_BYTES)) {
                NET_DestroyDatagram(packet); packet = nullptr; continue;
            }
            const char *text = NET_GetAddressString(packet->addr);
            const std::string address = text ? text : "";
            if (address.empty() || address.find(':') != std::string::npos) {
                NET_DestroyDatagram(packet); packet = nullptr; continue;
            }
            try {
                auto *msb = new MemoryStreamBackend(packet->buf + NET_FRAME_LENGTH_PREFIX_BYTES, length);
                BinaryInputStream bis(msb);
                LANGameInformation info;
                info.decodeData(&bis);
                auto found = std::find(addresses.begin(), addresses.end(), address);
                if (found == addresses.end()) {
                    addresses.push_back(address); games.push_back(info); timeouts.push_back(1500);
                } else {
                    const auto index = found - addresses.begin();
                    games[index] = info; timeouts[index] = 1500;
                }
            } catch (const std::exception &) {
                // Ignore malformed advertisements without disrupting discovery.
            }
            NET_DestroyDatagram(packet); packet = nullptr;
        }

		Uint64 time = std::max<Sint64>(0, static_cast<Sint64>(SDL_GetTicks()) - static_cast<Sint64>(lastTime));
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
		lastTime = SDL_GetTicks();
	}
}


const std::vector<LANGameInformation>& NetBroadcastListener::getLANGames()
{
	return games;
}



std::string NetBroadcastListener::getIPAddress(size_t num)
{
    return addresses.at(num);
}

void NetBroadcastListener::enableListening()
{
#ifdef __EMSCRIPTEN__
    // Browser transport is WebSocket-only; no native LAN datagram support.
    return;
#endif
    disableListening();
    NET_Address *ipv4 = NET_ResolveHostname("0.0.0.0");
    if (ipv4 && NET_WaitUntilResolved(ipv4, 1000) == NET_SUCCESS)
        socket = NET_CreateDatagramSocket(ipv4, LAN_BROADCAST_PORT, 0);
    if (ipv4) NET_UnrefAddress(ipv4);
}

void NetBroadcastListener::disableListening()
{
    if (socket) NET_DestroyDatagramSocket(socket);
    socket = nullptr;
}
