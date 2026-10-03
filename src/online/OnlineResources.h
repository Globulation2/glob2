// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PlatformProtocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Typed views of the platform resources the quick-match, profile and map
// screens read (platform/packages/protocol: realtime.ts queue events,
// MatchAssignment; resources.ts QueueInfo, MatchSummary, MatchDetail, MapInfo,
// MapDetail; relay.ts RelayRegionList). Parsing is lenient about fields the
// screens do not need and returns nothing for documents missing required ones.
// Timestamps are milliseconds since the Unix epoch.
namespace Online
{
struct QueueInfo
{
	std::string id, name, mode; // mode: 1v1 | 2v2
	bool rated = false;
	std::optional<int> aiBackfillSeconds;
	int acceptSeconds = 0;
	std::vector<std::string> maps; // generator ids of the pool
	static std::optional<QueueInfo> fromJson(const Json &json);
};
// InstanceInfo.queues.
std::vector<QueueInfo> queuesFromInstance(const Json &instanceInfo);

struct RegionRtt
{
	std::string region;
	int rttMs = 0;
	Json toJson() const;
};
Json regionsJson(const std::vector<RegionRtt> &regions);

struct RelayRegionInfo
{
	std::string region, probeUrl;
	int relays = 0;
};
// RelayRegionList (GET /api/v1/relays/regions).
std::vector<RelayRegionInfo> parseRelayRegions(const Json &json);

struct QueueStatus
{
	std::string ticketId, queueId;
	int waitedSeconds = 0;
	std::optional<int> ratingWindow;
	std::optional<std::int64_t> aiBackfillAt;
	std::optional<int> ratingMin, ratingMax, rating;
	bool provisional = false;
	std::string region;
	std::optional<int> rttMs;
	std::optional<bool> allowAiOpponent;
	std::string backfillAi; // AI id
	std::optional<int> backfillAiRating;
	std::optional<int> typicalWaitSeconds;
	static std::optional<QueueStatus> fromJson(const Json &json);
};

struct ProposalSeat
{
	int slot = 0, side = 0;
	bool human = true;
	std::string displayName, ai;
	std::optional<int> rating;
	bool provisional = false;
	// pending | accepted | declined | timeout | not_required
	std::string response;
	bool you = false;
};

struct QueueProposal
{
	std::string proposalId, ticketId, queueId;
	bool requiresAccept = false;
	std::optional<std::int64_t> expiresAt;
	int humans = 0, ais = 0;
	std::optional<bool> rated;
	bool backfilled = false;
	std::string region;
	std::string generatorId;
	std::optional<int> width, height;
	std::vector<ProposalSeat> seats;
	const ProposalSeat *own() const;
	static std::optional<QueueProposal> fromJson(const Json &json);
};

struct ProposalEnded
{
	std::string proposalId, ticketId;
	std::string outcome; // requeued | removed
	std::string reason;	 // declined | timeout | other_declined | start_failed
	std::optional<std::int64_t> cooldownUntil;
	static std::optional<ProposalEnded> fromJson(const Json &json);
};

// The match a client has been placed in (match.start, match.reconnect).
struct MatchAssignment
{
	std::string matchId;
	int seat = 0;
	std::string ticket;
	std::optional<std::int64_t> ticketExpiresAt;
	std::string relayUrl, mapUrl;
	Json setup; // MatchSetup, parsed by Online::MatchSetup::fromJson when needed
	// The whole MatchAssignment as received (mapTitle, ratingPreview and fields
	// this struct does not name), for OnlineMatch.
	Json raw;
	// setup.map.hash
	std::string mapHash() const;
	static std::optional<MatchAssignment> fromJson(const Json &json);
};

struct RatingChange
{
	std::string ladder;
	double before = 0, after = 0;
	bool provisional = false;
};

struct MatchParticipant
{
	int seat = 0, team = 0;
	bool human = true;
	std::string displayName, accountId, ai;
	std::string outcome; // won | lost | draw | unresolved | abandoned, empty while unknown
	int disconnects = 0;
	std::optional<RatingChange> rating;
};

struct MatchSummary
{
	std::string id;
	std::string origin; // room | queue
	std::string queueId;
	bool rated = false;
	std::string status;		  // starting | running | ended | cancelled
	std::string endReason;	  // completed | abandoned | aborted
	std::string verification; // pending | verified | diverged | unverifiable | not_applicable | failed
	std::string mapHash, mapTitle;
	std::optional<std::int64_t> startedAt, endedAt;
	std::optional<int> durationTicks;
	std::vector<MatchParticipant> participants;
	const MatchParticipant *participant(const std::string &accountId) const;
	bool aiFilled() const;
	static std::optional<MatchSummary> fromJson(const Json &json);
};

struct MatchArtifact
{
	std::string kind; // record | replay | result
	std::string url, sha256;
	std::int64_t size = 0;
};

struct MatchDetail
{
	MatchSummary match;
	std::vector<MatchArtifact> artifacts;
	Json setup;
	const MatchArtifact *artifact(const std::string &kind) const;
	static std::optional<MatchDetail> fromJson(const Json &json);
};

template <class T> struct ResourcePage
{
	std::vector<T> items;
	std::string nextCursor;
};
ResourcePage<MatchSummary> parseMatchList(const Json &json);

struct MapVersionInfo
{
	std::string hash;
	std::int64_t size = 0;
	std::optional<int> width, height, teamCount, minVersionMinor;
	std::string validation; // pending | valid | invalid
	std::string reason;		// why it is invalid
	std::string preview;	// pending | ready | failed
	std::string previewUrl, downloadUrl;
	std::optional<std::int64_t> createdAt;
	static std::optional<MapVersionInfo> fromJson(const Json &json);
};

struct MapInfo
{
	std::string id;
	std::string ownerId, ownerName;
	std::string title, description;
	std::string visibility; // public | unlisted | private
	bool hidden = false;
	std::string hiddenReason;
	std::optional<MapVersionInfo> latestVersion;
	std::int64_t plays = 0, downloads = 0, likes = 0;
	std::optional<std::int64_t> createdAt, updatedAt;
	static std::optional<MapInfo> fromJson(const Json &json);
};
ResourcePage<MapInfo> parseMapList(const Json &json);

struct MapDetail
{
	MapInfo map;
	std::vector<MapVersionInfo> versions;
	bool owner = false, moderator = false, liked = false, reported = false;
	static std::optional<MapDetail> fromJson(const Json &json);
};

// Profile figures computed from the account's recent matches (MatchList),
// so the client needs no endpoint beyond the history list.
struct LadderSummary
{
	std::string ladder; // queue id
	double rating = 0;
	bool provisional = false;
	int games = 0;
	std::optional<int> rank;
	std::optional<double> lastChange;
	std::vector<double> trend; // ratings after each game, oldest first
};
struct ProfileSummary
{
	std::vector<LadderSummary> ladders;
	// Over the most recent `window` ended matches the account played.
	int recent = 0, wins = 0, losses = 0, draws = 0;
	std::optional<int> medianMinutes;
	std::string bestMap;
	int bestMapWins = 0, bestMapLosses = 0;
};
ProfileSummary summarizeProfile(const std::string &accountId, const std::vector<MatchSummary> &matches,
								std::size_t window = 50);

// GET /api/v1/players/{id} (PlayerProfile in platform/packages/protocol/src/history.ts).
struct PlayerProfile
{
	std::string accountId, displayName, kind;
	std::optional<std::int64_t> createdAt;
	bool full = false; // detail: full (registered) or minimal (guests: no ratings)
	struct Rating
	{
		std::string ladder;
		double rating = 0;
		int games = 0, wins = 0;
		bool provisional = false;
		std::optional<int> rank;
	};
	std::vector<Rating> ratings;
	struct Point
	{
		std::string ladder;
		double after = 0;
		std::optional<std::int64_t> at;
	};
	std::vector<Point> history; // oldest first
	std::vector<MatchSummary> recentMatches;
	struct WinRate
	{
		std::string dimension, key, label;
		int games = 0, wins = 0;
	};
	std::optional<int> aggregateGames, aggregateWins, aggregateLosses, aggregateDays;
	std::optional<double> medianTicks;
	std::vector<WinRate> winRates;
	static std::optional<PlayerProfile> fromJson(const Json &json);
};
// The profile figures from the server's aggregates; matches (the history
// list) only fill what the profile leaves out.
ProfileSummary summarizeProfile(const PlayerProfile &profile, const std::vector<MatchSummary> &matches);
// Ticks per second of online matches (the relay clock).
inline constexpr int MATCH_TICKS_PER_SECOND = 25;
} // namespace Online
