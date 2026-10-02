// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineHandoff.h"

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
	RematchHandler rematch;
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
} // namespace Online
