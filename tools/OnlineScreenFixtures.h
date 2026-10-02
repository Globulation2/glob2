// SPDX-License-Identifier: GPL-3.0-or-later
// Offline review fixtures for the online screens (quick match, profile,
// maps), shared by test/UIPresentationHarness.cpp and the mobile gallery.
// Everything is canned data: no platform is contacted. Map previews are PNGs
// of real generator output from docs/map-generators.
#pragma once

#include "OnlineMapsScreen.h"
#include "OnlineProfileScreen.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "QuickMatch.h"
#include "QuickMatchScreen.h"
#include "ui/OnlineUI.h"

#include <ScreenStack.h>

#include <chrono>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace OnlineScreenFixtures
{
inline std::string timestamp(std::int64_t ms)
{
	const std::time_t t = static_cast<std::time_t>(ms / 1000);
	std::tm tm{};
#ifdef _WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buffer[32];
	std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &tm);
	return buffer;
}

inline std::vector<Online::QueueInfo> queues()
{
	Online::QueueInfo ranked{"ranked-1v1", "1 vs 1 ranked", "1v1", true, 60, 10,
							 {"symmetric-arena", "even-ground", "marchland", "carousel"}};
	Online::QueueInfo team{"ranked-2v2", "2 vs 2 ranked", "2v2", true, 120, 10, {"even-ground", "marchland"}};
	Online::QueueInfo casual{"casual-1v1", "Casual 1 vs 1", "1v1", false, 30, 0, {"symmetric-arena"}};
	return {ranked, team, casual};
}

inline const char *TICKET = "5d0f2c43-6a3e-4c8e-b8f1-9e2a7c4d3b10";

// A search model that never talks to a server (the client is not started).
inline Online::QuickMatch &model(int slot)
{
	static std::vector<std::unique_ptr<Online::QuickMatch>> models;
	while (int(models.size()) <= slot)
	{
		Online::QuickMatch::Environment env;
		env.wallClock = Glob2UI::wallClockMs;
		models.push_back(std::make_unique<Online::QuickMatch>(Online::services().client, env));
	}
	return *models[slot];
}

inline Online::QueueStatus status(std::int64_t now, bool allowAi = true)
{
	Online::QueueStatus s;
	s.ticketId = TICKET;
	s.queueId = "ranked-1v1";
	s.waitedSeconds = 42;
	s.ratingWindow = 100;
	s.aiBackfillAt = now + 18000;
	s.ratingMin = 1430;
	s.ratingMax = 1630;
	s.rating = 1528;
	s.region = "eu-west";
	s.rttMs = 28;
	s.allowAiOpponent = allowAi;
	s.backfillAi = "cortex";
	s.backfillAiRating = 1601;
	s.typicalWaitSeconds = 40;
	return s;
}

inline std::unique_ptr<GAGGUI::Screen> quickMatch(GAGGUI::ScreenStack &stack, bool searching)
{
	auto &m = model(searching ? 1 : 0);
	const auto now = Glob2UI::wallClockMs();
	if (searching)
		m.presentSearching(queues()[0], status(now), now - 42000);
	return std::make_unique<QuickMatchScreen>(stack, m, queues(), "https://app.glob2online.com", "Bradley");
}

inline Online::Json proposalJson(bool ranked, std::int64_t now)
{
	Online::Json seats = Online::Json::array();
	seats.push_back({{"slot", 0}, {"side", 0}, {"kind", "human"}, {"displayName", "Bradley"}, {"rating", 1528},
					 {"response", ranked ? "pending" : "not_required"}, {"you", true}});
	if (ranked)
		seats.push_back({{"slot", 1}, {"side", 1}, {"kind", "human"}, {"displayName", "Kestrel"}, {"rating", 1540},
						 {"response", "pending"}});
	else
		seats.push_back({{"slot", 1}, {"side", 1}, {"kind", "ai"}, {"displayName", "Cortex"}, {"ai", "cortex"},
						 {"rating", 1601}, {"response", "not_required"}});
	Online::Json data = {{"proposalId", "0b6a7f6e-8d1c-4a59-9a43-2f0c3d1e5b77"},
						 {"ticketId", TICKET},
						 {"queueId", "ranked-1v1"},
						 {"requiresAccept", ranked},
						 {"humans", ranked ? 2 : 1},
						 {"ais", ranked ? 0 : 1},
						 {"rated", true},
						 {"backfilled", !ranked},
						 {"region", "eu-west"},
						 {"map", {{"generatorId", ranked ? "even-ground" : "symmetric-arena"}, {"width", 128}, {"height", 128}}},
						 {"seats", seats}};
	if (ranked)
		data["expiresAt"] = timestamp(now + 7000);
	return data;
}

inline std::unique_ptr<GAGGUI::Screen> matchFound(bool ranked)
{
	auto &m = model(ranked ? 2 : 3);
	const auto now = Glob2UI::wallClockMs();
	m.setStartDelayMs(3600 * 1000); // keep the countdown on screen
	m.presentSearching(queues()[0], status(now), now - 42000);
	m.handleEvent("queue.proposal", proposalJson(ranked, now));
	return std::make_unique<MatchFoundScreen>(m);
}

// ---------------------------------------------------------------- profile

inline Online::MatchSummary match(const std::string &id, const std::string &rival, bool ai, const std::string &outcome,
								  double before, double after, const std::string &map, int minutes,
								  std::int64_t endedAt, const std::string &origin = "queue", bool rated = true,
								  const std::string &verification = "verified")
{
	Online::MatchSummary m;
	m.id = id;
	m.origin = origin;
	m.queueId = rated ? "ranked-1v1" : "";
	m.rated = rated;
	m.status = "ended";
	m.verification = verification;
	m.mapTitle = map;
	m.durationTicks = minutes * 60 * Online::MATCH_TICKS_PER_SECOND;
	m.endedAt = endedAt;
	Online::MatchParticipant me;
	me.accountId = "me";
	me.displayName = "Bradley";
	me.outcome = outcome;
	if (rated && verification == "verified")
		me.rating = Online::RatingChange{"ranked-1v1", before, after, false};
	Online::MatchParticipant other;
	other.team = 1;
	other.seat = 1;
	other.human = !ai;
	other.ai = ai ? "cortex" : "";
	other.displayName = rival;
	other.outcome = outcome == "won" ? "lost" : outcome == "lost" ? "won" : outcome;
	m.participants = {me, other};
	return m;
}

inline std::unique_ptr<GAGGUI::Screen> profile(GAGGUI::ScreenStack &stack)
{
	const auto now = Glob2UI::wallClockMs();
	const std::int64_t day = 86400000;
	OnlineProfileScreen::Data data;
	data.instance = "https://app.glob2online.com";
	data.accountId = "me";
	data.displayName = "Bradley";
	data.kind = "registered";
	data.since = "Sep 2026";
	data.now = now;
	data.ladderNames = {{"ranked-1v1", "1 vs 1 ranked"}, {"ranked-2v2", "2 vs 2 ranked"}};
	data.matches = {
		match("m8", "Kestrel", false, "lost", 1543, 1543, "River", 12, now - 3 * day, "queue", true, "pending"),
		match("m7", "Kestrel", false, "won", 1528, 1543, "Even Ground", 21, now - 2 * 3600000),
		match("m6", "Mirelle", false, "lost", 1539, 1528, "Symmetric Arena", 34, now - day),
		match("m5", "Cortex", true, "won", 1530, 1539, "Symmetric Arena", 18, now - 2 * day),
		match("m4", "tuxboy", false, "won", 0, 0, "Fingerprint", 48, now - 3 * day, "room", false, "not_applicable"),
		match("m3", "Ana_M", false, "won", 1499, 1530, "Marchland", 29, now - 4 * day),
		match("m2", "Nyx", false, "lost", 1510, 1499, "Even Ground", 25, now - 5 * day),
		match("m1", "Zed", false, "won", 1490, 1510, "Even Ground", 22, now - 6 * day),
		match("m0", "Kestrel", false, "draw", 1490, 1490, "Marchland", 60, now - 7 * day),
	};
	auto team = match("m9", "Kestrel", false, "won", 1490, 1521, "Marchland", 29, now - 4 * day);
	team.queueId = "ranked-2v2";
	team.participants[0].rating = Online::RatingChange{"ranked-2v2", 1490, 1521, true};
	data.matches.insert(data.matches.begin() + 5, team);
	// The server's profile: ratings with rank, rating history and aggregates.
	Online::PlayerProfile profile;
	profile.accountId = "me";
	profile.displayName = "Bradley";
	profile.kind = "registered";
	profile.full = true;
	profile.ratings = {{"ranked-1v1", 1543, 31, 18, false, 198}, {"ranked-2v2", 1521, 5, 3, true, std::nullopt}};
	const double ones[] = {1490, 1510, 1499, 1530, 1539, 1528, 1543, 1531, 1538, 1543};
	for (double after : ones)
		profile.history.push_back({"ranked-1v1", after, std::nullopt});
	const double twos[] = {1490, 1502, 1497, 1515, 1521};
	for (double after : twos)
		profile.history.push_back({"ranked-2v2", after, std::nullopt});
	profile.aggregateGames = 50;
	profile.aggregateWins = 28;
	profile.aggregateLosses = 21;
	profile.aggregateDays = 90;
	profile.medianTicks = 24 * 60 * Online::MATCH_TICKS_PER_SECOND;
	profile.winRates = {{"queue", "ranked-1v1", "1 vs 1 ranked", 31, 18}, {"generator", "even-ground", "Even Ground", 12, 9}};
	data.profile = profile;
	return std::make_unique<OnlineProfileScreen>(stack, data);
}

// ------------------------------------------------------------------- maps

inline Online::MapInfo map(const std::string &id, const std::string &title, const std::string &owner, int side,
						   int teams, int plays, int likes, const std::string &previewKey, std::int64_t updated,
						   const std::string &visibility = "public", const std::string &validation = "valid")
{
	Online::MapInfo m;
	m.id = id;
	m.title = title;
	m.ownerName = owner;
	m.ownerId = owner;
	m.visibility = visibility;
	m.description = "Tight canals split the map in two; bridges are the only way across. Built for 1 vs 1.";
	m.plays = plays;
	m.likes = likes;
	m.downloads = plays * 3;
	m.updatedAt = updated;
	if (validation == "valid")
	{
		Online::MapVersionInfo v;
		v.hash = std::string(64, 'a');
		v.width = v.height = side;
		v.teamCount = teams;
		v.validation = "valid";
		v.preview = "ready";
		v.previewUrl = "fixture://" + previewKey;
		m.latestVersion = v;
	}
	return m;
}

inline OnlineMapsScreen::Data mapsData(const std::string &root)
{
	const auto now = Glob2UI::wallClockMs();
	const std::int64_t day = 86400000;
	OnlineMapsScreen::Data data;
	data.instance = "https://app.glob2online.com";
	data.now = now;
	data.previewFiles = {
		{"fixture://drumlin-128", root + "docs/map-generators/drumlin-field/128-4-colonies.png"},
		{"fixture://sierpinski", root + "docs/map-generators/images/sierpinski-gardens.png"},
		{"fixture://hilbert", root + "docs/map-generators/images/hilbert-river.png"},
		{"fixture://drumlin-256", root + "docs/map-generators/drumlin-field/256-4-colonies-seed1.png"},
		{"fixture://lava", root + "docs/map-generators/images/lava-shield/default-256.png"},
		{"fixture://islets", root + "docs/map-generators/images/lava-shield/islets-256-4-seed101.png"},
		{"fixture://drumlin-256b", root + "docs/map-generators/drumlin-field/256-4-colonies-seed2.png"},
	};
	data.browse = {
		map("m1", "Canal Duel", "mirelle", 128, 2, 1204, 214, "drumlin-128", now - 4 * day),
		map("m2", "Four Isles", "tuxboy", 128, 4, 860, 97, "islets", now - 9 * day),
		map("m3", "Whorl", "Ana_M", 256, 4, 410, 66, "sierpinski", now - 20 * day),
		map("m4", "Long River", "kestrel", 128, 2, 395, 41, "hilbert", now - 30 * day),
		map("m5", "Marsh Kings", "zed", 128, 4, 220, 30, "drumlin-256", now - 40 * day),
		map("m6", "Arena Classic", "bradley", 128, 2, 180, 28, "lava", now - 21 * day),
		map("m7", "Flatlands", "nyx", 128, 2, 95, 9, "drumlin-256b", now - 50 * day),
	};
	Online::MapDetail detail;
	detail.map = data.browse[0];
	detail.versions = {*data.browse[0].latestVersion, *data.browse[0].latestVersion, *data.browse[0].latestVersion};
	data.details["m1"] = detail;

	auto arena = map("m6", "Arena Classic", "bradley", 128, 2, 180, 28, "lava", now - 21 * day);
	auto checking = map("m8", "Test archipelago", "bradley", 128, 4, 0, 0, "", now, "unlisted", "pending");
	auto broken = map("m9", "broken-start", "bradley", 128, 4, 0, 0, "", now - day, "unlisted", "invalid");
	data.mine = {arena, checking, broken};
	Online::MapVersionInfo pending;
	pending.hash = std::string(64, 'b');
	pending.validation = "pending";
	pending.preview = "pending";
	data.details["m8"] = Online::MapDetail{checking, {pending}};
	Online::MapVersionInfo invalid;
	invalid.hash = std::string(64, 'c');
	invalid.validation = "invalid";
	invalid.preview = "failed";
	invalid.reason = "colony 3 has no starting building";
	data.details["m9"] = Online::MapDetail{broken, {invalid}};
	return data;
}

inline std::unique_ptr<GAGGUI::Screen> maps(GAGGUI::ScreenStack &stack, OnlineMapsScreen::Tab tab, const std::string &root)
{
	return std::make_unique<OnlineMapsScreen>(stack, tab, mapsData(root));
}

inline std::unique_ptr<GAGGUI::Screen> share(int stage)
{
	auto screen = std::make_unique<MapShareScreen>("maps/balanced.map");
	if (stage > 0)
	{
		Online::MapVersionInfo version;
		version.hash = std::string(64, 'd');
		version.validation = stage == 2 ? "invalid" : "pending";
		version.preview = "pending";
		version.reason = "colony 3 has no starting building";
		screen->presentProgress(stage == 2 ? Online::MapShare::Stage::Rejected : Online::MapShare::Stage::Checking, version);
	}
	return screen;
}
} // namespace OnlineScreenFixtures
