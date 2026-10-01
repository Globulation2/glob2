// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetBroadcaster.h"
#include "NetConsts.h"
#include "NetTransport.h"
#include "SDLCompat.h"
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
	const auto now = SDL_GetTicks64();
	if (!socket || now - lastTime < 500)
		return;
	lastTime = now;
	auto *packet = SDLNet_AllocPacket(beacon.size());
	if (!packet)
		return;
	packet->len = beacon.size();
	std::copy(beacon.begin(), beacon.end(), packet->data);
	SDLNet_ResolveHost(&packet->address, "255.255.255.255", LAN_BROADCAST_PORT);
	SDLNet_UDP_Send(socket, -1, packet);
	SDLNet_FreePacket(packet);
}
void NetBroadcaster::disableBroadcasting()
{
	if (socket)
		SDLNet_UDP_Close(socket);
	socket = nullptr;
}
void NetBroadcaster::enableBroadcasting()
{
	disableBroadcasting();
	socket = SDLNet_UDP_Open(0);
	lastTime = SDL_GetTicks64();
}
