// SPDX-License-Identifier: GPL-3.0-or-later
// QuickMatch against a scripted platform: relay probes before queue.join,
// search progress, ranked accept prompts, AI backfill with the start
// countdown, requeues, declines with a cooldown, and the hand-off of the
// assigned match.

#include "Glob2Test.h"
#include "InstanceConfig.h"
#include "OnlineFakes.h"
#include "OnlineStorage.h"
#include "PlatformClient.h"
#include "QuickMatch.h"
#include "RelayProbe.h"

#include <filesystem>

using namespace Online;
using OnlineFakes::Json;

namespace
{
const std::string ORIGIN = "https://play.example.org";
const std::string TICKET = "5d0f2c43-6a3e-4c8e-b8f1-9e2a7c4d3b10";
const std::string PROPOSAL = "0b6a7f6e-8d1c-4a59-9a43-2f0c3d1e5b77";
const std::string MATCH = "7e3c1d2b-9a8f-4e6d-8c5b-4a3f2e1d0c9b";

Json assignmentJson()
{
	const auto path = glob2test::sourceRoot() /
					  "platform/packages/protocol/fixtures/valid/RealtimeEventMatchStart/assignment.json";
	Json value = Json::parse(glob2test::readFile(path));
	value["matchId"] = MATCH;
	return value;
}

QueueInfo ranked()
{
	QueueInfo queue;
	queue.id = "ranked-1v1";
	queue.name = "Ranked 1v1";
	queue.mode = "1v1";
	queue.rated = true;
	queue.aiBackfillSeconds = 90;
	queue.acceptSeconds = 10;
	return queue;
}

struct Fixture
{
	OnlineFakes::World world;
	MemoryStorage storage;
	InstanceConfig config{storage};
	PlatformClient client{config, ClientOptions{"test", "desktop"}, world.environment()};
	std::vector<MatchAssignment> handedOff;
	int attention = 0;
	bool probing = false;
	std::unique_ptr<QuickMatch> quick;

	Fixture(bool withProbe = false) : probing(withProbe)
	{
		QuickMatch::Environment env;
		if (probing)
			env.probe = [this](const std::string &origin)
			{
				RelayProbe::Options options;
				options.attempts = 2;
				options.roundTripsPerRequest = 3;
				return std::make_unique<RelayProbe>(
					origin, [this](HttpFetch::Request request) { return world.http.start(std::move(request)); },
					[this] { return world.now; }, options);
			};
		env.wallClock = [this] { return world.wall; };
		env.attention = [this] { ++attention; };
		env.handoff = [this](const MatchAssignment &assignment) { handedOff.push_back(assignment); };
		quick = std::make_unique<QuickMatch>(client, env);
		client.start(ORIGIN);
		auto exchange = world.http.pending("/api/v1/auth/guest");
		REQUIRE(exchange);
		exchange->reply(200, Json{{"account", OnlineFakes::account("Bradley", "registered")},
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
										   {"account", OnlineFakes::account("Bradley", "registered")}});
		client.update();
		REQUIRE(client.connection() == PlatformClient::Connection::Online);
	}
	void pump()
	{
		client.update();
		quick->update();
	}
	void advance(std::int64_t ms)
	{
		world.now += ms;
		world.wall += ms;
		pump();
	}
	void event(const std::string &name, const Json &data)
	{
		world.socket().event(name, data);
		pump();
	}
	// From search() to Searching with a ticket.
	void joined()
	{
		quick->search(ranked(), true);
		pump();
		auto join = world.socket().find("queue.join");
		REQUIRE(!join.is_null());
		world.socket().respond(join, Json{{"ticketId", TICKET}, {"joinedAt", "2026-09-21T14:13:20Z"}});
		pump();
		REQUIRE(quick->phase() == QuickMatch::Phase::Searching);
	}
	Json proposal(bool requiresAccept, const std::string &ownResponse = "pending",
				  const std::string &otherResponse = "pending")
	{
		Json seats = Json::array();
		seats.push_back({{"slot", 0}, {"side", 0}, {"kind", "human"}, {"displayName", "Bradley"},
						 {"rating", 1528}, {"response", ownResponse}, {"you", true}});
		if (requiresAccept)
			seats.push_back({{"slot", 1}, {"side", 1}, {"kind", "human"}, {"displayName", "Kestrel"},
							 {"rating", 1540}, {"response", otherResponse}});
		else
			seats.push_back({{"slot", 1}, {"side", 1}, {"kind", "ai"}, {"displayName", "Cortex"}, {"ai", "cortex"},
							 {"rating", 1601}, {"response", "not_required"}});
		Json data = {{"proposalId", PROPOSAL}, {"ticketId", TICKET}, {"queueId", "ranked-1v1"},
					 {"requiresAccept", requiresAccept}, {"humans", requiresAccept ? 2 : 1},
					 {"ais", requiresAccept ? 0 : 1}, {"rated", true}, {"backfilled", !requiresAccept},
					 {"region", "eu-west"}, {"map", {{"generatorId", "even-ground"}, {"width", 128}, {"height", 128}}},
					 {"seats", seats}};
		if (requiresAccept)
			data["expiresAt"] = "2026-09-21T14:14:30Z";
		return data;
	}
};
} // namespace

TEST_SUITE("QuickMatch")
{
	TEST_CASE("relay regions are probed before queue.join and their round trips sent")
	{
		Fixture f(true);
		f.quick->search(ranked(), false);
		CHECK(f.quick->phase() == QuickMatch::Phase::Probing);
		auto listing = f.world.http.pending("/api/v1/relays/regions");
		REQUIRE(listing);
		listing->reply(200, Json{{"items",
								  {{{"region", "eu-west"}, {"probeUrl", "https://eu.example.org/relay/r1"}, {"relays", 1}},
								   {{"region", "us-east"}, {"probeUrl", "https://us.example.org/relay/r2"}, {"relays", 1}}}}});
		f.pump();
		// Two attempts per region; the fastest counts, divided by three round trips.
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			auto eu = f.world.http.pending("/relay/r1");
			auto us = f.world.http.pending("/relay/r2");
			REQUIRE(eu);
			REQUIRE(us);
			f.advance(attempt == 0 ? 120 : 90);
			eu->reply(404, Json());
			f.pump();
			f.advance(300);
			if (attempt == 1)
				us->state = HttpFetch::State::Failed;
			else
				us->reply(200, Json());
			f.pump();
		}
		auto join = f.world.socket().find("queue.join");
		REQUIRE(!join.is_null());
		CHECK(join["params"]["queueId"] == "ranked-1v1");
		CHECK(join["params"]["allowAiOpponent"] == false);
		const auto regions = join["params"]["regions"];
		REQUIRE(regions.size() == 2);
		CHECK(regions[0] == Json{{"region", "eu-west"}, {"rttMs", 40}}); // fastest 120 ms / 3
		CHECK(regions[1] == Json{{"region", "us-east"}, {"rttMs", 140}});
		CHECK(f.quick->phase() == QuickMatch::Phase::Joining);
	}

	TEST_CASE("an unreachable region list still joins, without round trips")
	{
		Fixture f(true);
		f.quick->search(ranked());
		f.world.http.pending("/api/v1/relays/regions")->reply(503, Json());
		f.pump();
		auto join = f.world.socket().find("queue.join");
		REQUIRE(!join.is_null());
		CHECK(join["params"]["regions"] == Json::array());
	}

	TEST_CASE("searching follows queue.status and toggles AI backfill with queue.update")
	{
		Fixture f;
		f.joined();
		f.advance(42000);
		CHECK(f.quick->waitedSeconds() == 42);
		f.event("queue.status", {{"ticketId", TICKET}, {"queueId", "ranked-1v1"}, {"waitedSeconds", 45},
								 {"ratingRange", {{"min", 1430}, {"max", 1630}}}, {"region", "eu-west"}, {"rttMs", 28},
								 {"aiBackfillAt", "2026-09-21T14:14:50Z"}, {"backfillAi", {{"ai", "cortex"}, {"rating", 1601}}}});
		REQUIRE(f.quick->status());
		CHECK(f.quick->status()->ratingMin == 1430);
		CHECK(f.quick->waitedSeconds() == 45);
		REQUIRE(f.quick->backfillInMs());
		// Another ticket's status is ignored.
		f.event("queue.status", {{"ticketId", "other"}, {"queueId", "ranked-1v1"}, {"waitedSeconds", 1}});
		CHECK(f.quick->status()->waitedSeconds == 45);

		f.quick->setAllowAiOpponent(false);
		auto update = f.world.socket().find("queue.update");
		REQUIRE(!update.is_null());
		CHECK(update["params"] == Json{{"ticketId", TICKET}, {"allowAiOpponent", false}});
		CHECK_FALSE(f.quick->backfillInMs());

		f.quick->cancel();
		auto leave = f.world.socket().find("queue.leave");
		REQUIRE(!leave.is_null());
		CHECK(leave["params"]["ticketId"] == TICKET);
		CHECK(f.quick->phase() == QuickMatch::Phase::Idle);
	}

	TEST_CASE("a ranked prompt is answered, shows who accepted and hands off at once")
	{
		Fixture f;
		f.joined();
		f.event("queue.proposal", f.proposal(true));
		CHECK(f.quick->phase() == QuickMatch::Phase::Proposed);
		CHECK(f.attention == 1);
		REQUIRE(f.quick->acceptInMs());
		CHECK(*f.quick->acceptInMs() == 70000); // expiresAt is 70 s after the fixture's wall clock start
		f.quick->respond(true);
		auto respond = f.world.socket().find("queue.respond");
		REQUIRE(!respond.is_null());
		CHECK(respond["params"] == Json{{"proposalId", PROPOSAL}, {"accept", true}});
		f.world.socket().respond(respond, Json::object());
		f.pump();
		CHECK(f.quick->answer() == true);
		// The resent prompt: the opponent accepted too.
		f.event("queue.proposal", f.proposal(true, "accepted", "accepted"));
		CHECK(f.attention == 1);
		CHECK(f.quick->proposal()->seats[1].response == "accepted");
		f.event("queue.matchFound", {{"ticketId", TICKET}, {"matchId", MATCH}});
		CHECK(f.quick->phase() == QuickMatch::Phase::Starting);
		CHECK(f.handedOff.empty());
		f.event("match.start", assignmentJson());
		REQUIRE(f.handedOff.size() == 1);
		CHECK(f.handedOff[0].matchId == MATCH);
		CHECK(f.quick->phase() == QuickMatch::Phase::Idle);
	}

	TEST_CASE("an AI-backfilled match starts after the countdown without a prompt")
	{
		Fixture f;
		f.joined();
		f.event("queue.proposal", f.proposal(false));
		CHECK(f.quick->phase() == QuickMatch::Phase::Starting);
		f.event("queue.matchFound", {{"ticketId", TICKET}, {"matchId", MATCH}});
		// A match.start for another match is not ours.
		auto other = assignmentJson();
		other["matchId"] = "11111111-2222-4333-8444-555555555555";
		f.event("match.start", other);
		CHECK(f.quick->phase() == QuickMatch::Phase::Starting);
		f.event("match.start", assignmentJson());
		CHECK(f.quick->phase() == QuickMatch::Phase::Matched);
		f.advance(2900);
		CHECK(f.handedOff.empty());
		CHECK(*f.quick->startInMs() == 100);
		f.advance(100);
		REQUIRE(f.handedOff.size() == 1);
	}

	TEST_CASE("cancelling during the countdown stops the hand-off")
	{
		Fixture f;
		f.joined();
		f.event("queue.proposal", f.proposal(false));
		f.event("queue.matchFound", {{"ticketId", TICKET}, {"matchId", MATCH}});
		f.event("match.start", assignmentJson());
		f.quick->cancel();
		f.advance(5000);
		CHECK(f.handedOff.empty());
		CHECK(f.quick->phase() == QuickMatch::Phase::Idle);
	}

	TEST_CASE("an opponent who does not accept puts the player back in the search")
	{
		Fixture f;
		f.joined();
		f.event("queue.proposal", f.proposal(true));
		f.quick->respond(true);
		f.event("queue.proposalEnded", {{"proposalId", PROPOSAL}, {"ticketId", TICKET}, {"outcome", "requeued"},
										{"reason", "other_declined"}});
		CHECK(f.quick->phase() == QuickMatch::Phase::Searching);
		CHECK(f.quick->notice() == QuickMatch::Notice::OpponentDeclined);
		CHECK(f.quick->noticeName() == "Kestrel");
		CHECK_FALSE(f.quick->proposal());
		f.quick->dismissNotice();
		CHECK(f.quick->notice() == QuickMatch::Notice::None);
	}

	TEST_CASE("declining or not answering ends the search with the cooldown")
	{
		Fixture f;
		f.joined();
		f.event("queue.proposal", f.proposal(true));
		f.event("queue.proposalEnded", {{"proposalId", PROPOSAL}, {"ticketId", TICKET}, {"outcome", "removed"},
										{"reason", "timeout"}, {"cooldownUntil", "2026-09-21T14:15:20Z"}});
		CHECK(f.quick->phase() == QuickMatch::Phase::Idle);
		CHECK(f.quick->notice() == QuickMatch::Notice::TimedOut);
		CHECK(f.quick->cooldownUntil() == parseTimestamp("2026-09-21T14:15:20Z"));

		// Joining during the cooldown fails with its end.
		f.quick->search(ranked());
		f.pump();
		auto join = f.world.socket().find("queue.join");
		f.world.socket().incoming.push_back(Json{{"type", "response"},
												 {"id", join["id"]},
												 {"ok", false},
												 {"error",
												  {{"code", "rate_limited"},
												   {"message", "wait"},
												   {"details", {{"until", "2026-09-21T14:15:20Z"}}}}}}
												.dump());
		f.pump();
		CHECK(f.quick->phase() == QuickMatch::Phase::Failed);
		CHECK(f.quick->error().code == "rate_limited");
		CHECK(f.quick->cooldownUntil() == parseTimestamp("2026-09-21T14:15:20Z"));
	}

	TEST_CASE("cancelling before the queue answers leaves the ticket it gets")
	{
		Fixture f;
		f.quick->search(ranked());
		f.pump();
		auto join = f.world.socket().find("queue.join");
		REQUIRE(!join.is_null());
		f.quick->cancel();
		CHECK(f.quick->phase() == QuickMatch::Phase::Idle);
		f.world.socket().respond(join, Json{{"ticketId", TICKET}, {"joinedAt", "2026-09-21T14:13:20Z"}});
		f.pump();
		auto leave = f.world.socket().find("queue.leave");
		REQUIRE(!leave.is_null());
		CHECK(leave["params"]["ticketId"] == TICKET);
		CHECK(f.quick->phase() == QuickMatch::Phase::Idle);
	}
}
