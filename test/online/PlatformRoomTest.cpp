// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// PlatformRoom against a scripted platform: a premade map is uploaded and then
// chosen for the room (not replaced by a generated one), the room names it by the
// server's mapTitle, map changes are announced once, and a member without a seat is
// listed and told why Ready is unavailable.

#include "Glob2Test.h"
#include "EngineFixtures.h"
#include "InstanceConfig.h"
#include "MapCache.h"
#include "OnlineFakes.h"
#include "OnlineStorage.h"
#include "PlatformClient.h"
#include "PlatformRoom.h"
#include "CustomGameSetup.h"

#include <StringTable.h>
#include <Toolkit.h>

#include <algorithm>

using namespace Online;
using OnlineFakes::Json;

namespace
{
const std::string ORIGIN = "https://play.example.org";
const std::string ME = "00000000-0000-4000-8000-000000000001";
const std::string OTHER = "00000000-0000-4000-8000-000000000002";
const std::string HASH(64, 'a');

ClientOptions options()
{
	ClientOptions o;
	o.clientVersion = "test";
	o.platform = "desktop";
	return o;
}

Json seat(int index, const std::string &account, const std::string &name)
{
	if (account.empty())
		return Json{{"seat", index}, {"team", index}, {"occupant", {{"kind", "open"}}}};
	return Json{{"seat", index},
				{"team", index},
				{"occupant", {{"kind", "human"}, {"accountId", account}, {"displayName", name}, {"ready", false}}}};
}

// A room on a generated map still being generated (no hash: nothing to download).
Json room(int revision, const std::string &host, Json seats, Json members)
{
	Json teams = Json::array();
	for (std::size_t i = 0; i < seats.size(); ++i)
		teams.push_back({{"team", int(i)}, {"alliance", int(i)}});
	return Json{{"id", "5b0f3c1e-0000-4a7e-8a51-3d0a1c0b0001"},
				{"code", "KXQ742MNPR"},
				{"inviteUrl", ORIGIN + "/j/KXQ742MNPR"},
				{"name", "Room"},
				{"visibility", "link"},
				{"status", "open"},
				{"hostAccountId", host},
				{"simVersion", {{"versionMinor", 125}, {"netProtocol", 49}, {"dataHash", std::string(64, '0')}}},
				{"map",
				 {{"kind", "generated"},
				  {"generator",
				   {{"generatorId", "marchland"},
					{"revision", 1},
					{"params", {{"width", 8}, {"height", 8}, {"teams", int(seats.size())}}},
					{"seed", 7},
					{"candidates", 5},
					{"startingUnitLevel", 0}}}}},
				{"mapStatus", "pending"},
				{"teams", teams},
				{"seats", seats},
				{"rules", Json::object()},
				{"experiments", Json::array()},
				{"members", members},
				{"revision", revision},
				{"createdAt", "2026-10-01T20:00:00Z"}};
}

Json member(const std::string &id, const std::string &name)
{
	return Json{{"accountId", id}, {"displayName", name}, {"kind", "guest"}, {"connected", true}};
}

struct Fixture
{
	glob2test::HeadlessGlobals globals{[] {
		glob2test::GlobalsOptions o;
		o.loadStrings = true;
		return o;
	}()};
	OnlineFakes::World world;
	MemoryStorage storage;
	InstanceConfig config{storage};
	PlatformClient client{config, options(), world.environment()};
	MapCache maps{storage, [this](HttpFetch::Request request) { return world.http.start(std::move(request)); }};

	Fixture()
	{
		client.start(ORIGIN);
		auto guest = world.http.pending("/api/v1/auth/guest");
		REQUIRE(guest);
		guest->reply(200, Json{{"account", OnlineFakes::account()},
							   {"tokens", OnlineFakes::tokens("r1", 1790000000, 600)},
							   {"deviceCredential", std::string(43, 'c')}});
		client.update();
		world.socket().state = NetTransport::State::Connected;
		client.update();
		auto hello = world.socket().find("session.hello");
		REQUIRE(!hello.is_null());
		world.socket().respond(hello, Json{{"sessionId", "00000000-0000-4000-8000-0000000000aa"},
										   {"serverTime", "2026-09-21T14:13:20Z"},
										   {"simSupported", true},
										   {"account", OnlineFakes::account()}});
		client.update();
		REQUIRE(client.connection() == PlatformClient::Connection::Online);
	}
	// Joins a room and answers with `state`.
	std::shared_ptr<PlatformRoom> join(const Json &state)
	{
		auto r = PlatformRoom::join(client, maps, storage, "KXQ742MNPR");
		client.update();
		auto request = world.socket().find("room.join");
		REQUIRE(!request.is_null());
		world.socket().respond(request, Json{{"room", state}});
		client.update();
		return r;
	}
	std::vector<std::string> chat(RoomBackend &r)
	{
		std::vector<std::string> lines;
		while (auto e = r.takeEvent())
			if (e->kind == RoomBackend::Event::Chat)
				lines.push_back(e->text);
		return lines;
	}
};
} // namespace

TEST_SUITE("PlatformRoom")
{
	TEST_CASE("a premade map is uploaded and chosen for the room, then named by the server")
	{
		Fixture f;
		auto r = f.join(room(3, ME, Json::array({seat(0, ME, "Guest-1234"), seat(1, "", "")}),
							 Json::array({member(ME, "Guest-1234")})));
		REQUIRE(r->isHost());
		f.chat(*r);

		CustomGameSetup setup;
		setup.random = false;
		setup.setCapacity(2);
		setup.prestige = false;
		const std::string bytes = "GLOB2 MAP BYTES";
		r->useMapBytes(bytes, "balanced for 2", setup);
		CHECK(r->uploadingMap());
		CHECK(r->waitingFor() == GAGCore::Toolkit::getStringTable()->getString("[room uploading map]"));
		f.client.update();
		auto upload = f.world.http.pending("&fileName=balanced%20for%202.map");
		REQUIRE(upload);
		CHECK(upload->request.method == HttpFetch::Method::Post);
		CHECK(upload->request.url.find("/api/v1/uploads?format=map&simVersion=") != std::string::npos);
		CHECK(upload->request.body == bytes);
		CHECK(upload->header("Content-Type") == "application/octet-stream");
		upload->reply(201, Json{{"id", "00000000-0000-4000-8000-0000000000ff"},
								{"format", "map"},
								{"sha256", HASH},
								{"status", "pending"}});
		f.client.update();
		CHECK_FALSE(r->uploadingMap());
		// The room switches to the uploaded file, not to a generated map.
		auto update = f.world.socket().find("room.update");
		REQUIRE(!update.is_null());
		const Json &changes = update["params"]["changes"];
		CHECK(changes["map"] == Json{{"kind", "upload"}, {"format", "map"}, {"hash", HASH}});
		CHECK(changes["rules"]["prestigeVictory"] == false);

		// The answer: pending while the engine validates the upload. The host names the
		// map by its file until the server reports a title.
		Json next = room(4, ME, Json::array({seat(0, ME, "Guest-1234"), seat(1, "", "")}),
						 Json::array({member(ME, "Guest-1234")}));
		next["map"] = {{"kind", "upload"}, {"format", "map"}, {"hash", HASH}};
		f.world.socket().respond(update, Json{{"room", next}});
		f.client.update();
		CHECK(r->mapName() == "balanced for 2");
		CHECK_FALSE(r->generatedMap());
		auto lines = f.chat(*r);
		CHECK(std::count_if(lines.begin(), lines.end(), [](const std::string &l) { return l.find("balanced for 2") != std::string::npos; }) == 1);

		// Validated: the server's title wins, and the same choice is not announced again.
		next["revision"] = 5;
		next["mapStatus"] = "ready";
		next["mapTitle"] = "Balanced (2 players)";
		f.world.socket().event("room.state", Json{{"room", next}});
		f.client.update();
		CHECK(r->mapName() == "Balanced (2 players)");
		CHECK(f.chat(*r).empty());

		// Changing only the rules keeps the premade map...
		CustomGameSetup rules;
		REQUIRE(r->setupDraft(rules));
		rules.random = true; // as the room editor opens
		rules.prestige = true;
		r->applySetup(rules);
		f.client.update();
		auto rulesOnly = f.world.socket().find("room.update");
		CHECK(rulesOnly["params"]["changes"]["rules"]["prestigeVictory"] == true);
		CHECK_FALSE(rulesOnly["params"]["changes"].contains("map"));
		// ...while a random map chosen in "Change map…" replaces it.
		r->useGeneratedMap(rules);
		f.client.update();
		auto generated = f.world.socket().find("room.update");
		REQUIRE(generated["params"]["changes"].contains("map"));
		CHECK(generated["params"]["changes"]["map"]["kind"] == "generated");
	}

	// (A generated map gaining its hash when generation finishes is the same choice and is
	// not announced again; that path downloads the map, so it is covered end to end by
	// OnlinePlayHarness rather than here.)
	TEST_CASE("the server's start notice is shown once in the room chat")
	{
		Fixture f;
		Json state = room(3, ME, Json::array({seat(0, ME, "Guest-1234"), seat(1, "", "")}),
						  Json::array({member(ME, "Guest-1234")}));
		auto r = f.join(state);
		f.chat(*r);
		// The server reopened the room after an interrupted start.
		state["revision"] = 4;
		state["notice"] = "The match could not be started because the server was interrupted. Start it again.";
		f.world.socket().event("room.state", Json{{"room", state}});
		f.client.update();
		CHECK(f.chat(*r) == std::vector<std::string>{state["notice"].get<std::string>()});
		state["revision"] = 5;
		f.world.socket().event("room.state", Json{{"room", state}});
		f.client.update();
		CHECK(f.chat(*r).empty());
	}

	TEST_CASE("a map change is announced once; a status change is not")
	{
		Fixture f;
		Json state = room(3, ME, Json::array({seat(0, ME, "Guest-1234"), seat(1, "", "")}),
						  Json::array({member(ME, "Guest-1234")}));
		auto r = f.join(state);
		f.chat(*r);
		state["revision"] = 4;
		state["map"]["generator"]["seed"] = 8;
		f.world.socket().event("room.state", Json{{"room", state}});
		f.client.update();
		CHECK(f.chat(*r).size() == 1);
		state["revision"] = 5;
		state["mapStatus"] = "failed";
		f.world.socket().event("room.state", Json{{"room", state}});
		f.client.update();
		CHECK(f.chat(*r).empty());
	}

	TEST_CASE("a member without a seat is listed, and Ready says why it is unavailable")
	{
		Fixture f;
		// Every seat taken: this client joined late.
		auto r = f.join(room(3, OTHER, Json::array({seat(0, OTHER, "Bradley"), seat(1, OTHER + "x", "Ana")}),
							 Json::array({member(OTHER, "Bradley"), member(ME, "Guest-1234")})));
		CHECK_FALSE(r->isHost());
		CHECK(r->localUnseated());
		const auto unseated = r->unseatedMembers();
		REQUIRE(unseated.size() == 1);
		CHECK(unseated[0].find("Guest-1234") == 0);
		CHECK(r->readyBlocker() == GAGCore::Toolkit::getStringTable()->getString("[room waiting for a seat]"));
		CHECK(r->waitingFor() == r->readyBlocker());

		// A seat opens: take it.
		Json open = room(4, OTHER, Json::array({seat(0, OTHER, "Bradley"), seat(1, "", "")}),
						 Json::array({member(OTHER, "Bradley"), member(ME, "Guest-1234")}));
		f.world.socket().event("room.state", Json{{"room", open}});
		f.client.update();
		CHECK(r->readyBlocker() == GAGCore::Toolkit::getStringTable()->getString("[room take a seat to play]"));

		// Seated: nothing blocks Ready and nobody is listed apart.
		Json seated = room(5, OTHER, Json::array({seat(0, OTHER, "Bradley"), seat(1, ME, "Guest-1234")}),
						   Json::array({member(OTHER, "Bradley"), member(ME, "Guest-1234")}));
		f.world.socket().event("room.state", Json{{"room", seated}});
		f.client.update();
		CHECK_FALSE(r->localUnseated());
		CHECK(r->unseatedMembers().empty());
		CHECK(r->readyBlocker().empty());
	}
}
