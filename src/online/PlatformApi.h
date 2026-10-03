// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The platform's REST paths (platform/apps/api, docs/multiplayer/platform.md),
// built in one place: the /api/v1 prefix and the encoding of path segments
// live here, not in screens. Every function returns a path relative to the
// instance origin, for PlatformClient::rest()/restRaw() or apiUrl().
namespace Online
{
// Percent-encodes everything but RFC 3986 unreserved characters.
std::string urlEncode(const std::string &text);

namespace Api
{
inline constexpr const char *PREFIX = "/api/v1";

inline std::string path(const std::string &rest)
{
	return std::string(PREFIX) + rest;
}
inline std::string instance()
{
	return path("/instance");
}
inline std::string accountMe()
{
	return path("/accounts/me");
}
inline std::string accountIdentity(const std::string &provider)
{
	return path("/accounts/me/identities/" + urlEncode(provider));
}
inline std::string authGuest()
{
	return path("/auth/guest");
}
inline std::string authRefresh()
{
	return path("/auth/refresh");
}
inline std::string authSignOut()
{
	return path("/auth/sign-out");
}
inline std::string player(const std::string &accountId)
{
	return path("/players/" + urlEncode(accountId));
}
inline std::string playerMatches(const std::string &accountId, int limit)
{
	return player(accountId) + "/matches?limit=" + std::to_string(limit);
}
inline std::string leaderboard(const std::string &queueId, int limit)
{
	return path("/leaderboards/" + urlEncode(queueId) + "?limit=" + std::to_string(limit));
}
inline std::string rooms(const std::string &simVersionKey)
{
	return path("/rooms?simVersion=" + urlEncode(simVersionKey));
}
inline std::string match(const std::string &matchId)
{
	return path("/matches/" + urlEncode(matchId));
}
inline std::string matchReplay(const std::string &matchId)
{
	return match(matchId) + "/artifacts/replay";
}
inline std::string maps()
{
	return path("/maps");
}
inline std::string map(const std::string &mapId)
{
	return maps() + "/" + urlEncode(mapId);
}
inline std::string mapLike(const std::string &mapId)
{
	return map(mapId) + "/like";
}
inline std::string mapReports(const std::string &mapId)
{
	return map(mapId) + "/reports";
}
inline std::string mapVersions(const std::string &mapId)
{
	return map(mapId) + "/versions";
}
inline std::string mapVersion(const std::string &mapId, const std::string &hash)
{
	return mapVersions(mapId) + "/" + urlEncode(hash);
}
inline std::string mapBlob(const std::string &hash)
{
	return path("/blobs/maps/" + urlEncode(hash));
}
inline std::string uploads(const std::string &format, const std::string &simVersionKey)
{
	return path("/uploads?format=" + urlEncode(format) + "&simVersion=" + urlEncode(simVersionKey));
}
inline std::string relayRegions()
{
	return path("/relays/regions");
}
} // namespace Api
} // namespace Online
