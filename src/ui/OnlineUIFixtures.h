// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#pragma once
// Fixed models of the online screens for the presentation harness
// (src/ui/UIPresentationHarness.cpp) and the mobile gallery
// (tools/MobileGalleryHarness.cpp): the play screens (hub, room, starting match)
// and the quick match, profile and maps screens. Everything is canned data and
// nothing touches the network; map previews are PNGs of real generator output
// from docs/map-generators.
#include "MatchStartScreen.h"
#include "OnlineHubScreen.h"
#include "OnlineMapsScreen.h"
#include "OnlineMatch.h"
#include "OnlineProfileScreen.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "PlatformRoom.h"
#include "QuickMatch.h"
#include "QuickMatchScreen.h"
#include "RoomBackend.h"
#include "RoomScreen.h"
#include "ui/OnlineUI.h"
#include <ScreenStack.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace OnlineUIFixtures
{
// Online play fixtures: fixed models, no network (docs/multiplayer/client.md).
using Online::Json;
inline const char *const HOST_ID = "8f0e0c3a-1111-4c1e-9c51-0a6d6c1f0001";
inline const char *const GUEST_ID = "8f0e0c3a-2222-4c1e-9c51-0a6d6c1f0002";

inline Json roomRules()
{
	return Json{{"prestigeVictory", true}, {"suddenDeathMinutes", 0}, {"mapDiscovered", false}, {"allyTeamsFixed", true},
				{"resourceGrowthDisabled", false}, {"resourceScarcityLevel", 0}, {"instantConstruction", false},
				{"stockpileStartLevel", 0}, {"hungerDisabled", false}, {"unitUpgradesDisabled", false}, {"glassCannonLevel", 0},
				{"unitsFearless", false}, {"permadeathDisabled", false}, {"peacefulMode", false}, {"buildingHpLevel", 0}};
}

inline Json roomState()
{
	Json generator{{"generatorId", "marchland"},
				   {"revision", 1},
				   {"params", {{"width", 7}, {"height", 7}, {"teams", 4}, {"workers", 6}}},
				   {"seed", 1937204},
				   {"candidates", 5},
				   {"startingUnitLevel", 0}};
	Json seats = Json::array(
		{{{"seat", 0}, {"team", 0}, {"occupant", {{"kind", "human"}, {"accountId", HOST_ID}, {"displayName", "Bradley"}, {"ready", true}}}},
		 {{"seat", 1}, {"team", 1}, {"occupant", {{"kind", "human"}, {"accountId", GUEST_ID}, {"displayName", "Ana_M"}, {"ready", false}}}},
		 {{"seat", 2}, {"team", 2}, {"occupant", {{"kind", "ai"}, {"ai", "cortex"}, {"name", "Cortex"}}}},
		 {{"seat", 3}, {"team", 3}, {"occupant", {{"kind", "open"}}}}});
	Json members = Json::array(
		{{{"accountId", HOST_ID}, {"displayName", "Bradley"}, {"kind", "registered"}, {"connected", true}, {"seat", 0}},
		 {{"accountId", GUEST_ID}, {"displayName", "Ana_M"}, {"kind", "guest"}, {"connected", true}, {"seat", 1}}});
	Json teams = Json::array({{{"team", 0}, {"alliance", 0}}, {{"team", 1}, {"alliance", 1}}, {{"team", 2}, {"alliance", 0}}, {{"team", 3}, {"alliance", 1}}});
	return Json{{"id", "5b0f3c1e-0000-4a7e-8a51-3d0a1c0b0001"},
				{"code", "KXQ742MNPR"},
				{"inviteUrl", "https://app.glob2online.com/j/KXQ742MNPR"},
				{"name", "Bradley's room"},
				{"visibility", "link"},
				{"status", "open"},
				{"hostAccountId", HOST_ID},
				{"simVersion", {{"versionMinor", 125}, {"netProtocol", 49}, {"dataHash", std::string(64, '0')}}},
				{"map", {{"kind", "generated"}, {"generator", generator}}},
				{"mapStatus", "ready"},
				{"teams", teams},
				{"seats", seats},
				{"rules", roomRules()},
				{"experiments", Json::array()},
				{"members", members},
				{"revision", 7},
				{"createdAt", "2026-10-01T20:00:00Z"}};
}

inline const char *const LATE_ID = "8f0e0c3a-3333-4c1e-9c51-0a6d6c1f0003";

// Every seat taken (the open one locked) and a third member who joined late.
inline Json fullRoomState()
{
	Json room = roomState();
	room["seats"][3]["locked"] = true;
	room["members"].push_back(
		{{"accountId", LATE_ID}, {"displayName", "Guest-2472"}, {"kind", "guest"}, {"connected", true}});
	return room;
}

// A premade map the host uploaded ("balanced for 2"), as the server reports it.
inline Json premadeRoomState()
{
	Json room = roomState();
	room["map"] = {{"kind", "upload"}, {"format", "map"}};
	room["mapTitle"] = "balanced for 2";
	room["seats"] = Json::array({room["seats"][0], room["seats"][1]});
	room["teams"] = Json::array({{{"team", 0}, {"alliance", 0}}, {{"team", 1}, {"alliance", 1}}});
	return room;
}

inline std::vector<std::pair<std::string, std::string>> roomChat()
{
	return {{"", "Ana_M joined from an invite link."},
			{"Ana_M", "hi! is this vs AI?"},
			{"Bradley", "2v2, Cortex is on my team for now."},
			{"", "The map changed to Marchland 128x128."}};
}

// A LAN room as the shared Room screen shows it (state C of the mock-up).
class LanRoomFixture final : public RoomBackend
{
	std::deque<Event> events;

  public:
	LanRoomFixture()
	{
		for (const char *line : {"Sam joined.", "Juno joined from a browser.", "Juno: map is loading, sec"})
		{
			Event e;
			e.kind = Event::Chat;
			e.text = line;
			events.push_back(e);
		}
	}
	void update() override {}
	std::optional<Event> takeEvent() override
	{
		if (events.empty())
			return std::nullopt;
		auto e = events.front();
		events.pop_front();
		return e;
	}
	bool lobbyReady() const override { return true; }
	bool isHost() const override { return true; }
	std::string mapName() const override { return "Isles 128x128"; }
	int teamCount() const override { return 3; }
	std::optional<std::array<std::uint8_t, 3>> teamColor(int team) const override
	{
		static const std::array<std::uint8_t, 3> colors[] = {{229, 46, 46}, {120, 229, 46}, {46, 210, 229}};
		return colors[std::clamp(team, 0, 2)];
	}
	std::vector<Slot> slots() const override
	{
		std::vector<Slot> result(3);
		result[0].index = 0;
		result[0].name = "Bradley";
		result[0].local = result[0].host = true;
		result[0].detail = "this computer";
		result[1].index = 1;
		result[1].name = "Sam";
		result[1].team = 1;
		result[1].latencyMs = 3;
		result[1].detail = "192.168.1.31";
		result[2].index = 2;
		result[2].name = "Juno";
		result[2].team = 2;
		result[2].ready = false;
		result[2].latencyMs = 9;
		result[2].detail = "browser";
		result[2].progress = 64;
		return result;
	}
	void changeTeam(int, int) override {}
	void kick(int) override {}
	void addAI(AI::ImplementationID) override {}
	void setReady(bool) override {}
	bool everyoneReady() const override { return false; }
	bool canStart() const override { return false; }
	void start() override {}
	bool starting() const override { return false; }
	void sendChat(const std::string &) override {}
	int downloadPercent() const override { return -1; }
	std::string experimentsLabel() const override { return {}; }
	std::string shareText() const override { return "ws://192.168.1.20:7487 4F2A"; }
	GameHeader *optionsHeader() override { return nullptr; }
	MapHeader *optionsMap() override { return nullptr; }
	void optionsChanged() override {}
	void leave() override {}
	GAGCore::CooperativeTask initGame(Engine &) override { co_return false; }
	void gameStarted(bool) override {}
	void gameEnded(bool) override {}
	std::string roomName() const override { return "Living-room LAN"; }
	std::string localAddress() const override { return "192.168.1.20"; }
	std::string setupSummary() const override { return "FFA - Isles 128x128 - Quick clash"; }
	std::string waitingFor() const override { return "Waiting for Juno's map download"; }
};

inline OnlineHubScreen::Model hubModel()
{
	OnlineHubScreen::Model m;
	m.origin = "https://app.glob2online.com";
	m.instanceName = "Globulation 2 Online";
	m.link = OnlineHubScreen::Model::Link::Online;
	m.displayName = "Guest-4821";
	m.accountKind = "guest";
	m.accountId = GUEST_ID;
	m.queues = Json::array({{{"id", "ranked-1v1"}, {"name", "1 vs 1"}, {"mode", "1v1"}, {"rated", true}},
							{{"id", "ranked-2v2"}, {"name", "2 vs 2"}, {"mode", "2v2"}, {"rated", true}},
							{{"id", "casual-1v1"}, {"name", "Casual 1 vs 1"}, {"mode", "1v1"}, {"rated", false}, {"aiBackfillSeconds", 30},
							 {"maps", Json::array({"even-ground", "symmetric-arena"})}}});
	m.providers = Json::array({{{"id", "google"}, {"kind", "oidc"}, {"displayName", "Google"}},
							   {{"id", "microsoft"}, {"kind", "oidc"}, {"displayName", "Microsoft"}}});
	m.rooms = Json::array(
		{{{"code", "AAAAAA2222"}, {"name", "Sunday 2v2, newcomers welcome"}, {"hostDisplayName", "Ana_M"}, {"mapTitle", "Marchland 128x128"}, {"seatsTotal", 4}, {"seatsTaken", 3}, {"status", "open"}},
		 {{"code", "BBBBBB3333"}, {"name", "FFA on big maps"}, {"hostDisplayName", "tuxboy"}, {"mapTitle", "Fingerprint 256x256"}, {"seatsTotal", 4}, {"seatsTaken", 1}, {"status", "open"}},
		 {{"code", "CCCCCC4444"}, {"name", "Co-op vs 2 Maxima"}, {"hostDisplayName", "Kestrel"}, {"mapTitle", "Isles 128x128"}, {"seatsTotal", 4}, {"seatsTaken", 4}, {"status", "open"}}});
	Json won = Json::array({{{"accountId", GUEST_ID}, {"displayName", "Guest-4821"}, {"outcome", "won"}, {"rating", {{"before", 1512.0}, {"after", 1528.0}}}},
							{{"displayName", "Kestrel"}, {"outcome", "lost"}}});
	Json room = Json::array({{{"accountId", GUEST_ID}, {"displayName", "Guest-4821"}, {"outcome", "won"}},
							 {{"displayName", "Ana_M"}, {"outcome", "lost"}},
							 {{"displayName", "tuxboy"}, {"outcome", "lost"}}});
	m.recent = Json::array({{{"id", "m1"}, {"origin", "queue"}, {"queueId", "1 vs 1"}, {"rated", true}, {"verification", "verified"}, {"mapTitle", "Even Ground"}, {"durationTicks", 31500}, {"participants", won}},
							{{"id", "m2"}, {"origin", "room"}, {"rated", false}, {"verification", "not_applicable"}, {"mapTitle", "Marchland"}, {"durationTicks", 72000}, {"participants", room}}});
	m.leaderboardName = "1 vs 1";
	auto entry = [](int rank, const char *name, double rating, std::string id = {}) {
		return Json{{"rank", rank}, {"rating", rating}, {"entity", {{"kind", "account"}, {"account", {{"id", id}, {"displayName", name}}}}}};
	};
	m.leaderboard = Json::array({entry(1, "Mirelle", 1912), entry(2, "tuxboy", 1874), entry(3, "Kestrel", 1840),
								 entry(4, "Ana_M", 1795), entry(5, "Bradley", 1760, GUEST_ID)});
	return m;
}

inline std::unique_ptr<GAGGUI::Screen> hubFixture(GAGGUI::ScreenStack &s, std::function<void(OnlineHubScreen::Model &)> adjust = {})
{
	auto hub = std::make_unique<OnlineHubScreen>(s, false);
	auto model = hubModel();
	if (adjust)
		adjust(model);
	hub->preview(model);
	return hub;
}

inline std::shared_ptr<Online::OnlineMatch> startingMatch()
{
	Json setup{{"simVersion", {{"versionMinor", 125}, {"netProtocol", 49}, {"dataHash", std::string(64, '0')}}},
			   {"seed", 7},
			   {"map", {{"kind", "catalog"}, {"hash", std::string(64, 'a')}}},
			   {"teams", Json::array({{{"team", 0}, {"alliance", 0}}, {{"team", 1}, {"alliance", 1}}})},
			   {"seats", Json::array({{{"seat", 0}, {"human", true}, {"team", 0}, {"name", "Bradley"}}, {{"seat", 1}, {"human", true}, {"team", 1}, {"name", "Kestrel"}}})},
			   {"rules", roomRules()},
			   {"experiments", Json::array()}};
	Json assignment{{"matchId", "9d0c4b1a-0000-4c2e-9a10-000000000001"},
					{"seat", 0},
					{"ticket", "x"},
					{"ticketExpiresAt", "2026-10-01T21:00:00Z"},
					{"relayUrl", "wss://app.glob2online.com/relay/eu-west-2"},
					{"setup", setup},
					{"mapUrl", "https://app.glob2online.com/api/v1/blobs/maps/aaaa"},
					{"mapTitle", "Even Ground"}};
	Online::OnlineMatchContext context;
	context.label = "1 vs 1 - Ranked";
	context.fromRoom = false;
	context.rated = true;
	context.ladder = "1v1";
	auto match = std::make_shared<Online::OnlineMatch>(Online::services().client, Online::services().maps, Online::services().storage, assignment, context);
	Online::OnlineMatch::Player you, other;
	you.seat = 0;
	you.name = "Bradley";
	you.local = true;
	you.r = 229, you.g = 46, you.b = 46;
	you.progress = 40;
	you.state = "map";
	other.seat = 1;
	other.name = "Kestrel";
	other.r = 120, other.g = 229, other.b = 46;
	other.progress = 100;
	other.state = "ready";
	match->preview(Online::OnlineMatch::Step::Map, {you, other}, "eu-west-2", 28);
	return match;
}

// ---------------------------------------------- quick match, profile and maps

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

// A section of the hub other than Play (a registered player placed on the ladder).
inline std::unique_ptr<GAGGUI::Screen> hubSection(GAGGUI::ScreenStack &stack, OnlineHubScreen::Section section)
{
	auto hub = std::make_unique<OnlineHubScreen>(stack, false);
	auto model = hubModel();
	model.displayName = "Bradley";
	model.accountKind = "registered";
	model.myRank = 5;
	model.myRating = 1760;
	hub->preview(model);
	hub->showSection(section);
	return hub;
}

// The hub with a casual search running behind it: the search strip.
inline std::unique_ptr<GAGGUI::Screen> hubSearching(GAGGUI::ScreenStack &stack)
{
	auto &m = model(10);
	const auto now = Glob2UI::wallClockMs();
	auto searching = status(now);
	searching.queueId = queues()[2].id;
	m.presentSearching(queues()[2], searching, now - 21000);
	auto hub = std::make_unique<OnlineHubScreen>(stack, false);
	hub->preview(hubModel());
	hub->previewSearching(m);
	return hub;
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

// A server whose catalog has no maps yet: the empty state offers Upload.
inline std::unique_ptr<GAGGUI::Screen> mapsEmpty(GAGGUI::ScreenStack &stack)
{
	auto data = mapsData("");
	data.browse.clear();
	return std::make_unique<OnlineMapsScreen>(stack, OnlineMapsScreen::Tab::Browse, std::move(data));
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
} // namespace OnlineUIFixtures
