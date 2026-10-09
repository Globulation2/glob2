// SPDX-License-Identifier: GPL-3.0-or-later
// Realtime envelope codec, timestamps, token lifetimes, refresh scheduling,
// backoff and SHA-256.

#include "Glob2Test.h"
#include "OnlineFakes.h"
#include "PlatformProtocol.h"
#include "Sha256.h"

using namespace Online;

TEST_SUITE("PlatformProtocol")
{
	TEST_CASE("requests encode as the realtime envelope")
	{
		auto text = encodeRequest("c7", "room.join", Json{{"code", "ABCDEF12"}});
		auto json = Json::parse(text);
		CHECK_EQ(json, Json{{"type", "request"},
							{"id", "c7"},
							{"method", "room.join"},
							{"params", {{"code", "ABCDEF12"}}}});
		CHECK_EQ(Json::parse(encodeRequest("c1", "session.ping", Json()))["params"], Json::object());
	}

	TEST_CASE("responses and events decode; malformed frames are rejected")
	{
		auto ok = decodeServerMessage(R"({"type":"response","id":"c3","ok":true,"result":{"x":1}})");
		REQUIRE(ok.kind == ServerMessage::Kind::Response);
		CHECK_EQ(ok.id, "c3");
		CHECK(ok.ok);
		CHECK_EQ(ok.result["x"], 1);

		auto failed = decodeServerMessage(
			R"({"type":"response","id":"c4","ok":false,"error":{"code":"not_found","message":"No room.","details":{"a":2}}})");
		REQUIRE(failed.kind == ServerMessage::Kind::Response);
		CHECK_FALSE(failed.ok);
		CHECK_EQ(failed.error.code, "not_found");
		CHECK_EQ(failed.error.message, "No room.");
		CHECK_EQ(failed.error.details["a"], 2);

		auto localized = decodeServerMessage(
			R"({"type":"response","id":"c5","ok":false,"error":{"code":"not_found","message":"No room.","messageKey":"Room {id} not found.","messageParams":{"id":"ABCDEF"},"details":{"a":2}}})");
		REQUIRE(localized.kind == ServerMessage::Kind::Response);
		CHECK_FALSE(localized.ok);
		CHECK_EQ(localized.error.code, failed.error.code);
		CHECK_EQ(localized.error.message, failed.error.message);
		CHECK_EQ(localized.error.details, failed.error.details);

		auto event = decodeServerMessage(
			R"({"type":"event","event":"auth.handoff.failed","data":{"reason":"denied"},"extra":1})");
		REQUIRE(event.kind == ServerMessage::Kind::Event);
		CHECK_EQ(event.event, "auth.handoff.failed");
		CHECK_EQ(event.data["reason"], "denied");

		for (const char *bad :
			 {"", "[]", "not json", R"({"type":"response","ok":true,"result":{}})",
			  R"({"type":"response","id":7,"ok":true,"result":{}})",
			  R"({"type":"response","id":"c1","ok":true})",
			  R"({"type":"response","id":"c1","ok":false})", R"({"type":"event","event":"Bad","data":{}})",
			  R"({"type":"event","event":"room.state"})", R"({"type":"other"})"})
			CHECK_MESSAGE(decodeServerMessage(bad).kind == ServerMessage::Kind::Invalid, bad);
	}

	TEST_CASE("RFC 3339 timestamps convert to epoch milliseconds")
	{
		CHECK_EQ(parseTimestamp("1970-01-01T00:00:00Z").value(), 0);
		CHECK_EQ(parseTimestamp("2026-10-01T12:34:56.789Z").value(), 1790858096789);
		CHECK_EQ(parseTimestamp("2026-10-01T14:34:56.789+02:00").value(), 1790858096789);
		CHECK_EQ(parseTimestamp("2026-10-01T07:34:56.7-05:00").value(), 1790858096700);
		CHECK_EQ(parseTimestamp("2000-02-29T00:00:00Z").value(), 951782400000);
		for (const char *bad : {"", "2026-10-01", "2026-13-01T00:00:00Z", "2026-10-01T00:00:00",
								"2026-10-01T00:00:00Zjunk", "2026-10-01T00:00:00.Z"})
			CHECK_MESSAGE(!parseTimestamp(bad).has_value(), bad);
	}

	TEST_CASE("token lifetime comes from the JWT claims")
	{
		CHECK_EQ(tokenLifetimeMs(OnlineFakes::token(1000, 1600)).value(), 600000);
		CHECK_FALSE(tokenLifetimeMs("opaque").has_value());
		CHECK_FALSE(tokenLifetimeMs(OnlineFakes::token(1600, 1000)).has_value());
		auto claims = jwtClaims(OnlineFakes::token(1, 2, "someone"));
		REQUIRE(claims.has_value());
		CHECK_EQ((*claims)["sub"], "someone");
	}

	TEST_CASE("refreshes are scheduled well before expiry")
	{
		CHECK_EQ(refreshDelayMs(600000), 480000); // 10 min: 2 min early
		CHECK_EQ(refreshDelayMs(3600000), 2880000); // 1 h: a fifth early
		CHECK_EQ(refreshDelayMs(120000), 60000);	// a minute early, but not before halfway
		CHECK_EQ(refreshDelayMs(30000), 15000);
		CHECK_EQ(refreshDelayMs(100), 1000);
	}

	TEST_CASE("backoff grows exponentially to a cap, with bounded jitter")
	{
		Backoff backoff(1000, 30000, 2.0, 0.5);
		CHECK_EQ(backoff.next(0.0), 1000);
		CHECK_EQ(backoff.next(0.0), 2000);
		CHECK_EQ(backoff.next(0.0), 4000);
		CHECK_EQ(backoff.next(0.999999), 4000); // 8000 halved at most
		CHECK_EQ(backoff.next(0.0), 16000);
		CHECK_EQ(backoff.next(0.0), 30000);
		CHECK_EQ(backoff.next(0.5), 22500);
		for (int i = 0; i < 100; ++i)
		{
			const auto delay = backoff.next(0.25);
			CHECK(delay >= 15000);
			CHECK(delay <= 30000);
		}
		CHECK_EQ(backoff.attempts(), 107);
		backoff.reset();
		CHECK_EQ(backoff.next(0.0), 1000);
	}

	TEST_CASE("SHA-256 matches the FIPS 180-4 vectors")
	{
		CHECK_EQ(Sha256::hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
		CHECK_EQ(Sha256::hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
		CHECK_EQ(Sha256::hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
				 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
		Sha256 chunked;
		const std::string block(1000, 'a');
		for (int i = 0; i < 1000; ++i)
			chunked.update(block.data() + (i % 7), 0), chunked.update(block);
		CHECK_EQ(Sha256::toHex(chunked.finish()),
				 "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
		CHECK(Sha256::isHexDigest(Sha256::hex("x")));
		CHECK_FALSE(Sha256::isHexDigest("ABC"));
		CHECK_FALSE(Sha256::isHexDigest(std::string(64, 'g')));
	}

	TEST_CASE("the local sim version carries this build's versions")
	{
		auto version = SimVersion::local().toJson();
		CHECK(version["versionMinor"].is_number_integer());
		CHECK(version["netProtocol"].is_number_integer());
		CHECK(Sha256::isHexDigest(version["dataHash"].get<std::string>()));
		const std::string platform = clientPlatform();
		CHECK((platform == "desktop" || platform == "android" || platform == "ios" ||
			   platform == "browser"));
	}
}
