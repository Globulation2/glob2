// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Ticket verification against the protocol package's fixtures (every expected
// rejection reason) and against tickets signed here with the same TEST-ONLY key.

#include "doctest.h"

#include "RelayTestSupport.h"
#include "TicketVerifier.h"

#include <nlohmann/json.hpp>

using nlohmann::json;
using namespace Relay;
using RelayTest::SigningKey;

namespace
{
	constexpr std::int64_t VERIFY_AT = 1790000060;

	std::shared_ptr<const KeySet> fixtureKeys()
	{
		return KeySet::parse(RelayTest::readFixture("jwks.json"));
	}

	VerifyOptions at(std::int64_t now)
	{
		VerifyOptions o;
		o.nowSeconds = now;
		return o;
	}
}

TEST_SUITE("RelayTicket")
{
	TEST_CASE("every protocol fixture verifies or fails with the manifest's reason")
	{
		const json manifest = json::parse(RelayTest::readFixture("manifest.json"))["tickets"];
		const auto keys = KeySet::parse(RelayTest::readFixture(manifest["jwks"].get<std::string>()));
		VerifyOptions options = at(manifest["verifyAt"].get<std::int64_t>());
		options.leewaySeconds = manifest["leewaySeconds"].get<std::int64_t>();
		REQUIRE(manifest["tickets"].size() >= 8);
		for (const auto& entry : manifest["tickets"])
		{
			const std::string file = entry["file"].get<std::string>();
			CAPTURE(file);
			const TicketResult result = verifyTicket(RelayTest::readFixture(file), *keys, options);
			if (entry["valid"].get<bool>())
				CHECK(result.ok());
			else
				CHECK(std::string(ticketErrorName(result.error)) == entry["reason"].get<std::string>());
		}
	}

	TEST_CASE("the valid fixture yields the expected claims")
	{
		const json expected = RelayTest::fixtureClaims();
		const TicketResult r = verifyTicket(RelayTest::readFixture("valid.jwt"), *fixtureKeys(), at(VERIFY_AT));
		REQUIRE(r.ok());
		CHECK(r.kid == "fixture-key-1");
		CHECK(r.claims.matchId == expected["matchId"].get<std::string>());
		CHECK(r.claims.seat == expected["seat"].get<int>());
		CHECK(r.claims.accountId == expected["accountId"].get<std::string>());
		CHECK(r.claims.subject == expected["sub"].get<std::string>());
		CHECK(r.claims.relayUrl == expected["relayUrl"].get<std::string>());
		CHECK(r.claims.issuer == expected["iss"].get<std::string>());
		CHECK(r.claims.expiresAt == expected["exp"].get<std::int64_t>());
		CHECK(r.claims.simVersion.versionMinor == expected["simVersion"]["versionMinor"].get<int>());
		CHECK(r.claims.simVersion.netProtocol == expected["simVersion"]["netProtocol"].get<int>());
		CHECK(r.claims.simVersion.dataHash == expected["simVersion"]["dataHash"].get<std::string>());
		CHECK(r.claims.simVersion.key() ==
		      "125-49-3f9a6c1e8b2d47a05e6f1c2b3a4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f");
		CHECK(r.claims.humanSeatMask == 0b11u);
	}

	TEST_CASE("the TEST-ONLY seed reproduces the fixture JWKS key")
	{
		SigningKey key(SigningKey::fixtureSeed());
		const json jwks = json::parse(RelayTest::readFixture("jwks.json"));
		CHECK(key.publicX() == jwks["keys"][0]["x"].get<std::string>());
		// And a ticket signed here verifies like the fixture.
		const auto token = key.sign(RelayTest::ticketHeader(), RelayTest::fixtureClaims());
		CHECK(verifyTicket(token, *fixtureKeys(), at(VERIFY_AT)).ok());
	}

	TEST_CASE("expiry and not-before honour the 30 second leeway exactly")
	{
		SigningKey key(SigningKey::fixtureSeed());
		json claims = RelayTest::fixtureClaims();
		const std::int64_t exp = claims["exp"].get<std::int64_t>();
		const auto token = key.sign(RelayTest::ticketHeader(), claims);
		CHECK(verifyTicket(token, *fixtureKeys(), at(exp + 29)).ok());
		CHECK(verifyTicket(token, *fixtureKeys(), at(exp + 30)).error == TicketError::Expired);

		claims["nbf"] = VERIFY_AT + 100;
		const auto early = key.sign(RelayTest::ticketHeader(), claims);
		CHECK(verifyTicket(early, *fixtureKeys(), at(VERIFY_AT + 70)).ok());
		CHECK(verifyTicket(early, *fixtureKeys(), at(VERIFY_AT + 69)).error == TicketError::NotYetValid);

		claims.erase("exp");
		CHECK(verifyTicket(key.sign(RelayTest::ticketHeader(), claims), *fixtureKeys(), at(VERIFY_AT)).error ==
		      TicketError::Expired);
	}

	TEST_CASE("access tokens and other JWT types are refused")
	{
		SigningKey key(SigningKey::fixtureSeed());
		json header = RelayTest::ticketHeader();
		header["typ"] = "at+jwt";
		CHECK(verifyTicket(key.sign(header, RelayTest::fixtureClaims()), *fixtureKeys(), at(VERIFY_AT)).error ==
		      TicketError::Type);
		header.erase("typ");
		CHECK(verifyTicket(key.sign(header, RelayTest::fixtureClaims()), *fixtureKeys(), at(VERIFY_AT)).error ==
		      TicketError::Type);
		header = RelayTest::ticketHeader();
		header["alg"] = "ES256";
		CHECK(verifyTicket(key.sign(header, RelayTest::fixtureClaims()), *fixtureKeys(), at(VERIFY_AT)).error ==
		      TicketError::Algorithm);
	}

	TEST_CASE("claim shape is checked after the signature")
	{
		SigningKey key(SigningKey::fixtureSeed());
		auto verdict = [&](json claims) {
			return verifyTicket(key.sign(RelayTest::ticketHeader(), claims), *fixtureKeys(), at(VERIFY_AT));
		};
		json c = RelayTest::fixtureClaims();
		c["seat"] = 2; // not in humanSeats [0, 1]
		CHECK(verdict(c).error == TicketError::Claims);
		c = RelayTest::fixtureClaims();
		c["seat"] = 12;
		CHECK(verdict(c).error == TicketError::Claims);
		c = RelayTest::fixtureClaims();
		c["sub"] = "00000000-0000-4000-8000-000000000000";
		CHECK(verdict(c).error == TicketError::Claims);
		c = RelayTest::fixtureClaims();
		c["matchId"] = "7E3C1D2B-9A8F-4E6D-8C5B-4A3F2E1D0C9B";
		CHECK(verdict(c).error == TicketError::Claims);
		c = RelayTest::fixtureClaims();
		c["humanSeats"] = json::array({0, 1, 1});
		CHECK(verdict(c).error == TicketError::Claims);
		c = RelayTest::fixtureClaims();
		c["humanSeats"] = json::array();
		CHECK(verdict(c).error == TicketError::Claims);
		c = RelayTest::fixtureClaims();
		c["simVersion"]["dataHash"] = "3F9A";
		CHECK(verdict(c).error == TicketError::Claims);
		c = RelayTest::fixtureClaims();
		c["simVersion"]["extra"] = 1;
		CHECK(verdict(c).error == TicketError::Claims);
		c = RelayTest::fixtureClaims();
		c["relayUrl"] = "ftp://relay";
		CHECK(verdict(c).error == TicketError::Claims);
		c = RelayTest::fixtureClaims();
		c["aud"] = json::array({"glob2-relay"});
		CHECK(verdict(c).error == TicketError::Audience);
		c = RelayTest::fixtureClaims();
		c["entitlements"] = json::array({"anything"}); // ignored
		CHECK(verdict(c).ok());
	}

	TEST_CASE("an issuer can be required")
	{
		VerifyOptions options = at(VERIFY_AT);
		options.issuer = "https://play.example.org";
		CHECK(verifyTicket(RelayTest::readFixture("valid.jwt"), *fixtureKeys(), options).ok());
		options.issuer = "https://elsewhere.example";
		CHECK(verifyTicket(RelayTest::readFixture("valid.jwt"), *fixtureKeys(), options).error == TicketError::Claims);
	}

	TEST_CASE("malformed tokens fail closed")
	{
		const auto keys = fixtureKeys();
		CHECK(verifyTicket("", *keys, at(VERIFY_AT)).error == TicketError::Malformed);
		CHECK(verifyTicket("a.b", *keys, at(VERIFY_AT)).error == TicketError::Malformed);
		CHECK(verifyTicket("a.b.c.d", *keys, at(VERIFY_AT)).error == TicketError::Malformed);
		CHECK(verifyTicket("!!!.e30.AA", *keys, at(VERIFY_AT)).error == TicketError::Malformed);
		// A header that is JSON but not an object.
		CHECK(verifyTicket("WzFd.e30.AA", *keys, at(VERIFY_AT)).error == TicketError::Malformed);
		const std::string valid = RelayTest::readFixture("valid.jwt");
		const auto lastDot = valid.rfind('.');
		CHECK(verifyTicket(valid.substr(0, lastDot) + ".", *keys, at(VERIFY_AT)).error == TicketError::Signature);
		CHECK(ticketKeyId(valid) == "fixture-key-1");
		CHECK(ticketKeyId("garbage") == "");
	}

	TEST_CASE("key rotation: a JWKS with old and new keys accepts both; unknown kids are named")
	{
		SigningKey oldKey(SigningKey::fixtureSeed());
		std::vector<std::uint8_t> seed(32, 7);
		SigningKey newKey(seed);
		const std::string both = "{\"keys\":[" + oldKey.jwk("fixture-key-1") + "," + newKey.jwk("key-2") + "]}";
		const auto keys = KeySet::parse(both);
		CHECK(keys->size() == 2);
		CHECK(verifyTicket(oldKey.sign(RelayTest::ticketHeader("fixture-key-1"), RelayTest::fixtureClaims()), *keys,
		                   at(VERIFY_AT)).ok());
		CHECK(verifyTicket(newKey.sign(RelayTest::ticketHeader("key-2"), RelayTest::fixtureClaims()), *keys,
		                   at(VERIFY_AT)).ok());
		// The new key under the old kid does not verify.
		CHECK(verifyTicket(newKey.sign(RelayTest::ticketHeader("fixture-key-1"), RelayTest::fixtureClaims()), *keys,
		                   at(VERIFY_AT)).error == TicketError::Signature);
		const auto onlyOld = fixtureKeys();
		const TicketResult unknown =
			verifyTicket(newKey.sign(RelayTest::ticketHeader("key-2"), RelayTest::fixtureClaims()), *onlyOld, at(VERIFY_AT));
		CHECK(unknown.error == TicketError::Key);
		CHECK(unknown.kid == "key-2");
	}

	TEST_CASE("JWKS parsing skips foreign keys and rejects broken Ed25519 keys")
	{
		const auto keys = KeySet::parse(
			R"({"keys":[{"kty":"RSA","kid":"rsa","n":"AQAB","e":"AQAB"},)"
			R"({"kty":"OKP","crv":"X25519","kid":"x","x":"SV8bVfvhVyPmCYGxl3fL48HQ_laoUTAD56ovXFL-jNI"},)"
			R"({"kty":"OKP","crv":"Ed25519","kid":"enc","use":"enc","x":"SV8bVfvhVyPmCYGxl3fL48HQ_laoUTAD56ovXFL-jNI"},)"
			R"({"kty":"OKP","crv":"Ed25519","kid":"ok","x":"SV8bVfvhVyPmCYGxl3fL48HQ_laoUTAD56ovXFL-jNI"}]})");
		CHECK(keys->kids() == std::vector<std::string>{"ok"});
		CHECK_THROWS(KeySet::parse("{}"));
		CHECK_THROWS(KeySet::parse("not json"));
		CHECK_THROWS(KeySet::parse(R"({"keys":[{"kty":"OKP","crv":"Ed25519","kid":"k","x":"AAAA"}]})"));
		CHECK_THROWS(KeySet::parse(R"({"keys":[{"kty":"OKP","crv":"Ed25519","x":"SV8bVfvhVyPmCYGxl3fL48HQ_laoUTAD56ovXFL-jNI"}]})"));
	}

	TEST_CASE("base64url is strict and round-trips")
	{
		std::vector<std::uint8_t> out;
		for (std::size_t n = 0; n < 70; ++n)
		{
			std::vector<std::uint8_t> bytes(n);
			for (std::size_t i = 0; i < n; ++i)
				bytes[i] = static_cast<std::uint8_t>(i * 37 + n);
			const std::string text = base64UrlEncode(bytes.data(), bytes.size());
			CHECK(text.find('=') == std::string::npos);
			REQUIRE(base64UrlDecode(text, out));
			CHECK(out == bytes);
		}
		CHECK_FALSE(base64UrlDecode("AA==", out));
		CHECK_FALSE(base64UrlDecode("A", out));
		CHECK_FALSE(base64UrlDecode("AB", out)); // non-zero trailing bits
		CHECK_FALSE(base64UrlDecode("a+b/", out));
	}
}
