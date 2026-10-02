// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// What the online results card and the hub's Recent matches show: the phases of a
// result (waiting for players, verifying, settled) with their "taking longer" flags,
// and the merge of the history API's page with summaries seen live.

#include "Glob2Test.h"

#include "OnlineMatch.h"

using Online::Json;
using Online::OnlineMatchResult;

namespace
{
Json summary(const std::string &id, const std::string &status, const std::string &verification)
{
	return Json{{"id", id}, {"status", status}, {"verification", verification}, {"rated", false},
				{"origin", "room"}, {"participants", Json::array()}};
}
} // namespace

TEST_SUITE("OnlineResult")
{
	TEST_CASE("a result waits for the players, then for the verifier, and says when it is slow")
	{
		OnlineMatchResult result("https://play.example.org", "m1", "me");
		CHECK(result.phase() == OnlineMatchResult::Phase::Waiting);
		std::uint64_t now = 1000;
		result.poll(now);
		CHECK_FALSE(result.slow);
		const unsigned before = result.revision;
		CHECK(result.poll(now + OnlineMatchResult::WAITING_EXPECTED_MS + 1));
		CHECK(result.slow);
		CHECK(result.revision == before + 1);

		// The relay ended the match: the verifier's clock starts afresh.
		result.apply(summary("m1", "ended", "pending"));
		CHECK(result.phase() == OnlineMatchResult::Phase::Verifying);
		now += OnlineMatchResult::WAITING_EXPECTED_MS + 2;
		result.poll(now);
		CHECK_FALSE(result.slow);
		result.poll(now + OnlineMatchResult::VERIFYING_EXPECTED_MS + 1);
		CHECK(result.slow);

		result.apply(summary("m1", "ended", "verified"));
		CHECK(result.phase() == OnlineMatchResult::Phase::Done);
		result.poll(now + 2 * OnlineMatchResult::VERIFYING_EXPECTED_MS);
		CHECK_FALSE(result.slow);
		CHECK(result.verification == OnlineMatchResult::Verification::Verified);

		OnlineMatchResult cancelled("https://play.example.org", "m2", "me");
		cancelled.apply(summary("m2", "cancelled", "not_applicable"));
		CHECK(cancelled.phase() == OnlineMatchResult::Phase::Done);
	}

	TEST_CASE("recent matches: the history page, refreshed and led by live summaries")
	{
		const Json history = Json::array({summary("b", "ended", "pending"), summary("c", "ended", "verified"),
										  summary("d", "ended", "verified")});
		const Json live = Json::array({summary("a", "ended", "pending"), summary("b", "ended", "verified")});
		const Json merged = Online::mergeRecentMatches(history, live, 3);
		REQUIRE(merged.size() == 3);
		CHECK(merged[0]["id"] == "a");
		CHECK(merged[1]["id"] == "b");
		CHECK(merged[1]["verification"] == "verified"); // the live summary is newer
		CHECK(merged[2]["id"] == "c");
		CHECK(Online::mergeRecentMatches(Json::array(), Json::array(), 5).empty());
		CHECK(Online::mergeRecentMatches(history, Json(), 5).size() == 3);
	}
}
