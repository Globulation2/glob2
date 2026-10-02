// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetBroadcaster.h"
#include "NetConsts.h"
#include "NetTransport.h"
#include <SDL3/SDL.h>
#include <Environment.h>
#include <stdexcept>
NetBroadcaster::NetBroadcaster(const std::string &identifier, const std::string &pairing)
{
	auto endpoint = pairing.substr(0, pairing.find('#'));
	if (identifier.size() != 32 || endpoint.size() > 512 ||
		NetEndpoint::parse(endpoint).route != "/yog")
		throw std::invalid_argument("Invalid LAN discovery identity");
	beacon = "G2D1" + identifier + endpoint;
	enableBroadcasting();
}
NetBroadcaster::~NetBroadcaster()
{
	disableBroadcasting();
}
void NetBroadcaster::update()
{
	const auto now = SDL_GetTicks();
	if ((!socket && broadcastSockets.empty()) || now - lastTime < 500)
		return;
	lastTime = now;
	for (auto *broadcastSocket : broadcastSockets)
		NET_SendDatagram(broadcastSocket, nullptr, LAN_BROADCAST_PORT, beacon.data(),
						 beacon.size());
	if (socket && localaddress)
		NET_SendDatagram(socket, localaddress, LAN_BROADCAST_PORT, beacon.data(), beacon.size());
}
void NetBroadcaster::disableBroadcasting()
{
	for (auto *broadcastSocket : broadcastSockets)
		NET_DestroyDatagramSocket(broadcastSocket);
	broadcastSockets.clear();
	if (socket)
		NET_DestroyDatagramSocket(socket);
	if (localaddress)
		NET_UnrefAddress(localaddress);
	socket = nullptr;
	localaddress = nullptr;
}

void NetBroadcaster::enableBroadcasting()
{
#ifdef __EMSCRIPTEN__
	// Browser transport is WebSocket-only; no native LAN datagram support.
	return;
#endif
	disableBroadcasting();
	// SDL_net requires a concrete interface for an IPv4-only broadcast socket.
	// Binding nullptr would also enable IPv6 multicast discovery, whose protocol
	// is deliberately outside this migration.
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetBooleanProperty(props, NET_PROP_DATAGRAM_SOCKET_ALLOW_BROADCAST_BOOLEAN, true);
	NET_Address **addresses = NET_GetLocalAddresses(nullptr);
	if (addresses)
	{
		for (auto **address = addresses; *address; ++address)
		{
			const char *text = NET_GetAddressString(*address);
			if (!text || SDL_strchr(text, ':') || SDL_strncmp(text, "127.", 4) == 0)
				continue;
			if (auto *broadcastSocket = NET_CreateDatagramSocket(*address, 0, props))
				broadcastSockets.push_back(broadcastSocket);
		}
	}
	NET_FreeLocalAddresses(addresses);
	SDL_DestroyProperties(props);
	localaddress = NET_ResolveHostname("127.0.0.1");
	if (localaddress && NET_WaitUntilResolved(localaddress, 1000) != NET_SUCCESS)
	{
		NET_UnrefAddress(localaddress);
		localaddress = nullptr;
	}
	if (localaddress)
		socket = NET_CreateDatagramSocket(localaddress, 0, 0);
	lastTime = SDL_GetTicks();
}
