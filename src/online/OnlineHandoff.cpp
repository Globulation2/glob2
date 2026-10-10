// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineHandoff.h"
#include "InstanceConfig.h"
#include "InviteLink.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace Online
{
namespace
{
struct Handoffs
{
	MatchHandler match;
	std::optional<MatchAssignment> pendingMatch;
	RoomMapHandler roomMap;
	std::optional<RoomMapChoice> pendingRoomMap;
	std::optional<MapPlayRequest> pendingMapPlay;
	RematchHandler rematch;
	QueueAgainHandler queueAgain;
};
Handoffs &handoffs()
{
	static Handoffs value;
	return value;
}
} // namespace

void setMatchHandler(MatchHandler handler)
{
	auto &h = handoffs();
	h.match = std::move(handler);
	if (h.match && h.pendingMatch)
	{
		auto pending = std::move(*h.pendingMatch);
		h.pendingMatch.reset();
		h.match(pending);
	}
}

bool beginMatch(const MatchAssignment &assignment)
{
	auto &h = handoffs();
	if (!h.match)
	{
		h.pendingMatch = assignment;
		return false;
	}
	h.pendingMatch.reset();
	h.match(assignment);
	return true;
}

std::optional<MatchAssignment> takePendingMatch()
{
	auto &h = handoffs();
	auto pending = std::move(h.pendingMatch);
	h.pendingMatch.reset();
	return pending;
}

void setRoomMapHandler(RoomMapHandler handler)
{
	auto &h = handoffs();
	h.roomMap = std::move(handler);
	if (h.roomMap && h.pendingRoomMap)
	{
		auto pending = std::move(*h.pendingRoomMap);
		h.pendingRoomMap.reset();
		h.roomMap(pending);
	}
}

bool useMapInRoom(const RoomMapChoice &choice)
{
	auto &h = handoffs();
	if (!h.roomMap)
	{
		h.pendingRoomMap = choice;
		return false;
	}
	h.pendingRoomMap.reset();
	h.roomMap(choice);
	return true;
}

std::optional<RoomMapChoice> takePendingRoomMap()
{
	auto &h = handoffs();
	auto pending = std::move(h.pendingRoomMap);
	h.pendingRoomMap.reset();
	return pending;
}

const std::optional<RoomMapChoice> &pendingRoomMap()
{
	return handoffs().pendingRoomMap;
}

bool validCatalogMap(const RoomMapChoice &map)
{
	if (map.mapId.size() != 36 || map.hash.size() != 64)
		return false;
	for (std::size_t i = 0; i < map.mapId.size(); ++i)
		if ((i == 8 || i == 13 || i == 18 || i == 23) ? map.mapId[i] != '-' : !std::isxdigit(static_cast<unsigned char>(map.mapId[i])))
			return false;
	return std::all_of(map.hash.begin(), map.hash.end(), [](unsigned char c) { return std::isxdigit(c); });
}

void setPendingMapPlay(const MapPlayRequest &request)
{
	clearPendingJoin();
	handoffs().pendingRoomMap.reset();
	handoffs().pendingMapPlay = request;
}

const std::optional<MapPlayRequest> &pendingMapPlay()
{
	return handoffs().pendingMapPlay;
}

std::optional<MapPlayRequest> takePendingMapPlay()
{
	auto result = std::move(handoffs().pendingMapPlay);
	handoffs().pendingMapPlay.reset();
	return result;
}

void setRematchHandler(RematchHandler handler)
{
	handoffs().rematch = std::move(handler);
}

bool requestRematch(const RematchRequest &request)
{
	auto &h = handoffs();
	if (!h.rematch)
		return false;
	h.rematch(request);
	return true;
}

void setQueueAgainHandler(QueueAgainHandler handler)
{
	handoffs().queueAgain = std::move(handler);
}

bool requestQueueAgain()
{
	auto &h = handoffs();
	if (!h.queueAgain)
		return false;
	h.queueAgain();
	return true;
}
} // namespace Online
