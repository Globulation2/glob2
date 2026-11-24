// SPDX-License-Identifier: GPL-3.0-or-later
// The typed views of queue events, match history and the map catalog read
// every protocol fixture of their schema (services/online/packages/protocol/fixtures),
// and the profile figures are computed from match history.

#include "Glob2Test.h"

#include <algorithm>
#include "OnlineResources.h"
#include "TurnProtocol.h"

#include <filesystem>
#include <set>

using namespace Online;
namespace fs = std::filesystem;

namespace
{
fs::path fixtureRoot()
{
	return glob2test::sourceRoot() / "services/online/packages/protocol/fixtures";
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
		for (auto &[file, value] : fixtures("PlayerProfile"))
		{
			INFO(file);
			CHECK(PlayerProfile::fromJson(value));
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

	TEST_CASE("assisted queues are for clients with MCP enabled and the rest for everyone else")
	{
		const Json instance = {{"queues",
								{{{"id", "ranked-1v1"}, {"name", "Ranked 1v1"}, {"mode", "1v1"}, {"rated", true}},
								 {{"id", "casual-1v1"}, {"name", "Casual 1v1"}, {"mode", "1v1"}, {"rated", false}, {"assisted", false}},
								 {{"id", "mcp-ranked-1v1"}, {"name", "MCP Ranked 1v1"}, {"mode", "1v1"}, {"rated", true}, {"assisted", true}}}}};
		const auto queues = queuesFromInstance(instance);
		REQUIRE(queues.size() == 3);
		CHECK(!queues[0].assisted); // servers before assisted queues send no flag
		CHECK(!queues[1].assisted);
		CHECK(queues[2].assisted);
		const auto &items = instance["queues"];
		CHECK(queueFits(items[0], false));
		CHECK(!queueFits(items[0], true));
		CHECK(queueFits(items[1], false));
		CHECK(!queueFits(items[2], false));
		CHECK(queueFits(items[2], true));
		CHECK(!queueFits(Json("ranked-1v1"), false));
	}

	TEST_CASE("a client's queue list keeps only its kind, in the instance's order")
	{
		const Json all = Json::array({{{"id", "ranked-1v1"}, {"assisted", false}},
									  {{"id", "mcp-ranked-1v1"}, {"assisted", true}},
									  {{"id", "casual-1v1"}},
									  {{"id", "mcp-casual-1v1"}, {"assisted", true}}});
		const Json plain = queuesFor(all, false);
		REQUIRE(plain.size() == 2);
		CHECK(plain[0]["id"] == "ranked-1v1");
		CHECK(plain[1]["id"] == "casual-1v1");
		const Json mcp = queuesFor(all, true);
		REQUIRE(mcp.size() == 2);
		CHECK(mcp[0]["id"] == "mcp-ranked-1v1");
		CHECK(mcp[1]["id"] == "mcp-casual-1v1");
		// Every queue lands in exactly one of the two lists.
		CHECK(plain.size() + mcp.size() == all.size());
	}

	TEST_CASE("a server without assisted queues leaves an assisted client none")
	{
		const Json old = Json::array({{{"id", "ranked-1v1"}, {"rated", true}}, {{"id", "casual-1v1"}}});
		CHECK(queuesFor(old, true).empty());
		CHECK(queuesFor(old, false).size() == 2);
		// Every protocol fixture splits into the two lists without losing a queue, and
		// the parsed flag agrees with the JSON one.
		int withAssisted = 0;
		for (auto &[file, value] : fixtures("InstanceInfo"))
		{
			INFO(file);
			const auto parsed = queuesFromInstance(value);
			const Json mcp = queuesFor(value["queues"], true);
			CHECK(queuesFor(value["queues"], false).size() + mcp.size() == value["queues"].size());
			CHECK(std::count_if(parsed.begin(), parsed.end(), [](const QueueInfo &q) { return q.assisted; }) ==
				  std::ptrdiff_t(mcp.size()));
			if (!mcp.empty())
			{
				++withAssisted;
				CHECK(mcp[0]["id"] == "mcp-ranked-1v1");
			}
		}
		CHECK(withAssisted == 1);
	}

	TEST_CASE("malformed queue lists give no queues rather than throwing")
	{
		CHECK(queuesFor(Json(), false).empty());
		CHECK(queuesFor(Json::object(), true).empty());
		CHECK(queuesFor(Json("queues"), false).empty());
		CHECK(queuesFor(Json::array(), true).empty());
		const Json mixed = Json::array({Json(7), Json("casual-1v1"), Json::array(), {{"id", "casual-1v1"}}});
		const Json kept = queuesFor(mixed, false);
		REQUIRE(kept.size() == 1);
		CHECK(kept[0]["id"] == "casual-1v1");
		// A flag that is not a boolean does not make a queue assisted.
		CHECK(!queueFits(Json{{"id", "x"}, {"assisted", "yes"}}, true));
		CHECK(queueFits(Json{{"id", "x"}, {"assisted", "yes"}}, false));
		const auto parsed = QueueInfo::fromJson(Json{{"id", "x"}, {"name", "X"}, {"mode", "1v1"}, {"assisted", 1}});
		REQUIRE(parsed);
		CHECK(!parsed->assisted);
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
		CHECK(detail->artifact("replay")->url.find("/artifacts/replay") != std::string::npos);
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
		CHECK(map->map.latestVersion->previewUrl.find("preview.webp") != std::string::npos);
		CHECK(map->map.plays == 12);
		CHECK(!map->versions.empty());

		auto pending = MapVersionInfo::fromJson(fixture("MapVersionInfo", "pending"));
		REQUIRE(pending);
		CHECK(pending->validation == "pending");

		CHECK_FALSE(MapInfo::fromJson(Json::object()));
		CHECK(parseMapList(Json{{"items", Json::array({map->map.title})}}).items.empty());
	}

	TEST_CASE("player profiles give ratings with rank, the trend and the aggregates")
	{
		auto registered = PlayerProfile::fromJson(fixture("PlayerProfile", "registered"));
		REQUIRE(registered);
		CHECK(registered->full);
		REQUIRE(registered->ratings.size() == 1);
		CHECK(registered->ratings[0].rank == 3);
		auto summary = summarizeProfile(*registered, {});
		REQUIRE(summary.ladders.size() == 1);
		CHECK(summary.ladders[0].rating == doctest::Approx(1532.4));
		CHECK(summary.ladders[0].rank == 3);
		CHECK(summary.ladders[0].trend == std::vector<double>{1532.4});
		CHECK(summary.recent == 11);
		CHECK(summary.wins == 7);
		CHECK(summary.losses == 4);
		CHECK(summary.medianMinutes == 20); // 36000 ticks at 30 ticks/s
		CHECK(summary.bestMap == "evenGround");
		CHECK(summary.bestMapWins == 4);

		auto guest = PlayerProfile::fromJson(fixture("PlayerProfile", "guest"));
		REQUIRE(guest);
		CHECK_FALSE(guest->full);
		CHECK(summarizeProfile(*guest, {}).ladders.empty());
	}

	TEST_CASE("typical game length counts the relay's ticks at its real rate")
	{
		// Two room games of 5:26 and 5:50, measured in the relay's ticks.
		const auto ticksFor = [](int seconds) {
			return std::int64_t(seconds) * Turn::DEFAULT_TICK_RATE_MILLIHZ / 1000;
		};
		std::vector<MatchSummary> matches{match("b", "me", "won", 0, 0, "Even Ground", 0, false),
										  match("a", "me", "lost", 0, 0, "Even Ground", 0, false)};
		matches[0].durationTicks = ticksFor(5 * 60 + 50);
		matches[1].durationTicks = ticksFor(5 * 60 + 26);
		CHECK(summarizeProfile("me", matches).medianMinutes == 6);

		// The server's median (a profile aggregate) reads the same way: 20 minutes.
		PlayerProfile profile;
		profile.full = true; // a registered player: the server sends aggregates
		profile.medianTicks = double(ticksFor(20 * 60));
		CHECK(summarizeProfile(profile, {}).medianMinutes == 20);
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

		// A draw (MatchOutcome "draw") is a game, neither a win nor a loss.
		std::vector<MatchSummary> drawn{match("d1", "me", "draw", 1500, 1500, "Even Ground", 30)};
		summary = summarizeProfile("me", drawn);
		CHECK(summary.recent == 1);
		CHECK(summary.draws == 1);
		CHECK(summary.wins + summary.losses == 0);
		CHECK(summary.bestMap.empty());
	}
}

#include "AiCatalog.h"
TEST_CASE("AI catalogue rejects malformed metadata and requires exact compatible evidence" * doctest::test_suite("OnlineResources"))
{
 for(const auto &[name,value]:fixtures("AiDetail")){INFO(name);CHECK(validAiDetail(value));}
 Json checks=Json::array();
 for(const auto &id:{"file","syntax","startup","state","gameplay","determinism","continuation"}) checks.push_back({{"id",id},{"status","passed"}});
 Json report={{"sourceHash",std::string(64,'a')},{"simVersion","engine"},{"suite",1},{"valid",true},{"checks",checks}};
 Json v={{"id","release"},{"hash",std::string(64,'a')},{"label","1.0"},{"notes",""},{"downloads",0},{"validations",Json::array({report})}};
 REQUIRE(validAiVersion(v)); CHECK(compatibleAiVersion(v,"engine")); CHECK_FALSE(compatibleAiVersion(v,"other"));
 auto bad=v;bad["validations"][0]["sourceHash"]=std::string(64,'b');CHECK_FALSE(compatibleAiVersion(bad,"engine"));
 bad=v;bad["validations"][0]["checks"][6]=checks[0];CHECK_FALSE(compatibleAiVersion(bad,"engine"));
 bad=v;bad["downloads"]="many";CHECK_FALSE(validAiVersion(bad));
 bad=v;bad["validations"]=false;CHECK_FALSE(compatibleAiVersion(bad,"engine"));
 Json ai={{"owner",{{"displayName","Author"}}},{"tags",Json::array({"Balanced"})},{"id","controller"},{"name","Patient"},{"description",""},{"likes",2},{"downloads",0},{"liked",false},{"favourited",true},{"latestVersion",v}};
 CHECK(validAiSummary(ai));
 auto malformed = ai; malformed["owner"]["displayName"] = false; CHECK_FALSE(validAiSummary(malformed));
 malformed = ai; malformed["tags"] = {7}; CHECK_FALSE(validAiSummary(malformed));
 ai["name"]=Json::array();CHECK_FALSE(validAiSummary(ai));
}
