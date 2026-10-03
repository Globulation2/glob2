// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Match admission (one match per matchId, tickets must agree, tombstones, drain and
// capacity), the platform reports and the configuration surface.

#include "doctest.h"

#include "MatchDirectory.h"
#include "MatchReport.h"
#include "RelayConfig.h"
#include "RelayLog.h"
#include "RelayTestSupport.h"

#include <nlohmann/json.hpp>

using nlohmann::json;
using namespace Relay;

namespace
{
	TicketClaims claims(const std::string& matchId, int seat, std::uint32_t humanSeats = 0b11, int minor = 125)
	{
		TicketClaims c;
		c.matchId = matchId;
		c.seat = seat;
		c.humanSeatMask = humanSeats;
		c.simVersion.versionMinor = minor;
		c.simVersion.netProtocol = 49;
		c.simVersion.dataHash = std::string(64, 'a');
		c.expiresAt = 1000;
		return c;
	}

	const std::string A = "7e3c1d2b-9a8f-4e6d-8c5b-4a3f2e1d0c9b";
	const std::string B = "0b8f6f2e-3c4d-4e5f-8a9b-0c1d2e3f4a5b";
}

TEST_SUITE("RelayAdmission")
{
	TEST_CASE("the first valid ticket creates a match; agreeing tickets join it")
	{
		MatchDirectory d;
		CHECK(d.check(claims(A, 0), false, 10) == Admission::Create);
		d.create(claims(A, 0));
		CHECK(d.live(A));
		CHECK(d.check(claims(A, 1), false, 10) == Admission::Join);
		CHECK(d.check(claims(A, 0), false, 10) == Admission::Join); // reconnect with the same ticket
		CHECK(d.liveIds() == std::vector<std::string>{A});
	}

	TEST_CASE("tickets of one match must agree on simVersion and humanSeats")
	{
		MatchDirectory d;
		d.create(claims(A, 0));
		CHECK(d.check(claims(A, 1, 0b11, 126), false, 10) == Admission::SimVersionDiffers);
		auto otherHash = claims(A, 1);
		otherHash.simVersion.dataHash = std::string(64, 'b');
		CHECK(d.check(otherHash, false, 10) == Admission::SimVersionDiffers);
		CHECK(d.check(claims(A, 1, 0b111), false, 10) == Admission::HumanSeatsDiffer);
		CHECK(d.check(claims(A, 0, 0b1), false, 10) == Admission::HumanSeatsDiffer);
	}

	TEST_CASE("an ended match cannot be restarted by a late ticket until every ticket has expired")
	{
		MatchDirectory d;
		d.create(claims(A, 0));
		auto later = claims(A, 1);
		later.expiresAt = 50000;
		d.sawTicket(later);
		d.end(A, 2000, 30, 3600);
		CHECK_FALSE(d.live(A));
		CHECK(d.check(claims(A, 1), false, 10) == Admission::Ended);
		d.prune(50000 + 30);
		CHECK(d.ended(A));
		d.prune(50000 + 31);
		CHECK_FALSE(d.ended(A));
		// Minimum tombstone lifetime when tickets expire sooner.
		d.create(claims(B, 0));
		d.end(B, 2000, 30, 3600);
		d.prune(5600);
		CHECK(d.ended(B));
		d.prune(5601);
		CHECK_FALSE(d.ended(B));
	}

	TEST_CASE("draining admits reconnects to running matches only; capacity limits new matches")
	{
		MatchDirectory d;
		d.create(claims(A, 0));
		CHECK(d.check(claims(A, 1), true, 10) == Admission::Join);
		CHECK(d.check(claims(B, 0), true, 10) == Admission::Draining);
		CHECK(d.check(claims(B, 0), false, 1) == Admission::Full);
		CHECK(d.check(claims(A, 1), false, 1) == Admission::Join);
		CHECK(std::string(admissionName(Admission::HumanSeatsDiffer)) == "human_seats_differ");
	}

	TEST_CASE("RelayMatchEnded summarises seats, desync and the record")
	{
		Turn::MatchRecord record;
		record.matchId = A;
		record.humanSeatMask = 0b111;
		record.endTick = 4500;
		record.flags = Turn::MatchRecord::FLAG_DESYNC_FLAGGED;
		using K = Turn::MatchEventKind;
		record.events = {{10, 0, K::Connected}, {11, 1, K::Connected}, {12, 2, K::Connected},
		                 {100, 1, K::Disconnected}, {150, 1, K::Connected}, {200, 1, K::Disconnected},
		                 {300, 2, K::ToldToRejoin}, {4000, 0, K::LeftByQuit}, {4400, 1, K::LeftByGrace}};
		const std::vector<std::uint8_t> bytes{1, 2, 3};
		MatchEndInfo info;
		info.matchId = A;
		info.relayId = "relay-eu1-a";
		info.simVersion = claims(A, 0).simVersion;
		info.startedAt = 1790000005;
		info.endedAt = 1790001805;
		info.reason = EndReason::Completed;
		const json j = json::parse(matchEndedJson(info, record, bytes));
		CHECK(j["matchId"] == A);
		CHECK(j["relayId"] == "relay-eu1-a");
		CHECK(j["startedAt"] == "2026-09-21T14:13:25Z");
		CHECK(j["endedAt"] == "2026-09-21T14:43:25Z");
		CHECK(j["finalTick"] == 4500);
		CHECK(j["reason"] == "completed");
		CHECK(j["simVersion"]["versionMinor"] == 125);
		CHECK(j["simVersion"]["dataHash"] == std::string(64, 'a'));
		REQUIRE(j["seats"].size() == 3);
		CHECK(j["seats"][0] == json{{"seat", 0}, {"disconnects", 0}, {"droppedForDesync", false}, {"quitTick", 4000}});
		CHECK(j["seats"][1] == json{{"seat", 1}, {"disconnects", 2}, {"droppedForDesync", false}, {"quitTick", 4400}});
		CHECK(j["seats"][2] == json{{"seat", 2}, {"disconnects", 0}, {"droppedForDesync", true}});
		CHECK(j["desync"] == json{{"flagged", true}, {"minoritySeats", {2}}});
		CHECK(j["record"]["sha256"] == "039058c6f2c0cb492c533b0a4d14ef77cc0f78abccced5287d84a1a2011cfb81");
		CHECK(j["record"]["size"] == 3);
		CHECK(j["record"]["formatVersion"] == 1);
		// Exactly the RelayMatchEnded properties; the schema rejects anything else.
		std::vector<std::string> keys;
		for (auto it = j.begin(); it != j.end(); ++it)
			keys.push_back(it.key());
		CHECK(keys == std::vector<std::string>{"desync", "endedAt", "finalTick", "matchId", "reason", "record", "relayId",
		                                       "seats", "simVersion", "startedAt"});
	}

	TEST_CASE("registration and heartbeat bodies carry the protocol's fields")
	{
		RegistrationInfo info{"relay-eu1-a", "wss://relay-eu1.play.example.org/relay", "eu-west", "glob2-relay test", 200};
		const json r = json::parse(registrationJson(info, RelayLoad{1, 2}, false));
		CHECK(r["relayId"] == "relay-eu1-a");
		CHECK(r["publicUrl"] == "wss://relay-eu1.play.example.org/relay");
		CHECK(r["region"] == "eu-west");
		CHECK(r["turnProtocol"] == Turn::PROTOCOL_VERSION);
		CHECK(r["capacity"] == json{{"maxMatches", 200}});
		CHECK(r["load"] == json{{"matches", 1}, {"connections", 2}});
		CHECK(r["draining"] == false);
		const json h = json::parse(heartbeatJson("relay-eu1-a", RelayLoad{1, 2}, true, {A}));
		CHECK(h == json{{"relayId", "relay-eu1-a"},
		                {"load", {{"matches", 1}, {"connections", 2}}},
		                {"draining", true},
		                {"activeMatchIds", {A}}});
	}

	TEST_CASE("the map hash comes from MatchSetup.map.hash")
	{
		std::array<std::uint8_t, 32> hash{};
		const std::string setup =
			R"({"schemaVersion":1,"map":{"kind":"catalog","hash":"00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff"}})";
		REQUIRE(setupMapHash(setup, hash));
		CHECK(hash[0] == 0x00);
		CHECK(hash[1] == 0xff);
		CHECK_FALSE(setupMapHash(R"({"map":{"hash":"00FF"}})", hash));
		CHECK_FALSE(setupMapHash("[]", hash));
	}

	TEST_CASE("configuration validates its environment")
	{
		std::map<std::string, std::string> env{{"GLOB2_RELAY_JWKS_FILE", "/tmp/jwks.json"}};
		RelayConfig c = RelayConfig::fromMap(env);
		CHECK(c.port == 7495);
		CHECK(c.route == "/relay");
		CHECK_FALSE(c.tlsEnabled());
		CHECK(c.leewaySeconds == 30);
		CHECK_THROWS(RelayConfig::fromMap({}));
		env["GLOB2_RELAY_PLATFORM_URL"] = "https://platform.example/";
		CHECK_THROWS(RelayConfig::fromMap(env)); // needs a public URL and a key
		env["GLOB2_RELAY_PUBLIC_URL"] = "wss://relay.example/relay";
		env["GLOB2_RELAY_KEY"] = "secret";
		c = RelayConfig::fromMap(env);
		CHECK(c.platformUrl == "https://platform.example");
		env["GLOB2_RELAY_REGION"] = "EU";
		CHECK_THROWS(RelayConfig::fromMap(env));
		env["GLOB2_RELAY_REGION"] = "eu-west";
		env["GLOB2_RELAY_TLS_CERT"] = "/cert.pem";
		CHECK_THROWS(RelayConfig::fromMap(env)); // certificate without key
		env.erase("GLOB2_RELAY_TLS_CERT");
		env["GLOB2_RELAY_PORT"] = "70000";
		CHECK_THROWS(RelayConfig::fromMap(env));
		env["GLOB2_RELAY_PORT"] = "0";
		env["GLOB2_RELAY_ALLOWED_ORIGINS"] = "https://a.example, https://b.example";
		c = RelayConfig::fromMap(env);
		CHECK(c.allowedOrigins == std::vector<std::string>{"https://a.example", "https://b.example"});
	}
}
