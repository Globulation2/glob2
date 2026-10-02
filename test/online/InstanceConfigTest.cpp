// SPDX-License-Identifier: GPL-3.0-or-later
// Instance origins, per-instance credentials and trust, and their persistence.

#include "Glob2Test.h"
#include "InstanceConfig.h"
#include "OnlineStorage.h"

#include <nlohmann/json.hpp>

using namespace Online;

TEST_SUITE("InstanceConfig")
{
	TEST_CASE("origins normalize to scheme, host and non-default port")
	{
		CHECK_EQ(normalizeOrigin("https://Play.Example.ORG").value(), "https://play.example.org");
		CHECK_EQ(normalizeOrigin("HTTPS://play.example.org/").value(), "https://play.example.org");
		CHECK_EQ(normalizeOrigin("https://play.example.org:443").value(), "https://play.example.org");
		CHECK_EQ(normalizeOrigin("https://play.example.org:8443").value(),
				 "https://play.example.org:8443");
		CHECK_EQ(normalizeOrigin("https://[2001:DB8::1]:8443").value(), "https://[2001:db8::1]:8443");
		CHECK_EQ(normalizeOrigin("https://[2001:db8::1]").value(), "https://[2001:db8::1]");
		CHECK_EQ(normalizeOrigin("http://localhost:8080").value(), "http://localhost:8080");
		CHECK_EQ(normalizeOrigin("http://127.0.0.1:80").value(), "http://127.0.0.1");
		for (const char *bad :
			 {"", "play.example.org", "ftp://play.example.org", "http://play.example.org",
			  "https://play.example.org/path", "https://user@play.example.org",
			  "https://play.example.org?x=1", "https://play.example.org#x", "https://play example.org",
			  "https://play.example.org:0", "https://play.example.org:70000", "https://play.example.org:",
			  "https://", "https://-bad.example.org", "https://a..b", "https://[::1", "glob2://join"})
			CHECK_MESSAGE(!normalizeOrigin(bad).has_value(), bad);
		CHECK_EQ(realtimeUrl("https://play.example.org:8443"), "wss://play.example.org:8443/realtime");
		CHECK_EQ(realtimeUrl("http://localhost:8080"), "ws://localhost:8080/realtime");
		CHECK_EQ(apiUrl("https://a.example", "/api/v1/auth/guest"), "https://a.example/api/v1/auth/guest");
		CHECK(normalizeOrigin(OFFICIAL_INSTANCE_ORIGIN).value() == OFFICIAL_INSTANCE_ORIGIN);
	}

	TEST_CASE("defaults to the official instance with nothing remembered")
	{
		MemoryStorage storage;
		InstanceConfig config(storage);
		CHECK_FALSE(config.load());
		CHECK_EQ(config.selectedOrigin(), OFFICIAL_INSTANCE_ORIGIN);
		CHECK(config.instances().empty());
		CHECK(config.isTrusted(OFFICIAL_INSTANCE_ORIGIN));
		CHECK_FALSE(config.isTrusted("https://other.example"));
	}

	TEST_CASE("selection, credentials and trust persist per instance")
	{
		MemoryStorage storage;
		{
			InstanceConfig config(storage);
			CHECK_FALSE(config.selectInstance("not an origin"));
			REQUIRE(config.selectInstance("https://Self.Hosted.example:8443/"));
			auto &self = config.record("https://self.hosted.example:8443");
			self.deviceCredential = std::string(43, 'd');
			self.refreshToken = "refresh-1";
			self.lastDisplayName = "Guest-0042";
			auto &official = config.record(OFFICIAL_INSTANCE_ORIGIN);
			official.refreshToken = "official-refresh";
			official.autoSignIn = false;
			config.trust("https://friend.example", true);
			config.trust("https://once.example", false);
			CHECK(config.isTrusted("https://once.example"));
			REQUIRE(config.save());
			CHECK_EQ(storage.persisted, 1);
		}
		REQUIRE(storage.files.count(InstanceConfig::FILE_NAME));
		InstanceConfig loaded(storage);
		REQUIRE(loaded.load());
		CHECK_EQ(loaded.selectedOrigin(), "https://self.hosted.example:8443");
		CHECK(loaded.isTrusted("https://self.hosted.example:8443")); // the selected instance
		const auto *self = loaded.find("https://self.hosted.example:8443");
		REQUIRE(self);
		CHECK_EQ(self->deviceCredential, std::string(43, 'd'));
		CHECK_EQ(self->refreshToken, "refresh-1");
		CHECK_EQ(self->lastDisplayName, "Guest-0042");
		CHECK(self->autoSignIn);
		const auto *official = loaded.find(OFFICIAL_INSTANCE_ORIGIN);
		REQUIRE(official);
		CHECK_EQ(official->refreshToken, "official-refresh");
		CHECK_FALSE(official->autoSignIn);
		CHECK(loaded.isTrusted("https://friend.example"));
		CHECK_FALSE(loaded.isTrusted("https://once.example")); // not remembered
		loaded.untrust("https://friend.example");
		CHECK_FALSE(loaded.isTrusted("https://friend.example"));
		loaded.forget("https://self.hosted.example:8443");
		CHECK_FALSE(loaded.find("https://self.hosted.example:8443"));
	}

	TEST_CASE("malformed files keep the configuration; unknown fields and bad origins are skipped")
	{
		MemoryStorage storage;
		InstanceConfig config(storage);
		config.selectInstance("https://kept.example");
		CHECK_FALSE(config.fromJson("{not json"));
		CHECK_FALSE(config.fromJson("[]"));
		CHECK_EQ(config.selectedOrigin(), "https://kept.example");
		REQUIRE(config.fromJson(R"({"version":9,"future":true,"selected":"http://evil.example",
			"instances":{"https://ok.example":{"refreshToken":"r","trusted":true,"newField":1},
			"http://evil.example":{"refreshToken":"x"},"https://bad.example":"string"}})"));
		CHECK_EQ(config.selectedOrigin(), OFFICIAL_INSTANCE_ORIGIN);
		CHECK_EQ(config.instances().size(), 1);
		CHECK_EQ(config.find("https://ok.example")->refreshToken, "r");
	}

	TEST_CASE("a failed write is reported and not persisted")
	{
		MemoryStorage storage;
		storage.failWrites = true;
		InstanceConfig config(storage);
		CHECK_FALSE(config.save());
		CHECK_EQ(storage.persisted, 0);
	}
}
