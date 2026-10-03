// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "InviteLink.h"

#include <string>

// The client's one connection to the online platform, shared by every
// screen: created on first use, pumped every frame by Online::pump() (called
// from the application loop) so sign-in, token refresh and keepalives continue
// while the player moves between screens.
namespace Online
{
class InstanceConfig;
class MapCache;
class OnlineStorage;
class PlatformClient;

struct Services
{
	OnlineStorage &storage;
	InstanceConfig &config;
	PlatformClient &client;
	MapCache &maps;
};

// Creates the services on first call (loading online/instances.json). The
// client is not started: the hub calls client.start(origin).
Services &services();
bool servicesCreated();
// Advances the client and picks up invite links delivered while running
// (macOS/iOS URL events arrive as SDL_EVENT_DROP_FILE through acceptDroppedText;
// Android intents and iOS universal links are polled here). Cheap when the
// services were never created.
void pump();
// SDL_EVENT_DROP_FILE text: true (and pending join set) when it is an invite link,
// which the caller then must not treat as a file.
bool acceptDroppedText(const std::string &text);
} // namespace Online
