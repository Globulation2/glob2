// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineResources.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace Online
{
namespace
{
std::string text(const Json &json, const char *key)
{
	auto i = json.find(key);
	return i != json.end() && i->is_string() ? i->get<std::string>() : std::string();
}
std::optional<int> integer(const Json &json, const char *key)
{
	auto i = json.find(key);
	if (i == json.end() || !i->is_number())
		return {};
	const double value = i->get<double>();
	if (!std::isfinite(value) || std::fabs(value) > 2e9)
		return {};
	return static_cast<int>(std::lround(value));
}
std::optional<double> number(const Json &json, const char *key)
{
	auto i = json.find(key);
	if (i == json.end() || !i->is_number())
		return {};
	return i->get<double>();
}
std::int64_t count(const Json &json, const char *key)
{
	auto i = json.find(key);
	return i != json.end() && i->is_number_integer() ? i->get<std::int64_t>() : 0;
}
bool flag(const Json &json, const char *key, bool fallback = false)
{
	auto i = json.find(key);
	return i != json.end() && i->is_boolean() ? i->get<bool>() : fallback;
}
std::optional<std::int64_t> timestamp(const Json &json, const char *key)
{
	const std::string value = text(json, key);
	return value.empty() ? std::nullopt : parseTimestamp(value);
}
bool required(const Json &json, std::initializer_list<const char *> keys)
{
	if (!json.is_object())
		return false;
	for (const char *key : keys)
		if (!json.contains(key))
			return false;
	return true;
}
const Json &array(const Json &json, const char *key)
{
	static const Json empty = Json::array();
	auto i = json.find(key);
	return i != json.end() && i->is_array() ? *i : empty;
}
} // namespace

// ------------------------------------------------------------------ queues

std::optional<QueueInfo> QueueInfo::fromJson(const Json &json)
{
	if (!required(json, {"id", "name", "mode"}))
		return {};
	QueueInfo queue;
	queue.id = text(json, "id");
	queue.name = text(json, "name");
	queue.mode = text(json, "mode");
	queue.rated = flag(json, "rated");
	queue.aiBackfillSeconds = integer(json, "aiBackfillSeconds");
	queue.acceptSeconds = integer(json, "acceptSeconds").value_or(0);
	for (const auto &map : array(json, "maps"))
		if (map.is_string())
			queue.maps.push_back(map.get<std::string>());
	if (queue.id.empty())
		return {};
	return queue;
}

std::vector<QueueInfo> queuesFromInstance(const Json &instanceInfo)
{
	std::vector<QueueInfo> queues;
	if (!instanceInfo.is_object())
		return queues;
	for (const auto &item : array(instanceInfo, "queues"))
		if (auto queue = QueueInfo::fromJson(item))
			queues.push_back(*queue);
	return queues;
}

Json RegionRtt::toJson() const
{
	return Json{{"region", region}, {"rttMs", std::clamp(rttMs, 0, 60000)}};
}

Json regionsJson(const std::vector<RegionRtt> &regions)
{
	Json out = Json::array();
	for (const auto &region : regions)
		if (out.size() < 32)
			out.push_back(region.toJson());
	return out;
}

std::vector<RelayRegionInfo> parseRelayRegions(const Json &json)
{
	std::vector<RelayRegionInfo> regions;
	if (!json.is_object())
		return regions;
	for (const auto &item : array(json, "items"))
	{
		RelayRegionInfo region{text(item, "region"), text(item, "probeUrl"),
							   integer(item, "relays").value_or(0)};
		if (!region.region.empty() && !region.probeUrl.empty())
			regions.push_back(region);
	}
	return regions;
}

std::optional<QueueStatus> QueueStatus::fromJson(const Json &json)
{
	if (!required(json, {"ticketId", "queueId", "waitedSeconds"}))
		return {};
	QueueStatus status;
	status.ticketId = text(json, "ticketId");
	status.queueId = text(json, "queueId");
	status.waitedSeconds = integer(json, "waitedSeconds").value_or(0);
	status.ratingWindow = integer(json, "ratingWindow");
	status.aiBackfillAt = timestamp(json, "aiBackfillAt");
	if (auto range = json.find("ratingRange"); range != json.end() && range->is_object())
	{
		status.ratingMin = integer(*range, "min");
		status.ratingMax = integer(*range, "max");
	}
	status.rating = integer(json, "rating");
	status.provisional = flag(json, "provisional");
	status.region = text(json, "region");
	status.rttMs = integer(json, "rttMs");
	if (auto allow = json.find("allowAiOpponent"); allow != json.end() && allow->is_boolean())
		status.allowAiOpponent = allow->get<bool>();
	if (auto ai = json.find("backfillAi"); ai != json.end() && ai->is_object())
	{
		status.backfillAi = text(*ai, "ai");
		status.backfillAiRating = integer(*ai, "rating");
	}
	status.typicalWaitSeconds = integer(json, "typicalWaitSeconds");
	return status;
}

const ProposalSeat *QueueProposal::own() const
{
	for (const auto &seat : seats)
		if (seat.you)
			return &seat;
	return nullptr;
}

std::optional<QueueProposal> QueueProposal::fromJson(const Json &json)
{
	if (!required(json, {"proposalId", "ticketId", "queueId", "requiresAccept"}))
		return {};
	QueueProposal proposal;
	proposal.proposalId = text(json, "proposalId");
	proposal.ticketId = text(json, "ticketId");
	proposal.queueId = text(json, "queueId");
	proposal.requiresAccept = flag(json, "requiresAccept");
	proposal.expiresAt = timestamp(json, "expiresAt");
	proposal.humans = integer(json, "humans").value_or(1);
	proposal.ais = integer(json, "ais").value_or(0);
	if (auto rated = json.find("rated"); rated != json.end() && rated->is_boolean())
		proposal.rated = rated->get<bool>();
	proposal.backfilled = flag(json, "backfilled", proposal.ais > 0);
	proposal.region = text(json, "region");
	if (auto map = json.find("map"); map != json.end() && map->is_object())
	{
		proposal.generatorId = text(*map, "generatorId");
		proposal.width = integer(*map, "width");
		proposal.height = integer(*map, "height");
	}
	for (const auto &item : array(json, "seats"))
	{
		if (!item.is_object())
			continue;
		ProposalSeat seat;
		seat.slot = integer(item, "slot").value_or(0);
		seat.side = integer(item, "side").value_or(0);
		seat.human = text(item, "kind") != "ai";
		seat.displayName = text(item, "displayName");
		seat.ai = text(item, "ai");
		seat.rating = integer(item, "rating");
		seat.provisional = flag(item, "provisional");
		seat.response = text(item, "response");
		seat.you = flag(item, "you");
		proposal.seats.push_back(seat);
	}
	return proposal;
}

std::optional<ProposalEnded> ProposalEnded::fromJson(const Json &json)
{
	if (!required(json, {"proposalId", "ticketId", "outcome", "reason"}))
		return {};
	return ProposalEnded{text(json, "proposalId"), text(json, "ticketId"), text(json, "outcome"),
						 text(json, "reason"), timestamp(json, "cooldownUntil")};
}

std::string MatchAssignment::mapHash() const
{
	if (setup.is_object())
		if (auto map = setup.find("map"); map != setup.end() && map->is_object())
			return text(*map, "hash");
	return {};
}

std::optional<MatchAssignment> MatchAssignment::fromJson(const Json &json)
{
	if (!required(json, {"matchId", "seat", "ticket", "relayUrl", "setup", "mapUrl"}))
		return {};
	MatchAssignment assignment;
	assignment.matchId = text(json, "matchId");
	assignment.seat = integer(json, "seat").value_or(-1);
	assignment.ticket = text(json, "ticket");
	assignment.ticketExpiresAt = timestamp(json, "ticketExpiresAt");
	assignment.relayUrl = text(json, "relayUrl");
	assignment.mapUrl = text(json, "mapUrl");
	assignment.setup = json.at("setup");
	assignment.raw = json;
	if (assignment.matchId.empty() || assignment.seat < 0 || !assignment.setup.is_object())
		return {};
	return assignment;
}

// ----------------------------------------------------------------- matches

const MatchParticipant *MatchSummary::participant(const std::string &accountId) const
{
	for (const auto &p : participants)
		if (!accountId.empty() && p.accountId == accountId)
			return &p;
	return nullptr;
}

bool MatchSummary::aiFilled() const
{
	return std::any_of(participants.begin(), participants.end(), [](const auto &p) { return !p.human; });
}

std::optional<MatchSummary> MatchSummary::fromJson(const Json &json)
{
	if (!required(json, {"id", "origin", "status", "participants"}))
		return {};
	MatchSummary match;
	match.id = text(json, "id");
	match.origin = text(json, "origin");
	match.queueId = text(json, "queueId");
	match.rated = flag(json, "rated");
	match.status = text(json, "status");
	match.endReason = text(json, "endReason");
	match.verification = text(json, "verification");
	match.mapHash = text(json, "mapHash");
	match.mapTitle = text(json, "mapTitle");
	match.startedAt = timestamp(json, "startedAt");
	match.endedAt = timestamp(json, "endedAt");
	match.durationTicks = integer(json, "durationTicks");
	for (const auto &item : array(json, "participants"))
	{
		if (!item.is_object())
			continue;
		MatchParticipant p;
		p.seat = integer(item, "seat").value_or(0);
		p.team = integer(item, "team").value_or(0);
		p.human = text(item, "kind") != "ai";
		p.displayName = text(item, "displayName");
		p.accountId = text(item, "accountId");
		p.ai = text(item, "ai");
		p.outcome = text(item, "outcome");
		p.disconnects = integer(item, "disconnects").value_or(0);
		if (auto rating = item.find("rating"); rating != item.end() && rating->is_object())
			p.rating = RatingChange{text(*rating, "ladder"), number(*rating, "before").value_or(0),
									number(*rating, "after").value_or(0), flag(*rating, "provisional")};
		match.participants.push_back(p);
	}
	return match;
}

const MatchArtifact *MatchDetail::artifact(const std::string &kind) const
{
	for (const auto &a : artifacts)
		if (a.kind == kind)
			return &a;
	return nullptr;
}

std::optional<MatchDetail> MatchDetail::fromJson(const Json &json)
{
	if (!required(json, {"match"}))
		return {};
	auto match = MatchSummary::fromJson(json.at("match"));
	if (!match)
		return {};
	MatchDetail detail;
	detail.match = std::move(*match);
	detail.setup = json.value("setup", Json());
	for (const auto &item : array(json, "artifacts"))
		if (item.is_object())
			detail.artifacts.push_back(
				{text(item, "kind"), text(item, "url"), text(item, "sha256"), count(item, "size")});
	return detail;
}

ResourcePage<MatchSummary> parseMatchList(const Json &json)
{
	ResourcePage<MatchSummary> page;
	if (!json.is_object())
		return page;
	for (const auto &item : array(json, "items"))
		if (auto match = MatchSummary::fromJson(item))
			page.items.push_back(std::move(*match));
	page.nextCursor = text(json, "nextCursor");
	return page;
}

// -------------------------------------------------------------------- maps

std::optional<MapVersionInfo> MapVersionInfo::fromJson(const Json &json)
{
	if (!required(json, {"hash", "validation"}))
		return {};
	MapVersionInfo version;
	version.hash = text(json, "hash");
	version.size = count(json, "size");
	version.width = integer(json, "width");
	version.height = integer(json, "height");
	version.teamCount = integer(json, "teamCount");
	version.minVersionMinor = integer(json, "minVersionMinor");
	version.validation = text(json, "validation");
	version.reason = text(json, "reason");
	version.preview = text(json, "preview");
	version.previewUrl = text(json, "previewUrl");
	version.downloadUrl = text(json, "downloadUrl");
	version.createdAt = timestamp(json, "createdAt");
	return version;
}

std::optional<MapInfo> MapInfo::fromJson(const Json &json)
{
	if (!required(json, {"id", "title"}))
		return {};
	MapInfo map;
	map.id = text(json, "id");
	if (auto owner = json.find("owner"); owner != json.end() && owner->is_object())
	{
		map.ownerId = text(*owner, "id");
		map.ownerName = text(*owner, "displayName");
	}
	map.title = text(json, "title");
	map.description = text(json, "description");
	map.visibility = text(json, "visibility");
	map.hidden = flag(json, "hidden");
	map.hiddenReason = text(json, "hiddenReason");
	if (auto version = json.find("latestVersion"); version != json.end())
		map.latestVersion = MapVersionInfo::fromJson(*version);
	if (auto stats = json.find("stats"); stats != json.end() && stats->is_object())
	{
		map.plays = count(*stats, "plays");
		map.downloads = count(*stats, "downloads");
		map.likes = count(*stats, "likes");
	}
	map.createdAt = timestamp(json, "createdAt");
	map.updatedAt = timestamp(json, "updatedAt");
	return map;
}

ResourcePage<MapInfo> parseMapList(const Json &json)
{
	ResourcePage<MapInfo> page;
	if (!json.is_object())
		return page;
	for (const auto &item : array(json, "items"))
		if (auto map = MapInfo::fromJson(item))
			page.items.push_back(std::move(*map));
	page.nextCursor = text(json, "nextCursor");
	return page;
}

std::optional<MapDetail> MapDetail::fromJson(const Json &json)
{
	if (!required(json, {"map"}))
		return {};
	auto map = MapInfo::fromJson(json.at("map"));
	if (!map)
		return {};
	MapDetail detail;
	detail.map = std::move(*map);
	for (const auto &item : array(json, "versions"))
		if (auto version = MapVersionInfo::fromJson(item))
			detail.versions.push_back(std::move(*version));
	if (auto viewer = json.find("viewer"); viewer != json.end() && viewer->is_object())
	{
		detail.owner = flag(*viewer, "owner");
		detail.moderator = flag(*viewer, "moderator");
		detail.liked = flag(*viewer, "liked");
		detail.reported = flag(*viewer, "reported");
	}
	return detail;
}

// ------------------------------------------------------------------ profile

ProfileSummary summarizeProfile(const std::string &accountId, const std::vector<MatchSummary> &matches,
								std::size_t window)
{
	ProfileSummary summary;
	// Matches arrive newest first; ladders are built oldest first.
	std::map<std::string, LadderSummary> ladders;
	std::vector<std::string> order;
	for (auto i = matches.rbegin(); i != matches.rend(); ++i)
	{
		const auto *me = i->participant(accountId);
		if (!me || !me->rating || me->rating->ladder.empty())
			continue;
		auto [entry, added] = ladders.try_emplace(me->rating->ladder);
		if (added)
			order.push_back(me->rating->ladder);
		auto &ladder = entry->second;
		ladder.ladder = me->rating->ladder;
		ladder.rating = me->rating->after;
		ladder.provisional = me->rating->provisional;
		ladder.lastChange = me->rating->after - me->rating->before;
		ladder.games++;
		ladder.trend.push_back(me->rating->after);
	}
	for (const auto &name : order)
		summary.ladders.push_back(ladders[name]);

	std::vector<int> minutes;
	std::map<std::string, std::pair<int, int>> maps;
	for (const auto &match : matches)
	{
		if (summary.recent >= static_cast<int>(window))
			break;
		const auto *me = match.participant(accountId);
		if (!me || match.status != "ended" || match.endReason == "aborted")
			continue;
		summary.recent++;
		const bool won = me->outcome == "won";
		const bool lost = me->outcome == "lost" || me->outcome == "abandoned";
		if (won)
			summary.wins++;
		else if (lost)
			summary.losses++;
		else
			summary.draws++;
		if (match.durationTicks && *match.durationTicks > 0)
			minutes.push_back(
				static_cast<int>(std::lround(*match.durationTicks / double(MATCH_TICKS_PER_SECOND * 60))));
		if (!match.mapTitle.empty() && (won || lost))
		{
			auto &record = maps[match.mapTitle];
			(won ? record.first : record.second)++;
		}
	}
	if (!minutes.empty())
	{
		std::sort(minutes.begin(), minutes.end());
		const std::size_t n = minutes.size();
		summary.medianMinutes = n % 2 ? minutes[n / 2] : (minutes[n / 2 - 1] + minutes[n / 2] + 1) / 2;
	}
	// Best map: most wins, then fewest losses, then title; at least one win.
	for (const auto &[title, record] : maps)
	{
		if (record.first == 0)
			continue;
		if (summary.bestMap.empty() || record.first > summary.bestMapWins ||
			(record.first == summary.bestMapWins && record.second < summary.bestMapLosses))
		{
			summary.bestMap = title;
			summary.bestMapWins = record.first;
			summary.bestMapLosses = record.second;
		}
	}
	return summary;
}
std::optional<PlayerProfile> PlayerProfile::fromJson(const Json &json)
{
	if (!required(json, {"account"}) || !json.at("account").is_object())
		return {};
	const Json &account = json.at("account");
	PlayerProfile profile;
	profile.accountId = text(account, "id");
	profile.displayName = text(account, "displayName");
	profile.kind = text(account, "kind");
	profile.createdAt = timestamp(account, "createdAt");
	profile.full = text(json, "detail") == "full";
	for (const auto &item : array(json, "ratings"))
		if (item.is_object())
			profile.ratings.push_back({text(item, "ladder"), number(item, "rating").value_or(0),
									   integer(item, "games").value_or(0), integer(item, "wins").value_or(0),
									   flag(item, "provisional"), integer(item, "rank")});
	for (const auto &item : array(json, "ratingHistory"))
		if (item.is_object())
			profile.history.push_back({text(item, "ladder"), number(item, "after").value_or(0), timestamp(item, "at")});
	for (const auto &item : array(json, "recentMatches"))
		if (auto match = MatchSummary::fromJson(item))
			profile.recentMatches.push_back(std::move(*match));
	if (auto aggregates = json.find("aggregates"); aggregates != json.end() && aggregates->is_object())
	{
		profile.aggregateGames = integer(*aggregates, "games");
		profile.aggregateWins = integer(*aggregates, "wins");
		profile.aggregateLosses = integer(*aggregates, "losses");
		profile.aggregateDays = integer(*aggregates, "windowDays");
		profile.medianTicks = number(*aggregates, "medianTicks");
		for (const auto &item : array(*aggregates, "winRates"))
			if (item.is_object())
				profile.winRates.push_back({text(item, "dimension"), text(item, "key"), text(item, "label"),
											integer(item, "games").value_or(0), integer(item, "wins").value_or(0)});
	}
	if (profile.accountId.empty())
		return {};
	return profile;
}

ProfileSummary summarizeProfile(const PlayerProfile &profile, const std::vector<MatchSummary> &matches)
{
	const auto &history = matches.empty() ? profile.recentMatches : matches;
	ProfileSummary summary = summarizeProfile(profile.accountId, history);
	if (!profile.full)
		return summary;
	// Ladders: the server's ratings, with the rating history as the trend.
	std::vector<LadderSummary> ladders;
	for (const auto &rating : profile.ratings)
	{
		LadderSummary ladder;
		ladder.ladder = rating.ladder;
		ladder.rating = rating.rating;
		ladder.provisional = rating.provisional;
		ladder.games = rating.games;
		ladder.rank = rating.rank;
		for (const auto &point : profile.history)
			if (point.ladder == rating.ladder)
				ladder.trend.push_back(point.after);
		if (ladder.trend.size() >= 2)
			ladder.lastChange = ladder.trend.back() - ladder.trend[ladder.trend.size() - 2];
		ladders.push_back(std::move(ladder));
	}
	summary.ladders = std::move(ladders);
	if (profile.aggregateGames && *profile.aggregateGames > 0)
	{
		summary.recent = *profile.aggregateGames;
		summary.wins = profile.aggregateWins.value_or(0);
		summary.losses = profile.aggregateLosses.value_or(0);
		summary.draws = std::max(0, summary.recent - summary.wins - summary.losses);
	}
	if (profile.medianTicks && *profile.medianTicks > 0)
		summary.medianMinutes = static_cast<int>(std::lround(*profile.medianTicks / double(MATCH_TICKS_PER_SECOND * 60)));
	// Best map: the catalog map (or else the generator) with the most wins.
	for (const char *dimension : {"map", "generator"})
	{
		const PlayerProfile::WinRate *best = nullptr;
		for (const auto &rate : profile.winRates)
			if (rate.dimension == dimension && rate.wins > 0 &&
				(!best || rate.wins > best->wins || (rate.wins == best->wins && rate.games < best->games)))
				best = &rate;
		if (best)
		{
			summary.bestMap = best->label.empty() ? best->key : best->label;
			summary.bestMapWins = best->wins;
			summary.bestMapLosses = best->games - best->wins;
			break;
		}
	}
	return summary;
}
} // namespace Online
