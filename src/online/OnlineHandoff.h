// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "OnlineResources.h"

#include <functional>
#include <optional>
#include <string>

// Hand-offs between online screen groups that are built separately: the
// quick-match screens give a match to the connecting flow, and the map
// browser gives a map to the room screen. The owner of the destination
// registers a handler (the hub, when it starts); a hand-off made while no
// handler is registered is kept until one is.
namespace Online
{
using MatchHandler = std::function<void(const MatchAssignment &)>;
// Registers the connecting flow (relay connection, map download, loading).
// An empty handler unregisters. A pending match is delivered at once.
void setMatchHandler(MatchHandler handler);
// Starts connecting to an assigned match: calls the handler, or keeps the
// assignment for the next handler. Returns whether a handler took it.
bool beginMatch(const MatchAssignment &assignment);
// The assignment no handler has taken yet.
std::optional<MatchAssignment> takePendingMatch();

// A catalog map chosen with "Use in a room": the room selects it as
// RoomMapSelection {kind: "catalog", hash, mapId}.
struct RoomMapChoice
{
	std::string mapId, hash, title;
	std::optional<int> width, height, teamCount;
	std::optional<std::string> scriptDescriptor;
};
using RoomMapHandler = std::function<void(const RoomMapChoice &)>;
void setRoomMapHandler(RoomMapHandler handler);
// Opens (or updates) the player's room with the map; false when no room
// screen is registered (the choice is kept for it).
bool useMapInRoom(const RoomMapChoice &choice);
std::optional<RoomMapChoice> takePendingRoomMap();
// The kept choice without taking it (the next online room the player hosts uses it).
const std::optional<RoomMapChoice> &pendingRoomMap();
// Explicit map launches, separate from the map browser's room hand-off.
struct MapPlayRequest
{
	enum class Mode { Local, Multiplayer };
	RoomMapChoice map;
	std::string origin;
	Mode mode = Mode::Multiplayer;
};
bool validCatalogMap(const RoomMapChoice &map);
void setPendingMapPlay(const MapPlayRequest &request);
const std::optional<MapPlayRequest> &pendingMapPlay();
std::optional<MapPlayRequest> takePendingMapPlay();
// Browser shell / CLI: online play-map or online host-map ID --hash HASH --title TITLE.

// Rematch after a quick match (Q9): an unrated room with the same players.
// The room screen registers how to create it; the results screen calls it.
struct RematchRequest
{
	std::string matchId;
	std::string mapHash;
	std::vector<std::string> accountIds; // the other human players
};
using RematchHandler = std::function<void(const RematchRequest &)>;
void setRematchHandler(RematchHandler handler);
bool requestRematch(const RematchRequest &request);

// "Find another match" on the results of a quick match: the hub searches the
// same queue again once the match has closed. False when no hub is open.
using QueueAgainHandler = std::function<void()>;
void setQueueAgainHandler(QueueAgainHandler handler);
bool requestQueueAgain();
} // namespace Online
