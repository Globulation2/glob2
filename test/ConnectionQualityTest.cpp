// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// The game's connection-quality table (src/gui/ConnectionQuality.h) against the
// platform's (platform/packages/protocol/src/connectionQuality.ts), through the JSON
// the protocol package generates. test/fixtures/protocol holds a copy so the test runs
// without the platform workspace; when the workspace is present the copy must match it.

#include "Glob2Test.h"

#include <filesystem>
#include <nlohmann/json.hpp>

#include "ConnectionQuality.h"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace ConnectionQuality;

namespace
{
json table()
{
	return json::parse(glob2test::readFile(glob2test::fixture("protocol/connection-quality.json")));
}

Metric metric(const std::string &id)
{
	if (id == "ping")
		return Metric::Ping;
	if (id == "delay")
		return Metric::Delay;
	REQUIRE(id == "behind");
	return Metric::Behind;
}
} // namespace

TEST_SUITE("connection quality")
{
	TEST_CASE("the table matches the platform's")
	{
		const json t = table();
		REQUIRE(t.at("formatVersion") == 1);
		CHECK(t.at("metrics").size() == 3);
		for (Metric m : {Metric::Ping, Metric::Delay, Metric::Behind})
		{
			const Limits l = limits(m);
			INFO(l.id);
			const json &entry = t.at("metrics").at(l.id);
			CHECK(entry.at("fairMs") == l.fairMs);
			CHECK(entry.at("poorMs") == l.poorMs);
			CHECK(entry.at("unit") == (l.seconds ? "s" : "ms"));
		}
		CHECK(t.at("ratings").at("good") == "Good");
		CHECK(t.at("ratings").at("fair") == "Fair");
		CHECK(t.at("ratings").at("poor") == "Poor");
	}

	TEST_CASE("every shared case rates and formats the same")
	{
		const json t = table();
		REQUIRE(t.at("cases").size() >= 10);
		for (const json &c : t.at("cases"))
		{
			const Metric m = metric(c.at("metric"));
			const int value = c.at("valueMs");
			INFO(c.dump());
			CHECK(id(rate(m, value)) == c.at("rating").get<std::string>());
			CHECK(format(m, value) == c.at("text").get<std::string>());
		}
	}

	TEST_CASE("the review's contradiction is gone: 171 ms delay is good, 760 ms behind is good, 1332 ms ping is poor")
	{
		CHECK(rate(Metric::Delay, 171) == Rating::Good);
		CHECK(rate(Metric::Behind, 760) == Rating::Good);
		CHECK(rate(Metric::Ping, 1332) == Rating::Poor);
		CHECK(worst(Rating::Fair, Rating::Good) == Rating::Fair);
		CHECK(worst(Rating::Fair, Rating::Poor) == Rating::Poor);
	}

	TEST_CASE("the vendored copy matches the protocol package when it is present")
	{
		const fs::path source = glob2test::sourceRoot() / "platform/packages/protocol/fixtures/connection-quality.json";
		if (!fs::exists(source))
			return;
		CHECK(glob2test::readFile(source) == glob2test::readFile(glob2test::fixture("protocol/connection-quality.json")));
	}
}
