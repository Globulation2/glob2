// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineHandoff.h"

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

int acceptRoomMapArguments(int argc, char **argv, int index)
{
	if (std::string(argv[index]) != "--room-map")
		return 0;
	if (index + 3 >= argc)
		return argc - index;
	RoomMapChoice choice;
	choice.mapId = argv[index + 1];
	choice.hash = argv[index + 2];
	choice.title = std::string(argv[index + 3]).substr(0, 128);
	const bool hex = choice.hash.size() == 64 &&
					 std::all_of(choice.hash.begin(), choice.hash.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
	if (hex && !choice.mapId.empty() && choice.mapId.size() <= 64)
		useMapInRoom(choice);
	else
		std::fprintf(stderr, "Ignoring invalid --room-map\n");
	return 4;
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
