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
};
using RoomMapHandler = std::function<void(const RoomMapChoice &)>;
void setRoomMapHandler(RoomMapHandler handler);
// Opens (or updates) the player's room with the map; false when no room
// screen is registered (the choice is kept for it).
bool useMapInRoom(const RoomMapChoice &choice);
std::optional<RoomMapChoice> takePendingRoomMap();
// The kept choice without taking it (the next online room the player hosts uses it).
const std::optional<RoomMapChoice> &pendingRoomMap();
// "Play this map" from the web: `--room-map <mapId> <hash> <title>` keeps the catalog
// map for the next room. Returns how many arguments were consumed (0: not ours).
int acceptRoomMapArguments(int argc, char **argv, int index);

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
} // namespace Online
