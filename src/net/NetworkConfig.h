// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NetTransport.h"
struct NetworkConfig
{
	NetListenConfig lobby, router, registration;
	std::string lobbyEndpoint, routerEndpoint, registrationEndpoint, discoveryId;
	std::string controlBind = "127.0.0.1";
	uint16_t controlPort = 7492;
	unsigned drainSeconds = 1800;
	bool lan = false;
};
// Validated before any listener opens. LAN mode provisions an in-memory identity.
NetworkConfig makeNetworkConfig(bool lan = false, bool routerRole = false);
