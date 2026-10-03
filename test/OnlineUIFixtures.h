// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#pragma once
// Fixed models of the online play screens (hub, room, starting match) for the
// presentation harness and the mobile gallery. Nothing here touches the network.
#include "MatchStartScreen.h"
#include "OnlineHubScreen.h"
#include "OnlineMatch.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "PlatformRoom.h"
#include "RoomBackend.h"
#include "RoomScreen.h"
#include <algorithm>
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
							{{"id", "casual-1v1"}, {"name", "Casual 1 vs 1"}, {"mode", "1v1"}, {"rated", false}, {"aiBackfillSeconds", 30}}});
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
	auto entry = [](int rank, const char *name, double rating) {
		return Json{{"rank", rank}, {"rating", rating}, {"entity", {{"kind", "account"}, {"account", {{"displayName", name}}}}}};
	};
	m.leaderboard = Json::array({entry(1, "Mirelle", 1912), entry(2, "tuxboy", 1874), entry(3, "Kestrel", 1840),
								 entry(4, "Ana_M", 1795), entry(5, "Bradley", 1760)});
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
	auto match = std::make_shared<Online::OnlineMatch>(Online::services().client, assignment, context);
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
} // namespace OnlineUIFixtures
