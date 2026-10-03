// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "InviteLink.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The client's one connection to the online platform, shared by every
// screen, its instance list and the map cache. The Application owns it for the
// run (ServicesOwner): created on first use, so a session that never goes online
// reads no online/instances.json and opens no socket; pumped every frame by
// Online::pump() so sign-in, token refresh and keepalives continue while the
// player moves between screens; destroyed when the application exits, which
// closes the connection. Online objects take what they need explicitly
// (OnlineMatch and PlatformRoom the map cache, QuickMatch the client); screens
// reach the application's services through services().
namespace Online
{
class InstanceConfig;
class MapCache;
class OnlineStorage;
class PlatformClient;
class QuickMatch;

class Services
{
	// Declared first: the references below bind to them.
	std::unique_ptr<InstanceConfig> ownedConfig;
	std::unique_ptr<PlatformClient> ownedClient;

  public:
	// Loads online/instances.json from `storage`. The client is not started: the
	// hub calls client.start(origin).
	Services(OnlineStorage &storage, MapCache &maps);
	~Services();
	Services(const Services &) = delete;
	Services &operator=(const Services &) = delete;

	OnlineStorage &storage;
	InstanceConfig &config;
	PlatformClient &client;
	MapCache &maps;

	// The quick-match search, which keeps running between screens; created on
	// first use and advanced by update().
	QuickMatch &quickMatch();
	// Advances the client, the quick-match search and the hooks.
	void update();
	// Work that must continue between screens, called by update() after the client.
	using HookId = std::uint64_t;
	HookId addHook(std::function<void()> hook);
	void removeHook(HookId id);

  private:
	std::unique_ptr<QuickMatch> search;
	std::vector<std::pair<HookId, std::function<void()>>> hooks;
	HookId nextHook = 1;
};

// Owns the services for its lifetime; the Application holds one. services()
// returns the newest live owner's, creating them on first use. Without an owner
// (tools and test harnesses) services() falls back to process-lifetime services.
class ServicesOwner
{
  public:
	// `make` creates the services on first use (default: the user directory's
	// online storage and map cache).
	explicit ServicesOwner(std::function<std::unique_ptr<Services>()> make = {});
	~ServicesOwner();
	ServicesOwner(const ServicesOwner &) = delete;
	ServicesOwner &operator=(const ServicesOwner &) = delete;
	Services &get();
	bool created() const { return owned != nullptr; }

  private:
	std::function<std::unique_ptr<Services>()> make;
	std::unique_ptr<Services> owned;
	ServicesOwner *previous = nullptr;
};

// The application's services, created on first call.
Services &services();
bool servicesCreated();
// The user directory's map cache (online/maps/), the same one as
// services().maps, without creating the platform client or reading
// online/instances.json: for LAN games.
MapCache &sharedMapCache();
// Advances the application's services (when created) and picks up invite links
// delivered while running (macOS/iOS URL events arrive as SDL_EVENT_DROP_FILE
// through acceptDroppedText; Android intents and iOS universal links are polled
// here). Cheap when the services were never created.
void pump();
// services().addHook(hook), for work that must continue between screens.
Services::HookId addPumpHook(std::function<void()> hook);
// SDL_EVENT_DROP_FILE text: true (and pending join set) when it is an invite link,
// which the caller then must not treat as a file.
bool acceptDroppedText(const std::string &text);
} // namespace Online
