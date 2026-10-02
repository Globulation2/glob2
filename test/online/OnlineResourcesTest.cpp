// SPDX-License-Identifier: GPL-3.0-or-later
// The typed views of queue events, match history and the map catalog read
// every protocol fixture of their schema (platform/packages/protocol/fixtures),
// and the profile figures are computed from match history.

#include "Glob2Test.h"
#include "OnlineResources.h"

#include <filesystem>
#include <set>

using namespace Online;
namespace fs = std::filesystem;

namespace
{
fs::path fixtureRoot()
{
	return glob2test::sourceRoot() / "platform/packages/protocol/fixtures";
}

std::vector<std::pair<std::string, Json>> fixtures(const std::string &schema, bool valid = true)
{
	std::vector<std::pair<std::string, Json>> out;
	const Json manifest = Json::parse(glob2test::readFile(fixtureRoot() / "manifest.json"));
	for (const auto &entry : manifest.at("fixtures"))
		if (entry.at("schema") == schema && entry.at("valid").get<bool>() == valid)
		{
			const auto file = entry.at("file").get<std::string>();
			out.emplace_back(file, Json::parse(glob2test::readFile(fixtureRoot() / file)));
		}
	return out;
}

Json fixture(const std::string &schema, const std::string &name)
{
	for (auto &[file, value] : fixtures(schema))
		if (file.find("/" + name + ".json") != std::string::npos)
			return value;
	FAIL("missing fixture " << schema << "/" << name);
	return Json();
}

MatchSummary match(const std::string &id, const std::string &me, const std::string &outcome, double before,
				   double after, const std::string &map, int minutes, bool rated = true)
{
	MatchSummary m;
	m.id = id;
	m.origin = "queue";
	m.queueId = "ranked-1v1";
	m.rated = rated;
	m.status = "ended";
	m.mapTitle = map;
	m.durationTicks = minutes * 60 * MATCH_TICKS_PER_SECOND;
	MatchParticipant self;
	self.accountId = me;
	self.displayName = "Me";
	self.outcome = outcome;
	if (rated)
		self.rating = RatingChange{"ranked-1v1", before, after, after < 1510};
	MatchParticipant other;
	other.team = 1;
	other.displayName = "Them";
	other.accountId = "other";
	other.outcome = outcome == "won" ? "lost" : "won";
	m.participants = {self, other};
	return m;
}
} // namespace

TEST_SUITE("OnlineResources")
{
	TEST_CASE("every valid queue, match and catalog fixture parses")
	{
		REQUIRE(fs::exists(fixtureRoot() / "manifest.json"));
		int count = 0;
		for (auto &[file, value] : fixtures("RealtimeEventQueueStatus"))
		{
			INFO(file);
			CHECK(QueueStatus::fromJson(value));
			++count;
		}
		for (auto &[file, value] : fixtures("RealtimeEventQueueProposal"))
		{
			INFO(file);
			CHECK(QueueProposal::fromJson(value));
			++count;
		}
		for (auto &[file, value] : fixtures("RealtimeEventQueueProposalEnded"))
		{
			INFO(file);
			CHECK(ProposalEnded::fromJson(value));
			++count;
		}
		for (auto &[file, value] : fixtures("RealtimeEventMatchStart"))
		{
			INFO(file);
			CHECK(MatchAssignment::fromJson(value));
			++count;
		}
		for (auto &[file, value] : fixtures("MatchDetail"))
		{
			INFO(file);
			CHECK(MatchDetail::fromJson(value));
			++count;
		}
		for (auto &[file, value] : fixtures("MapDetail"))
		{
			INFO(file);
			CHECK(MapDetail::fromJson(value));
			++count;
		}
		for (auto &[file, value] : fixtures("MapVersionInfo"))
		{
			INFO(file);
			CHECK(MapVersionInfo::fromJson(value));
			++count;
		}
		for (auto &[file, value] : fixtures("InstanceInfo"))
		{
			INFO(file);
			CHECK(!queuesFromInstance(value).empty());
			++count;
		}
		for (auto &[file, value] : fixtures("RelayRegionList"))
		{
			INFO(file);
			CHECK(!parseRelayRegions(value).empty());
			++count;
		}
		CHECK(count >= 12);
	}

	TEST_CASE("queue status and proposals carry what the search panel and prompt show")
	{
		auto status = QueueStatus::fromJson(fixture("RealtimeEventQueueStatus", "searching"));
		REQUIRE(status);
		CHECK(status->waitedSeconds == 42);
		CHECK(status->ratingMin == 1430);
		CHECK(status->ratingMax == 1630);
		CHECK(status->region == "eu-west");
		CHECK(status->rttMs == 28);
		CHECK(status->backfillAi == "cortex");
		CHECK(status->backfillAiRating == 1601);
		CHECK(status->typicalWaitSeconds == 40);
		CHECK(status->aiBackfillAt == parseTimestamp("2026-10-01T12:01:00Z"));

		auto ranked = QueueProposal::fromJson(fixture("RealtimeEventQueueProposal", "ranked-accept-with-seats"));
		REQUIRE(ranked);
		CHECK(ranked->requiresAccept);
		CHECK(ranked->generatorId == "even-ground");
		CHECK(ranked->width == 128);
		REQUIRE(ranked->seats.size() == 2);
		REQUIRE(ranked->own());
		CHECK(ranked->own()->displayName == "Bradley");
		CHECK(ranked->own()->response == "accepted");
		CHECK(ranked->seats[1].response == "pending");

		auto ai = QueueProposal::fromJson(fixture("RealtimeEventQueueProposal", "ai-backfill"));
		REQUIRE(ai);
		CHECK_FALSE(ai->requiresAccept);
		CHECK(ai->backfilled);
		CHECK_FALSE(ai->seats[1].human);
		CHECK(ai->seats[1].ai == "cortex");

		// The minimal form of the event (before seats existed) still parses.
		auto minimal = QueueProposal::fromJson(fixture("RealtimeEventQueueProposal", "ranked-accept"));
		REQUIRE(minimal);
		CHECK(minimal->seats.empty());
		CHECK(minimal->own() == nullptr);

		auto assignment = MatchAssignment::fromJson(fixture("RealtimeEventMatchStart", "assignment"));
		REQUIRE(assignment);
		CHECK(assignment->mapHash().size() == 64);
		CHECK(!assignment->relayUrl.empty());

		auto regions = parseRelayRegions(fixture("RelayRegionList", "two-regions"));
		REQUIRE(regions.size() == 2);
		CHECK(regions[0].probeUrl == "https://play.example.org/relay/relay-1");
		CHECK(regionsJson({{"eu-west", 28}}) == Json::parse(R"([{"region":"eu-west","rttMs":28}])"));
	}

	TEST_CASE("match details and catalog maps")
	{
		auto detail = MatchDetail::fromJson(fixture("MatchDetail", "verified"));
		REQUIRE(detail);
		CHECK(detail->match.rated);
		CHECK(detail->match.durationTicks == 45000);
		REQUIRE(detail->artifact("replay"));
		CHECK(detail->artifact("replay")->url.find("/api/v1/blobs/") != std::string::npos);
		const auto *alice = detail->match.participant("0b8f6f2e-3c4d-4e5f-8a9b-0c1d2e3f4a5b");
		REQUIRE(alice);
		REQUIRE(alice->rating);
		CHECK(alice->rating->after == doctest::Approx(1532.4));
		CHECK(alice->rating->provisional);

		auto map = MapDetail::fromJson(fixture("MapDetail", "public-map"));
		REQUIRE(map);
		CHECK(map->map.title == "Two Rivers");
		CHECK(map->map.ownerName == "Alice");
		REQUIRE(map->map.latestVersion);
		CHECK(map->map.latestVersion->teamCount == 2);
		CHECK(map->map.latestVersion->previewUrl.find("preview.png") != std::string::npos);
		CHECK(map->map.plays == 12);
		CHECK(!map->versions.empty());

		auto pending = MapVersionInfo::fromJson(fixture("MapVersionInfo", "pending"));
		REQUIRE(pending);
		CHECK(pending->validation == "pending");

		CHECK_FALSE(MapInfo::fromJson(Json::object()));
		CHECK(parseMapList(Json{{"items", Json::array({map->map.title})}}).items.empty());
	}

	TEST_CASE("the profile summary follows the ladders and recent results")
	{
		// Newest first, as the history list returns them.
		std::vector<MatchSummary> matches{
			match("m4", "me", "won", 1520, 1535, "Even Ground", 21),
			match("m3", "me", "lost", 1531, 1520, "Symmetric Arena", 34),
			match("m2", "me", "won", 1500, 1531, "Even Ground", 29),
			match("m1", "me", "unresolved", 0, 0, "Marchland", 10, false),
		};
		matches[3].origin = "room";
		auto summary = summarizeProfile("me", matches);
		REQUIRE(summary.ladders.size() == 1);
		const auto &ladder = summary.ladders[0];
		CHECK(ladder.ladder == "ranked-1v1");
		CHECK(ladder.rating == 1535);
		CHECK(ladder.games == 3);
		CHECK(ladder.lastChange == doctest::Approx(15));
		CHECK(ladder.trend == std::vector<double>{1531, 1520, 1535});
		CHECK(summary.recent == 4);
		CHECK(summary.wins == 2);
		CHECK(summary.losses == 1);
		CHECK(summary.draws == 1);
		CHECK(summary.medianMinutes == 25); // 10, 21, 29, 34
		CHECK(summary.bestMap == "Even Ground");
		CHECK(summary.bestMapWins == 2);

		// Aborted matches and matches the account did not play are left out.
		matches[0].endReason = "aborted";
		summary = summarizeProfile("me", matches, 2);
		CHECK(summary.recent == 2);
		CHECK(summary.wins == 1);
		CHECK(summarizeProfile("nobody", matches).recent == 0);
	}
}
