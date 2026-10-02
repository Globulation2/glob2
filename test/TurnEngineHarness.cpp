// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// Full-engine harness for the turn protocol: 2-4 real Engine instances, each a client
// started with Engine::initTurnMatch, play one MatchSetup against a TurnSequencer over
// the simulated network of TurnHarnessTest (latency, jitter, loss, outages). The AI
// seats run on every client; each human seat is driven by a small bot that queues
// orders through the GUI's order queue, as a player's clicks do.
//
// Every client records the checksum the engine computes before each tick. The cases
// check that all clients agree at every tick (after a reload, in their final run),
// that --verify-match reproduces the same checksums and result outcomes from the
// relay's record, and that a client which executes a tampered order is named by the
// verifier. Timings of rejoin fast-forwards are written under artifacts/tests/.
//
// The engines share one process and one GlobalContainer. Each keeps its own
// synchronized RNG stream, swapped in while it runs (as MenuColony does), and none
// writes replays; everything else an Engine touches is its own.

#include "EngineFixtures.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <set>
#include <sstream>

#include "Engine.h"
#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Headless.h"
#include "MatchRecord.h"
#include "MatchSetup.h"
#include "MersenneTwister.h"
#include "Order.h"
#include "Player.h"
#include "ReplayReader.h"
#include "Sha256.h"
#include "SimVersion.h"
#include "TurnLatencyTrace.h"
#include "TurnLockstep.h"
#include "TurnTestSupport.h"
#include "Utilities.h"
#include "VerifyMatch.h"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace turntest;

namespace
{
using Clock = std::chrono::steady_clock;

/// Swaps an engine's own synchronized RNG stream in for the scope.
struct RngScope
{
	MersenneTwister& own;
	explicit RngScope(MersenneTwister& stream) : own(stream) { std::swap(syncRandEngine(), own); }
	~RngScope() { std::swap(syncRandEngine(), own); }
};

std::string mapPath(const std::string& name)
{
	return (glob2test::sourceRoot() / "maps" / (name + ".map.gz")).string();
}

/// A setup on `map`: humans on the first teams, then the AIs, every team its own
/// alliance. Unused map teams stay listed (the contract lists every map team).
Online::MatchSetup makeSetup(const std::string& map, int humans, const std::vector<std::string>& ais, std::uint32_t seed)
{
	const MapHeader header = Engine::loadMapHeader(map);
	Online::MatchSetup setup;
	setup.simVersion = Online::currentSimVersion();
	setup.seed = seed;
	setup.map.kind = Online::MapSource::Kind::Catalog;
	setup.map.hash = Online::mapContentHash(map);
	REQUIRE(setup.map.hash.size() == 64);
	const int teams = header.getNumberOfTeams();
	REQUIRE(humans + static_cast<int>(ais.size()) <= teams);
	for (int t = 0; t < teams; ++t)
		setup.teams.push_back({t, t});
	for (int h = 0; h < humans; ++h)
	{
		Online::SetupSeat seat;
		seat.seat = h;
		seat.team = h;
		seat.name = "Player " + std::to_string(h + 1);
		setup.seats.push_back(seat);
	}
	for (std::size_t a = 0; a < ais.size(); ++a)
	{
		Online::SetupSeat seat;
		seat.seat = humans + static_cast<int>(a);
		seat.team = seat.seat;
		seat.human = false;
		seat.ai = ais[a];
		seat.name = "AI " + std::to_string(a + 1);
		setup.seats.push_back(seat);
	}
	setup.validateSemantics();
	return setup;
}

class EngineClient
{
public:
	EngineClient(SimNetwork& net, int index, const Online::MatchSetup& setup, const std::string& map)
		: net(net), index(index), seat(index), setup(setup), map(map), bot(7000 + index)
	{
		start();
	}

	~EngineClient()
	{
		if (engine)
		{
			RngScope scope(rng);
			engine.reset();
		}
	}

	/// A fresh process: a new Engine and transport with the same ticket.
	void start()
	{
		RngScope scope(rng);
		if (transport)
			transport->close();
		engine.reset();
		lives.emplace_back();
		transport = std::make_shared<SimTransport>(net, index);
		engine = std::make_unique<Engine>();
		Engine::TurnMatchStart start;
		start.setup = setup;
		start.mapFile = map;
		start.localSeat = seat;
		start.transport = transport;
		start.config.ticket = "seat:" + std::to_string(seat);
		REQUIRE(engine->initTurnMatch(start) == Engine::EE_NO_ERROR);
		auto* lockstep = engine->turnLockstep();
		REQUIRE(lockstep);
		lockstep->onChecksum = [this](std::uint32_t tick, Uint32 checksum) {
			if (tick == 0 && !lives.back().empty())
				lives.emplace_back(); // reloaded in place: a new run from tick 0
			lives.back()[tick] = checksum;
		};
		lockstep->turn().onSubmitted = [this](std::uint32_t sequence) {
			trace.submitted(sequence, net.now, engine->turnLockstep()->turn().executedTick());
		};
		lockstep->turn().onHorizon = [this](std::uint32_t horizon) { trace.received(horizon, net.now); };
		lockstep->orderFilter = [this](std::uint32_t tick, int player, std::shared_ptr<Order> order) {
			if (player == seat)
				trace.executed(tick, net.now);
			if (tick == tamperAtTick && player == seat && !tampered)
			{
				// A cheating client: executes an order the relay never sequenced.
				tampered = true;
				Team* team = engine->gui.game.teams[engine->gui.localTeamNo];
				return std::shared_ptr<Order>(std::make_shared<OrderCreate>(
					team->teamNumber, team->startPosX + 3, team->startPosY + 3,
					globalContainer->buildingsTypes.getTypeNum("explorationflag", 0, false), 4, 4));
			}
			return order;
		};
		engine->beginSession(net.now / MS);
		wakeAt = 0;
	}

	Turn::TurnSession& session()
	{
		REQUIRE_MESSAGE(engine->turnLockstep(), "client " << index << " has no running session");
		return engine->turnLockstep()->turn();
	}

	void frame()
	{
		if (stopped || !engine)
			return;
		RngScope scope(rng);
		const Uint64 now = net.now / MS;
		if (now < wakeAt)
			engine->pollTurnSession(now); // as GameSessionScreen between steps
		for (int budget = 4000; budget > 0 && now >= wakeAt; --budget)
		{
			const bool catching = session().catchingUp();
			const std::uint32_t before = session().executedTick();
			const auto started = Clock::now();
			const bool running = engine->stepSession(now);
			const std::uint32_t after = session().executedTick();
			if (catching)
			{
				catchUpNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
				catchUpTicks += after > before ? after - before : 0;
			}
			if (after < before)
				++reloads;
			engine->trackTeamEliminations();
			// A player acts in real time, not once per fast-forwarded tick.
			if (after > before && !catching && orderRate > 0 && std::uniform_real_distribution<double>(0, 1)(bot) < orderRate)
				queueBotOrder();
			if (!running)
			{
				// What the host does when the loop ends: finish the session, which
				// tells the relay this client is leaving.
				stopped = true;
				finalTick = after;
				engine->finishSessionForHost();
				break;
			}
			wakeAt = now + engine->sessionDelay(now);
		}
	}

	/// What a player might click: building sites and flags near the colony, worker
	/// counts on existing buildings, a chat line. Many are refused by the engine (no
	/// room, dead team), which is fine: they are still sequenced and executed.
	void queueBotOrder()
	{
		Game& game = engine->gui.game;
		Team* team = game.teams[engine->gui.localTeamNo];
		if (!team || !team->isAlive)
			return;
		std::uniform_int_distribution<int> offset(-10, 10);
		const int x = team->startPosX + offset(bot), y = team->startPosY + offset(bot);
		std::shared_ptr<Order> order;
		switch (bot() % 5)
		{
		case 0:
		case 1:
		{
			static const char* sites[] = {"inn", "swarm", "hospital", "racetrack", "swimmingpool", "school", "defencetower"};
			const int type = globalContainer->buildingsTypes.getTypeNum(sites[bot() % 7], 0, true);
			order = std::make_shared<OrderCreate>(team->teamNumber, x, y, type, 1 + bot() % 5, 1 + bot() % 5);
			break;
		}
		case 2:
		{
			static const char* flags[] = {"explorationflag", "warflag", "clearingflag"};
			const int type = globalContainer->buildingsTypes.getTypeNum(flags[bot() % 3], 0, false);
			order = std::make_shared<OrderCreate>(team->teamNumber, x + offset(bot), y + offset(bot), type, 1 + bot() % 6,
			                                      1 + bot() % 6);
			break;
		}
		case 3:
		{
			std::vector<Uint16> gids;
			for (int i = 0; i < Building::MAX_COUNT; ++i)
				if (const Building* b = team->myBuildings[i])
					if (!b->type->isVirtual && b->type->maxUnitWorking > 0)
						gids.push_back(b->gid);
			if (!gids.empty())
				order = std::make_shared<OrderModifyBuilding>(gids[bot() % gids.size()], 1 + bot() % 8);
			break;
		}
		default:
			order = std::make_shared<MessageOrder>(~0u, MessageOrder::NORMAL_MESSAGE_TYPE, "gl hf");
			break;
		}
		if (order)
		{
			engine->gui.orderQueue.push_back(order);
			trace.queued(net.now);
			++ordersQueued;
		}
	}

	SimNetwork& net;
	int index;
	int seat;
	const Online::MatchSetup& setup;
	std::string map;
	std::shared_ptr<SimTransport> transport;
	std::unique_ptr<Engine> engine;
	MersenneTwister rng;
	std::mt19937 bot;
	Uint64 wakeAt = 0;
	bool stopped = false;
	std::uint32_t finalTick = 0; ///< executed ticks when the engine stopped
	double orderRate = 0.04;
	std::uint32_t tamperAtTick = UINT32_MAX;
	bool tampered = false;
	int reloads = 0;
	int ordersQueued = 0;
	std::int64_t catchUpNs = 0;
	std::uint32_t catchUpTicks = 0;
	/// Per run of the engine from tick 0 (a restart or an in-place reload starts a
	/// new one): tick -> checksum before that tick.
	std::vector<std::map<std::uint32_t, Uint32>> lives;
	turntest::LatencyTrace trace;
};

struct EngineMatch
{
	SimNetwork net;
	Online::MatchSetup setup;
	std::string map;
	std::vector<std::unique_ptr<EngineClient>> clients;

	EngineMatch(const std::string& mapName, const std::vector<LinkProfile>& links, const std::vector<std::string>& ais,
	            std::uint32_t seed = 4242, Turn::SequencerConfig config = {})
		: map(mapPath(mapName))
	{
		const int humans = static_cast<int>(links.size());
		setup = makeSetup(map, humans, ais, seed);
		net.links = links;
		net.outageUntil.assign(links.size(), 0);
		net.relay = std::make_unique<Turn::TurnSequencer>(
			config, setup.humanSeatMask(),
			[humans](const std::string& t) {
				const int s = t.rfind("seat:", 0) == 0 ? std::atoi(t.c_str() + 5) : -1;
				return s < humans ? s : -1;
			},
			net, 0);
		for (int i = 0; i < humans; ++i)
			clients.push_back(std::make_unique<EngineClient>(net, i, setup, map));
		net.relay->onSequenced = [this](std::uint8_t seat, std::uint32_t sequence, std::uint32_t tick, std::uint32_t relayTick) {
			if (seat < clients.size())
				clients[seat]->trace.sequenced(sequence, tick, relayTick, net.now);
		};
		net.relay->onEmitted = [this](std::uint32_t, std::uint32_t horizon) {
			for (auto& c : clients)
				c->trace.emitted(horizon, net.now);
		};
	}

	void run(std::uint64_t duration, const std::function<void()>& each = {})
	{
		const std::uint64_t end = net.now + duration;
		while (net.now < end)
		{
			net.now += FRAME;
			net.step();
			for (auto& c : clients)
				c->frame();
			if (each)
				each();
		}
	}

	/// Ends the match the way a relay shutting down does, and lets every running
	/// client drain to the final horizon.
	std::uint32_t finish()
	{
		for (auto& c : clients)
			c->orderRate = 0;
		run(3 * SECOND);
		net.relay->finish(net.now);
		const std::uint32_t end = net.relay->horizon();
		for (int i = 0; i < 4000; ++i)
		{
			bool drained = true;
			for (auto& c : clients)
				drained &= c->stopped || c->session().executedTick() >= end;
			if (drained)
				break;
			run(FRAME);
		}
		for (auto& c : clients)
			if (!c->stopped)
				REQUIRE(c->session().executedTick() == end);
		// A little longer: each client takes the checksum before tick `end` at its next
		// step, as the verifier does.
		run(200 * MS);
		return end;
	}

	Turn::MatchRecord record(const std::string& id) const
	{
		std::array<std::uint8_t, 32> hash{};
		Online::Sha256::Digest digest;
		REQUIRE(Online::parseSha256Hex(setup.map.hash, digest));
		std::copy(digest.begin(), digest.end(), hash.begin());
		return net.relay->buildRecord(id, setup.simVersion.key(), setup.dump(), hash);
	}

	/// Every client's final run agrees at every tick it executed, with every other
	/// client's final run and with the relay's agreed checksums. Returns the ticks
	/// compared.
	std::size_t requireIdenticalChecksums(std::map<std::uint32_t, Uint32>* merged = nullptr)
	{
		std::map<std::uint32_t, Uint32> all;
		for (std::size_t i = 0; i < clients.size(); ++i)
		{
			INFO("client " << i);
			REQUIRE(!clients[i]->lives.empty());
			for (const auto& [tick, checksum] : clients[i]->lives.back())
			{
				auto [it, inserted] = all.emplace(tick, checksum);
				if (!inserted && it->second != checksum)
					FAIL_CHECK("tick " << tick << ": " << std::hex << checksum << " != " << it->second);
			}
		}
		for (const auto& [tick, checksum] : all)
			if (auto agreed = net.relay->agreedChecksum(tick))
				CHECK(*agreed == checksum);
		if (merged)
			*merged = all;
		return all.size();
	}
};

/// The teams part of a result.json for a live client's game, to compare with the
/// verifier's.
json liveTeams(EngineClient& client)
{
	std::ostringstream out;
	out << '{';
	Headless::playersAndTeamsJson(out, client.engine->gui.game, client.engine->teamEliminatedTick);
	out << '}';
	return json::parse(out.str());
}

struct Verified
{
	MatchVerifier::Verdict verdict;
	json result;
	json verdictJson;
};

Verified verifyRecord(const Turn::MatchRecord& record, const EngineMatch& match, const fs::path& directory)
{
	fs::create_directories(directory);
	const std::string path = (directory / "match.g2mr").string();
	record.writeFile(path);
	const auto reread = Turn::MatchRecord::readFile(path);
	REQUIRE(reread == record);
	const fs::path out = directory / "verify";
	fs::create_directories(out);
	Verified v;
	v.verdict = MatchVerifier::verify(reread, match.setup, match.map, out);
	v.result = json::parse(glob2test::readFile(out / "result.json"));
	v.verdictJson = json::parse(glob2test::readFile(out / "verdict.json"));
	CHECK(fs::exists(out / "checksums.txt"));
	// The replay is a standard one: the replay reader accepts it.
	ReplayReader replay;
	CHECK(replay.loadReplay((out / "match.replay").string()));
	CHECK(replay.isValid());
	return v;
}

void requireSameOutcomes(const json& verifierResult, const json& live)
{
	REQUIRE(verifierResult.at("teams").size() == live.at("teams").size());
	for (std::size_t t = 0; t < live.at("teams").size(); ++t)
	{
		const json& a = verifierResult.at("teams")[t];
		const json& b = live.at("teams")[t];
		INFO("team " << t);
		for (const char* key : {"outcome", "alive", "eliminated_tick", "prestige", "units", "buildings", "sites",
		                        "standard_statistics", "history"})
			CHECK(a.at(key) == b.at(key));
	}
	CHECK(verifierResult.at("winning_teams") == live.at("winning_teams"));
	CHECK(verifierResult.at("unresolved") == live.at("unresolved"));
}

std::string summary(EngineMatch& m, std::uint32_t endTick)
{
	std::ostringstream out;
	out << "end_tick " << endTick << " turns " << m.net.relay->turnLog().size() << " flagged "
	    << m.net.relay->desyncFlagged() << '\n';
	out << "client seat lives reloads orders target_ticks rtt_ms catch_up_ticks catch_up_ms\n";
	for (auto& c : m.clients)
	{
		out << c->index << ' ' << c->seat << ' ' << c->lives.size() << ' ' << c->reloads << ' ' << c->ordersQueued << ' ';
		if (c->stopped)
			out << "- - ";
		else
			out << c->session().targetTicks() << ' ' << c->session().rttMicros() / 1000 << ' ';
		out << c->catchUpTicks << ' ' << c->catchUpNs / 1000000 << '\n';
	}
	return out.str();
}

glob2test::GlobalsOptions harnessGlobals()
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	options.beforeLoad = [](GlobalContainer& g) {
		g.structuredHeadless = true; // no replay writer per client
		g.headlessReplay = false;
	};
	return options;
}
}

TEST_SUITE("TurnEngineHarness")
{
	GLOB2_TEST_CASE("input delay and stalls of real engines per link profile",
	                "[network-sim][benchmark][artifacts]")
	{
		// Two real engines with an AI; the measured player's link varies, the other
		// player is on a clean 15 ms link. Sim time, 5 ms frames; the bot clicks
		// right after a tick, so pickup is about one tick.
		glob2test::HeadlessGlobals globals(harnessGlobals());
		struct Profile
		{
			const char* name;
			LinkProfile link;
		};
		const std::vector<Profile> profiles = {
			{"15 ms", {15 * MS}},
			{"50 ms", {50 * MS}},
			{"60 ms, 80 ms jitter", {60 * MS, 80 * MS}},
			{"120 ms, 3% loss", {120 * MS, 0, 0.03}},
			{"120 ms, 20 ms jitter, 3% loss", {120 * MS, 20 * MS, 0.03}},
		};
		std::ostringstream table, stages;
		table << "Real engines on the simulated network (FourSquares1, two humans and a Nicowar AI), "
		         "click to execution, 5 s settle then 50 s measured.\n"
		         "link | samples | mean ms | p95 ms | target ticks | measured jitter ms | stalls | long stalls | "
		         "stalled ms | longest stall ms\n";
		stages << "link | " << turntest::LatencyTrace::header() << "\n";
		for (const auto& profile : profiles)
		{
			EngineMatch m("FourSquares1", {profile.link, {15 * MS}}, {"nicowar"});
			for (auto& c : m.clients)
				c->orderRate = 0.1;
			m.run(5 * SECOND);
			auto& c = *m.clients[0];
			const auto before = c.session().stallStats();
			c.trace.measuring = true;
			m.run(50 * SECOND);
			const auto b = c.trace.breakdown();
			const auto& st = c.session().stallStats();
			table << profile.name << " | " << b.samples << " | " << b.total.mean << " | " << b.total.p95 << " | "
			      << c.session().targetTicks() << " | " << c.session().jitterMicros() / 1000 << " | "
			      << st.stalls - before.stalls << " | " << st.longStalls - before.longStalls << " | "
			      << (st.stalledMicros - before.stalledMicros) / 1000 << " | " << st.longestMicros / 1000 << "\n";
			stages << profile.name << " | " << turntest::LatencyTrace::row(b) << "\n";
			const std::uint32_t end = m.finish();
			CHECK(m.requireIdenticalChecksums() == end + 1);
		}
		table << "\n" << stages.str();
		std::ofstream(glob2test::artifactDir() / "turn-engine-delay-profiles.txt") << table.str();
		MESSAGE(table.str());
	}

	GLOB2_TEST_CASE("two engines with AI seats under latency, jitter and loss agree at every tick and the record verifies",
	                "[network-sim][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		EngineMatch m("FourSquares1", {{20 * MS}, {70 * MS, 50 * MS, 0.03}}, {"nicowar", "warrush"});
		m.run(40 * SECOND);
		const std::uint32_t end = m.finish();
		CHECK(end > 1000);
		CHECK_FALSE(m.net.relay->desyncFlagged());
		std::map<std::uint32_t, Uint32> live;
		CHECK(m.requireIdenticalChecksums(&live) == end + 1);
		for (auto& c : m.clients)
		{
			CHECK(c->lives.size() == 1);
			CHECK(c->ordersQueued > 10);
		}
		CHECK(m.clients[1]->session().targetTicks() > m.clients[0]->session().targetTicks());

		const auto record = m.record("two-engines");
		CHECK(record.turns.size() > 20);
		CHECK(record.reports.size() >= 2 * (end / 25 - 2));
		const auto directory = glob2test::artifactDir() / "two-engines";
		const Verified v = verifyRecord(record, m, directory);
		CHECK(v.verdict.verdict == "verified");
		CHECK(v.verdictJson.at("verdict") == "verified");
		CHECK(v.verdictJson.at("outcome").at("finalTick") == m.clients[0]->engine->gui.game.stepCounter);
		CHECK(v.verdict.compared == record.reports.size());
		CHECK(v.verdict.checksums == live); // every tick, not only the reported ones
		for (auto& c : m.clients)
			requireSameOutcomes(v.result, liveTeams(*c));

		// A record whose turns were tampered with matches no client.
		// Dropping every order in the second half of the match changes the game
		// whichever of them the engine would have accepted.
		auto forged = record;
		int dropped = 0;
		for (auto& t : forged.turns)
			if (t.order[0] != ORDER_TYPE_NULL && t.order[0] != ORDER_TYPE_PLAYER_QUIT && t.tick > end / 2)
			{
				t.order = {ORDER_TYPE_NULL};
				++dropped;
			}
		REQUIRE(dropped > 5);
		const Verified f = verifyRecord(forged, m, directory / "forged-turns");
		CHECK(f.verdict.verdict == "unverifiable");
		CHECK(f.verdictJson.at("reason").get<std::string>().find("no client matches") != std::string::npos);

		// A client whose reports disagree is named.
		auto lying = record;
		for (auto& r : lying.reports)
			if (r.seat == 1 && r.tick >= 500)
				r.checksum ^= 1;
		const Verified l = verifyRecord(lying, m, directory / "lying-seat");
		CHECK(l.verdict.verdict == "diverged");
		CHECK(l.verdict.seats == std::vector<int>{1});
		CHECK(l.verdictJson.at("seats") == json::array({1}));
		CHECK(l.verdict.firstDivergence.at(1) == 500);

		glob2test::writeFile(directory / "summary.txt", summary(m, end));
		MESSAGE(summary(m, end));
	}

	GLOB2_TEST_CASE("a client that executes a tampered order is repaired by an in-place reload and named by the verifier",
	                "[network-sim][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		EngineMatch m("FourSquares1", {{20 * MS}, {40 * MS, 20 * MS}, {30 * MS, 10 * MS, 0.01}}, {"castor"});
		m.clients[1]->tamperAtTick = 400;
		m.run(45 * SECOND);
		const std::uint32_t end = m.finish();
		CHECK(m.clients[1]->tampered);
		CHECK(m.clients[1]->reloads == 1);
		REQUIRE(m.clients[1]->lives.size() == 2);
		// The first run diverged right after the tampered tick and was replaced.
		const auto& first = m.clients[1]->lives.front();
		const auto& reference = m.clients[0]->lives.back();
		CHECK(first.at(400) == reference.at(400));
		CHECK(first.at(401) != reference.at(401));
		CHECK(m.requireIdenticalChecksums() == end + 1);
		CHECK_FALSE(m.net.relay->desyncFlagged());

		const auto record = m.record("tampered-order");
		bool told = false, resynced = false;
		for (const auto& e : record.events)
		{
			told |= e.kind == Turn::MatchEventKind::ToldToRejoin && e.seat == 1;
			resynced |= e.kind == Turn::MatchEventKind::Resynced && e.seat == 1;
		}
		CHECK(told);
		CHECK(resynced);
		const auto directory = glob2test::artifactDir() / "tampered-order";
		const Verified v = verifyRecord(record, m, directory);
		CHECK(v.verdict.verdict == "diverged");
		CHECK(v.verdict.seats == std::vector<int>{1});
		CHECK(v.verdict.firstDivergence.at(1) == 425);
		requireSameOutcomes(v.result, liveTeams(*m.clients[0]));
		requireSameOutcomes(v.result, liveTeams(*m.clients[1]));

		std::ostringstream timing;
		timing << summary(m, end) << "reload fast-forward of seat 1: " << m.clients[1]->catchUpTicks << " ticks in "
		       << m.clients[1]->catchUpNs / 1000000 << " ms\n";
		glob2test::writeFile(directory / "summary.txt", timing.str());
		MESSAGE(timing.str());
	}

	GLOB2_TEST_CASE("four engines survive a stall and a restart: the restarted client reloads and fast-forwards",
	                "[network-sim][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		EngineMatch m("FourSquares1", {{15 * MS}, {60 * MS, 40 * MS}, {120 * MS, 20 * MS, 0.03}, {40 * MS, 10 * MS, 0.01}},
		              {});
		m.run(15 * SECOND);
		// Client 2 loses its link for 5 s: it stalls alone, then resumes from its horizon.
		auto& stalled = *m.clients[2];
		const std::uint32_t before0 = m.clients[0]->session().executedTick();
		m.net.outage(2, 5 * SECOND, *stalled.transport);
		bool sawReconnecting = false;
		m.run(5 * SECOND, [&] {
			sawReconnecting |= m.clients[0]->session().presence(2) == Turn::PresenceState::Reconnecting;
		});
		CHECK(m.clients[0]->session().executedTick() - before0 >= 115);
		CHECK(sawReconnecting);
		m.run(10 * SECOND);
		CHECK(stalled.lives.size() == 1); // incremental resume, no reload
		CHECK(stalled.catchUpTicks > 50);

		// Client 3 restarts (a new process): fresh engine, full log, fast-forward.
		auto& restarted = *m.clients[3];
		const std::uint32_t horizonAtRestart = m.net.relay->horizon();
		restarted.catchUpNs = 0;
		restarted.catchUpTicks = 0;
		restarted.start();
		m.run(5 * SECOND);
		CHECK(restarted.session().executedTick() >= horizonAtRestart);
		CHECK(restarted.catchUpTicks >= horizonAtRestart - 50);
		m.run(10 * SECOND);
		const std::uint32_t end = m.finish();
		CHECK(restarted.lives.size() == 2);
		CHECK(m.requireIdenticalChecksums() == end + 1);
		CHECK_FALSE(m.net.relay->desyncFlagged());

		const auto record = m.record("four-engines");
		const auto directory = glob2test::artifactDir() / "four-engines";
		const Verified v = verifyRecord(record, m, directory);
		CHECK(v.verdict.verdict == "verified");
		for (auto& c : m.clients)
			requireSameOutcomes(v.result, liveTeams(*c));

		std::ostringstream timing;
		timing << summary(m, end) << "restart fast-forward of seat 3: " << horizonAtRestart << " ticks behind, "
		       << restarted.catchUpTicks << " ticks in " << restarted.catchUpNs / 1000000 << " ms\n";
		glob2test::writeFile(directory / "summary.txt", timing.str());
		MESSAGE(timing.str());
	}

	GLOB2_TEST_CASE("a player who quits leaves through the sequenced quit order and the rest play on", "[network-sim]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		EngineMatch m("FourSquares1", {{20 * MS}, {30 * MS}, {25 * MS}}, {"numbi"});
		m.run(10 * SECOND);
		// The in-game "quit" menu: queue the quit order and flush out.
		auto& leaver = *m.clients[2];
		{
			RngScope scope(leaver.rng);
			leaver.engine->gui.orderQueue.push_back(std::make_shared<PlayerQuitsGameOrder>(leaver.seat));
			leaver.engine->gui.flushOutgoingAndExit = true;
		}
		m.run(3 * SECOND);
		CHECK(leaver.stopped);
		CHECK(leaver.engine->turnLockstep() == nullptr); // the host finished the session
		CHECK(m.net.relay->presence(2) == Turn::PresenceState::Left);
		const int team = m.setup.seats[2].team;
		CHECK_FALSE(m.clients[0]->engine->gui.game.teams[team]->isAlive);
		CHECK_FALSE(m.clients[1]->engine->gui.game.teams[team]->isAlive);
		m.run(5 * SECOND);
		const std::uint32_t end = m.finish();
		// The leaver stopped; the others agree to the end.
		m.clients.erase(m.clients.begin() + 2);
		CHECK(m.requireIdenticalChecksums() == end + 1);
		const auto record = m.record("quit");
		bool quit = false;
		for (const auto& t : record.turns)
			quit |= t.seat == 2 && t.order[0] == ORDER_TYPE_PLAYER_QUIT;
		CHECK(quit);
		const Verified v = verifyRecord(record, m, glob2test::artifactDir() / "quit");
		CHECK(v.verdict.verdict == "verified");
		requireSameOutcomes(v.result, liveTeams(*m.clients[0]));
	}

	// The cross-platform checksum job (.github/workflows/build.yml, browser/native
	// simulation equivalence) runs --verify-match on this record on Linux, Windows and
	// in three browsers and requires identical traces. A simulation change makes the
	// record stale (its clients' checksums and sim version no longer match); rerun
	// with --update-fixtures to record a fresh match and its trace.
	GLOB2_TEST_CASE("the committed match record verifies to the committed checksum trace", "[network-sim][golden]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		const fs::path record = glob2test::fixture("multiplayer/FourSquares1.g2mr");
		if (glob2test::updatingFixtures())
		{
			EngineMatch m("FourSquares1", {{20 * MS}, {60 * MS, 30 * MS, 0.02}}, {"nicowar", "warrush"}, 2026);
			m.run(25 * SECOND);
			m.finish();
			REQUIRE(m.requireIdenticalChecksums() > 600);
			fs::create_directories(record.parent_path());
			m.record("ci-fixture-foursquares1").writeFile(record.string());
		}
		const auto rec = Turn::MatchRecord::readFile(record.string());
		const auto setup = Online::MatchSetup::parse(rec.setupJson);
		glob2test::TempDir out("verify-fixture");
		const auto v = MatchVerifier::verify(rec, setup, mapPath("FourSquares1"), out.path);
		INFO(v.reason);
		CHECK(v.verdict == "verified");
		CHECK(v.compared == rec.reports.size());
		glob2test::expectGolden("multiplayer/FourSquares1.verify-trace.txt", glob2test::readFile(out.path / "checksums.txt"));
	}

	GLOB2_TEST_CASE("rejoin fast-forward time grows with game length and AI count", "[network-sim][benchmark][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		std::ostringstream table;
		table << "map ais game_minutes ticks_replayed fast_forward_ms ms_per_1000_ticks\n";
		for (const std::vector<std::string>& ais :
		     {std::vector<std::string>{"nicowar"}, std::vector<std::string>{"nicowar", "warrush", "castor"}})
			for (int minutes : {1, 5, 15, 30})
			{
				EngineMatch m("FourSquares1", {{20 * MS}}, ais);
				m.clients[0]->orderRate = 0.02;
				m.run(static_cast<std::uint64_t>(minutes) * 60 * SECOND);
				auto& c = *m.clients[0];
				const std::uint32_t behind = m.net.relay->horizon();
				c.catchUpNs = 0;
				c.catchUpTicks = 0;
				c.start();
				for (int i = 0; i < 20000 && c.session().executedTick() + 10 < m.net.relay->horizon(); ++i)
					m.run(FRAME);
				CHECK(c.catchUpTicks >= behind - 50);
				table << "FourSquares1 " << ais.size() << ' ' << minutes << ' ' << c.catchUpTicks << ' '
				      << c.catchUpNs / 1000000 << ' ' << (c.catchUpTicks ? c.catchUpNs / 1000 / c.catchUpTicks : 0) << '\n';
			}
		glob2test::writeFile(glob2test::artifactDir() / "rejoin-fast-forward.txt", table.str());
		MESSAGE(table.str());
	}
}
