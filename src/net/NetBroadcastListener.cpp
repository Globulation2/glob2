// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetBroadcastListener.h"
#include "NetConsts.h"
#include "NetTransport.h"
#include <SDL3/SDL.h>
#include <Environment.h>
#include <algorithm>
#include <memory>
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
	const auto now = SDL_GetTicks();
	if (socket)
	{
		for (unsigned count = 0; count < 32; ++count)
		{
			NET_Datagram *raw = nullptr;
			if (!NET_ReceiveDatagram(socket, &raw) || !raw)
				break;
			std::unique_ptr<NET_Datagram, decltype(&NET_DestroyDatagram)> packet(
				raw, NET_DestroyDatagram);
			if (packet->buflen <= 36 || packet->buflen > 548)
				continue;
			std::string bytes(reinterpret_cast<char *>(packet->buf), packet->buflen);
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
				const char *text = NET_GetAddressString(packet->addr);
				const std::string sender = text ? text : "";
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
	}
	hosts.erase(std::remove_if(hosts.begin(), hosts.end(),
							   [&](const auto &host) { return now - host.lastSeen > 2000; }),
				hosts.end());
}
void NetBroadcastListener::enableListening()
{
	disableListening();
#ifndef __EMSCRIPTEN__
	NET_Address *ipv4 = NET_ResolveHostname("0.0.0.0");
	if (ipv4 && NET_WaitUntilResolved(ipv4, 1000) == NET_SUCCESS)
		socket = NET_CreateDatagramSocket(ipv4, LAN_BROADCAST_PORT, 0);
	if (ipv4)
		NET_UnrefAddress(ipv4);
#endif
}
void NetBroadcastListener::disableListening()
{
	if (socket)
		NET_DestroyDatagramSocket(socket);
	socket = nullptr;
}
