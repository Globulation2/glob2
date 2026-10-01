// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetBroadcastListener.h"
#include "NetConsts.h"
#include "NetTransport.h"
#include "SDLCompat.h"
#include <algorithm>
NetBroadcastListener::NetBroadcastListener()
{
	enableListening();
}
NetBroadcastListener::~NetBroadcastListener()
{
	disableListening();
}
void NetBroadcastListener::update()
{
	const auto now = SDL_GetTicks64();
	if (socket)
	{
		UDPpacket *packet = SDLNet_AllocPacket(549);
		if (packet)
		{
			// Limit packet processing so discovery cannot starve the UI/game loop.
			for (unsigned count = 0; count < 32 && SDLNet_UDP_Recv(socket, packet) == 1; ++count)
			{
				if (packet->len <= 36 || packet->len > 548)
					continue;
				std::string bytes(reinterpret_cast<char *>(packet->data), packet->len);
				if (bytes.substr(0, 4) != "G2D1")
					continue;
				const auto id = bytes.substr(4, 32), url = bytes.substr(36);
				if (id.find_first_not_of("0123456789abcdef") != std::string::npos ||
					url.find('#') != std::string::npos)
					continue;
				try
				{
					const auto endpoint = NetEndpoint::parse(url);
					if (endpoint.route != "/yog")
						continue;
					const auto *ip = reinterpret_cast<const unsigned char *>(&packet->address.host);
					auto sender = std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." +
								  std::to_string(ip[2]) + "." + std::to_string(ip[3]);
					if (endpoint.host != sender)
						continue;
					auto found =
						std::find_if(hosts.begin(), hosts.end(), [&](const auto &host)
									 { return host.identifier == id && host.endpoint == url; });
					if (found != hosts.end())
						found->lastSeen = now;
					else if (hosts.size() < 64)
						hosts.push_back({id, url, now});
				}
				catch (...)
				{
				}
			}
			SDLNet_FreePacket(packet);
		}
	}
	hosts.erase(std::remove_if(hosts.begin(), hosts.end(),
							   [&](const auto &host) { return now - host.lastSeen > 2000; }),
				hosts.end());
}
void NetBroadcastListener::enableListening()
{
	disableListening();
	socket = SDLNet_UDP_Open(LAN_BROADCAST_PORT);
}
void NetBroadcastListener::disableListening()
{
	if (socket)
		SDLNet_UDP_Close(socket);
	socket = nullptr;
}
