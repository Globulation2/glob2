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
// verifier. A hostile client that sequences malformed and cross-team orders of every
// type must not crash, desynchronize or change any client (OrderValidation.h): every
// client and the verifier refuse the same orders. Timings of rejoin fast-forwards are
// written under artifacts/tests/.
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
#include "Brush.h"
#include "Order.h"
#include "OrderValidation.h"
#include "Player.h"
#include "ReplayReader.h"
#include "Sha256.h"
#include "SimVersion.h"
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

/// An order given as its wire bytes (type byte first): what a modified client can
/// submit, whatever the bytes.
class RawOrder : public Order
{
public:
	explicit RawOrder(std::vector<Uint8> wire) : bytes(std::move(wire)) {}
	Uint8 getOrderType() override { return bytes[0]; }
	Uint8* getData() override { return bytes.data() + 1; }
	bool setData(const Uint8*, int, Uint32) override { return false; }
	int getDataLength() override { return static_cast<int>(bytes.size()) - 1; }
	std::vector<Uint8> bytes;
};

std::vector<Uint8> wire(Order&& order)
{
	return Turn::defaultOrderCodec().encode(order);
}

Utilities::BitArray filledMask(int cells)
{
	Utilities::BitArray mask(cells);
	for (int i = 0; i < cells; ++i)
		mask.set(i, true);
	return mask;
}

/// One order of a cheating client playing `team` in `game`: each order type aimed at
/// another team, its buildings or its player, or carrying fields no user interface
/// produces; now and then a well-formed order of its own, and half of them mutated
/// byte-wise (flipped, truncated, extended, retyped). Never a pause (it would stop
/// the game for everyone, which is allowed) or the seat's own quit.
std::vector<Uint8> hostileOrder(Game& game, int team, int seat, std::mt19937& rng)
{
	auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
	const int teams = game.mapHeader.getNumberOfTeams();
	const int other = (team + 1 + pick(teams - 1)) % teams;
	auto anyBuilding = [&](int t, bool flag) -> Uint16 {
		std::vector<Uint16> gids;
		for (int i = 0; i < Building::MAX_COUNT; ++i)
			if (const Building* b = game.teams[t]->myBuildings[i])
				if (!flag || b->type->isVirtual)
					gids.push_back(b->gid);
		return gids.empty() ? Building::GIDfrom(pick(Building::MAX_COUNT), t) : gids[pick(static_cast<int>(gids.size()))];
	};
	const Team* own = game.teams[team];
	const int x = own->startPosX + pick(21) - 10, y = own->startPosY + pick(21) - 10;
	const int site = globalContainer->buildingsTypes.getTypeNum("inn", 0, true);
	const int flag = globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
	Sint32 ratio[NB_UNIT_TYPE];
	for (auto& r : ratio)
		r = pick(40) - 20;
	bool clearing[BASIC_COUNT];
	for (auto& c : clearing)
		c = pick(2);
	std::vector<Uint8> bytes;
	switch (pick(27))
	{
	case 0: bytes = wire(OrderCreate(other, x, y, site, 2, 2)); break;
	case 1: bytes = wire(OrderCreate(team, x, y, globalContainer->buildingsTypes.getTypeNum("inn", 2, false), 2, 2)); break;
	case 2: bytes = wire(OrderCreate(team, x, y, pick(1 << 16) - 100, 2, 2)); break;
	case 3: bytes = wire(OrderCreate(team, x, y, flag, 1000, -3, pick(2) ? 100000 : -9)); break;
	case 4: bytes = wire(OrderDelete(anyBuilding(other, false))); break;
	case 5:
	{
		OrderDelete order(0); // the constructor asserts on ids no client has
		order.gid = static_cast<Uint16>(rng());
		bytes = wire(std::move(order));
		break;
	}
	case 6: bytes = wire(OrderCancelDelete(anyBuilding(pick(2) ? team : other, false))); break;
	case 7: bytes = wire(OrderConstruction(anyBuilding(pick(2) ? team : other, false), rng(), rng())); break;
	case 8: bytes = wire(OrderCancelConstruction(anyBuilding(pick(2) ? team : other, false), rng())); break;
	case 9: bytes = wire(OrderChangePriority(anyBuilding(pick(2) ? team : other, false), static_cast<Sint32>(rng()))); break;
	case 10: bytes = wire(OrderModifyBuilding(anyBuilding(pick(2) ? team : other, false), static_cast<Uint16>(pick(600)))); break;
	case 11: bytes = wire(OrderModifyExchange(anyBuilding(other, false), rng(), rng())); break;
	case 12: bytes = wire(OrderModifySwarm(anyBuilding(pick(2) ? team : other, false), ratio)); break;
	case 13: bytes = wire(OrderModifyFlag(anyBuilding(pick(2) ? team : other, true), pick(2) ? pick(5000) : -pick(5000))); break;
	case 14: bytes = wire(OrderModifyClearingFlag(anyBuilding(other, true), clearing)); break;
	case 15: bytes = wire(OrderModifyMinLevelToFlag(anyBuilding(pick(2) ? team : other, true), static_cast<Uint16>(rng()))); break;
	case 16: bytes = wire(OrderMoveFlag(anyBuilding(pick(2) ? team : other, true), static_cast<Sint32>(rng()), static_cast<Sint32>(rng()), true)); break;
	case 17:
	{
		const int w = 1 + pick(40), h = 1 + pick(40);
		const Uint8 t = static_cast<Uint8>(pick(2) ? other : pick(256));
		const Uint8 mode = static_cast<Uint8>(pick(2) ? BrushTool::MODE_ADD : pick(256));
		switch (pick(3))
		{
		case 0: bytes = wire(OrderAlterForbidden(t, mode, x, y, w, h, filledMask(w * h))); break;
		case 1: bytes = wire(OrderAlterGuardArea(t, mode, x, y, w, h, filledMask(w * h))); break;
		default: bytes = wire(OrderAlterClearArea(t, mode, x, y, w, h, filledMask(w * h))); break;
		}
		break;
	}
	case 18: bytes = wire(MessageOrder(rng(), 3 + pick(100), "cheat")); break;
	case 19:
	{
		std::vector<Uint8> frames(1 + pick(200));
		for (auto& b : frames)
			b = static_cast<Uint8>(rng());
		bytes = wire(OrderVoiceData(rng(), frames.size(), static_cast<Uint8>(pick(256)), frames.data()));
		break;
	}
	case 20: bytes = wire(SetAllianceOrder(pick(2) ? other : rng(), rng(), rng(), rng(), rng(), rng())); break;
	case 21: bytes = wire(SetAllianceOrder(team, ~0u, 0, ~0u, ~0u, ~0u)); break;
	case 22: bytes = wire(MapMarkOrder(pick(2) ? other : rng(), rng(), rng())); break;
	case 23: bytes = wire(PlayerQuitsGameOrder((seat + 1 + pick(31)) % 32)); break; // the relay drops these
	case 24:
		bytes.resize(1 + pick(64));
		for (auto& b : bytes)
			b = static_cast<Uint8>(rng());
		break;
	case 25: bytes = wire(OrderCreate(team, x, y, site, 1 + pick(5), 1 + pick(5))); break; // a fair one
	default: bytes = wire(OrderModifyBuilding(anyBuilding(team, false), static_cast<Uint16>(pick(21)))); break;
	}
	if (pick(2))
	{
		switch (pick(4))
		{
		case 0:
			for (int i = 0, n = 1 + pick(4); i < n && bytes.size() > 1; ++i)
				bytes[1 + pick(static_cast<int>(bytes.size()) - 1)] ^= static_cast<Uint8>(1 + pick(255));
			break;
		case 1: bytes.resize(1 + pick(static_cast<int>(bytes.size()))); break;
		case 2: bytes.resize(bytes.size() + 1 + pick(8), static_cast<Uint8>(rng())); break;
		default: bytes[0] = static_cast<Uint8>(rng()); break;
		}
	}
	std::uint8_t ownQuit[5];
	Turn::encodePlayerQuitOrder(static_cast<std::uint8_t>(seat), ownQuit);
	if (bytes.empty() || bytes[0] == ORDER_PAUSE_GAME ||
	    (bytes.size() == 5 && std::equal(ownQuit, ownQuit + 5, bytes.begin())))
		return {};
	if (bytes.size() > Turn::MAX_ORDER_BYTES)
		bytes.resize(Turn::MAX_ORDER_BYTES);
	return bytes;
}

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
		lockstep->orderFilter = [this](std::uint32_t tick, int player, std::shared_ptr<Order> order) {
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
			if (after > before && !catching && hostileRate > 0 && std::uniform_real_distribution<double>(0, 1)(bot) < hostileRate)
				queueHostileOrder();
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
			++ordersQueued;
		}
	}

	/// Submits a hostileOrder straight to the session, bypassing the user interface.
	void queueHostileOrder()
	{
		auto bytes = hostileOrder(engine->gui.game, engine->gui.localTeamNo, seat, bot);
		if (bytes.empty())
			return;
		session().addLocalOrder(std::make_shared<RawOrder>(std::move(bytes)));
		++hostileSent;
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
	double hostileRate = 0;
	int hostileSent = 0;
	std::uint32_t tamperAtTick = UINT32_MAX;
	bool tampered = false;
	int reloads = 0;
	int ordersQueued = 0;
	std::int64_t catchUpNs = 0;
	std::uint32_t catchUpTicks = 0;
	/// Per run of the engine from tick 0 (a restart or an in-place reload starts a
	/// new one): tick -> checksum before that tick.
	std::vector<std::map<std::uint32_t, Uint32>> lives;
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

/// The order audits of two runs agree (voice aside: only live clients see it).
void requireSameAudit(const OrderValidation::Audit& a, const OrderValidation::Audit& b)
{
	for (std::size_t seat = 0; seat < OrderValidation::Audit::SEATS; ++seat)
	{
		INFO("seat " << seat);
		CHECK(a.seats[seat].accepted == b.seats[seat].accepted);
		CHECK(a.seats[seat].stale == b.seats[seat].stale);
		CHECK(a.seats[seat].rejected == b.seats[seat].rejected);
		CHECK(a.seats[seat].firstRejectedTick == b.seats[seat].firstRejectedTick);
		CHECK(a.seats[seat].reasons == b.seats[seat].reasons);
	}
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
		auto forged = record;
		forged.turns[forged.turns.size() / 2].order = {ORDER_TYPE_NULL};
		bool changed = false;
		for (auto& t : forged.turns)
			if (t.order[0] != ORDER_TYPE_NULL && t.order[0] != ORDER_TYPE_PLAYER_QUIT && t.tick > 200)
			{
				// Move a real order to a later tick: same orders, different game.
				t.tick += 1;
				changed = true;
				break;
			}
		REQUIRE(changed);
		std::sort(forged.turns.begin(), forged.turns.end(),
		          [](const Turn::TurnEntry& a, const Turn::TurnEntry& b) { return a.tick != b.tick ? a.tick < b.tick : a.seat < b.seat; });
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

	GLOB2_TEST_CASE("a client sending hostile orders of every type crashes no one and is refused identically everywhere",
	                "[network-sim][fuzz][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		std::ostringstream report;
		// Three matches, each with its own cheater stream and game seed.
		for (const std::uint32_t seed : {101u, 202u, 303u})
		{
			INFO("seed " << seed);
			// The previous round's verifier switched the replay writer on.
			globals->headlessReplay = false;
			globals->structuredHeadless = true;
			EngineMatch m("FourSquares1", {{20 * MS}, {50 * MS, 30 * MS, 0.02}, {35 * MS, 15 * MS}}, {"nicowar"}, seed);
			auto& cheater = *m.clients[1];
			cheater.bot.seed(seed);
			cheater.hostileRate = 0.6;
			m.run(90 * SECOND);
			const std::uint32_t end = m.finish();
			CHECK(cheater.hostileSent > 300);
			CHECK_FALSE(m.net.relay->desyncFlagged());
			CHECK(m.requireIdenticalChecksums() == end + 1);
			for (auto& c : m.clients)
			{
				INFO("client " << c->index);
				CHECK(c->lives.size() == 1);
			}

			// Every client counted the same verdicts for the same seats. (The cheater may
			// have destroyed its own colony and left; the honest seats play to the end.)
			REQUIRE_FALSE(m.clients[0]->stopped);
			const auto& audit = m.clients[0]->engine->turnLockstep()->orderAudit();
			for (auto& c : m.clients)
				if (!c->stopped)
					requireSameAudit(audit, c->engine->turnLockstep()->orderAudit());
			const auto& honest = audit.seats[0];
			const auto& hostile = audit.seats[1];
			CHECK(honest.rejected == 0);
			CHECK(honest.stale == 0);
			CHECK(honest.accepted > 20);
			CHECK(audit.seats[2].rejected == 0);
			CHECK(hostile.rejected > 100);
			CHECK(hostile.accepted > 10);
			using OrderValidation::Reason;
			for (Reason r : {Reason::Undecodable, Reason::WrongTeam, Reason::ForeignBuilding, Reason::BadBuildingType,
			                 Reason::OutOfRange, Reason::BadMode})
			{
				INFO(OrderValidation::name(r));
				CHECK(hostile.reasons[static_cast<std::size_t>(r)] > 0);
			}
			CHECK(m.clients[0]->engine->turnLockstep()->orderAudit().seats[1].voiceRejected > 0);

			// The verifier replays the record, refuses the same orders and agrees.
			const auto record = m.record("hostile-orders");
			const auto directory = glob2test::artifactDir() / ("hostile-orders-" + std::to_string(seed));
			const Verified v = verifyRecord(record, m, directory);
			CHECK(v.verdict.verdict == "verified");
			requireSameAudit(v.verdict.orders, audit);
			for (auto& c : m.clients)
				requireSameOutcomes(v.result, liveTeams(*c));
			const json& checks = v.result.at("verification").at("order_checks");
			CHECK(checks.at("1").at("rejected") == hostile.rejected);
			CHECK(checks.at("0").at("rejected") == 0);
			const json& flagged = v.verdictJson.at("orderRejections");
			REQUIRE(flagged.size() == 1);
			CHECK(flagged[0].at("seat") == 1);
			CHECK(flagged[0].at("rejected") == hostile.rejected);
			CHECK(flagged[0].at("firstRejectedTick") == hostile.firstRejectedTick);

			report << "seed " << seed << '\n' << summary(m, end) << "hostile orders sent " << cheater.hostileSent
			       << "; seat 1 accepted " << hostile.accepted << " stale " << hostile.stale << " rejected " << hostile.rejected
			       << " voice-rejected " << audit.seats[1].voiceRejected << "\n";
			for (std::size_t r = 1; r < OrderValidation::REASON_COUNT; ++r)
				report << "  " << OrderValidation::name(static_cast<Reason>(r)) << ' ' << hostile.reasons[r] << '\n';
		}
		glob2test::writeFile(glob2test::artifactDir() / "hostile-orders-summary.txt", report.str());
		MESSAGE(report.str());
	}

	GLOB2_TEST_CASE("orders a hostile relay forges for a seat are refused by the verifier, which still verifies",
	                "[network-sim]")
	{
		// The relay drops a client's quit order for another seat and its latency
		// orders, but a modified relay or a doctored record need not. The engine
		// refuses them on its own, so the record replays to the clients' checksums.
		glob2test::HeadlessGlobals globals(harnessGlobals());
		EngineMatch m("FourSquares1", {{20 * MS}, {30 * MS}}, {"warrush"});
		m.run(10 * SECOND);
		const std::uint32_t end = m.finish();
		CHECK(m.requireIdenticalChecksums() == end + 1);
		const auto record = m.record("forged-orders");
		auto forged = record;
		std::set<std::uint32_t> taken;
		for (const auto& t : forged.turns)
			if (t.seat == 1)
				taken.insert(t.tick);
		std::uint8_t quitSeat0[5];
		Turn::encodePlayerQuitOrder(0, quitSeat0);
		const std::vector<std::vector<std::uint8_t>> injected = {
			{quitSeat0, quitSeat0 + 5},                                    // seat 1 quits for seat 0
			wire(AdjustLatency(40)),                                       // a legacy latency order
			{0xEE, 1, 2, 3},                                               // no such order type
			wire(OrderCreate(0, 10, 10, 0, 1, 1)),                         // builds for seat 0's team
			wire(SetAllianceOrder(0, ~0u, 0, ~0u, ~0u, ~0u)),             // allies seat 0 with everyone
			wire(OrderVoiceData(~0u, 3, 250, std::vector<Uint8>{1, 2, 3}.data())), // oversized voice
			wire(OrderVoiceData(~0u, 3, 1, std::vector<Uint8>{1, 2, 3}.data())),   // ordinary voice
		};
		std::uint32_t tick = 100;
		for (const auto& bytes : injected)
		{
			while (taken.count(tick))
				++tick;
			Turn::TurnEntry entry;
			entry.tick = tick++;
			entry.seat = 1;
			entry.order = bytes;
			forged.turns.push_back(entry);
		}
		std::sort(forged.turns.begin(), forged.turns.end(), [](const Turn::TurnEntry& a, const Turn::TurnEntry& b) {
			return a.tick != b.tick ? a.tick < b.tick : a.seat < b.seat;
		});
		const Verified v = verifyRecord(forged, m, glob2test::artifactDir() / "forged-orders");
		CHECK(v.verdict.verdict == "verified");
		const auto& seat1 = v.verdict.orders.seats[1];
		using OrderValidation::Reason;
		CHECK(seat1.rejected == 5);
		CHECK(seat1.voiceRejected == 1);
		CHECK(seat1.reasons[static_cast<std::size_t>(Reason::WrongPlayer)] == 1);
		CHECK(seat1.reasons[static_cast<std::size_t>(Reason::NotPermitted)] == 1);
		CHECK(seat1.reasons[static_cast<std::size_t>(Reason::Undecodable)] == 1);
		CHECK(seat1.reasons[static_cast<std::size_t>(Reason::WrongTeam)] == 2);
		CHECK(seat1.firstRejectedTick >= 100);
		CHECK(v.verdict.orders.seats[0].rejected == 0);
		// Seat 0 did not leave and its team kept its alliances.
		CHECK(v.result.at("teams")[0].at("alive") == liveTeams(*m.clients[0]).at("teams")[0].at("alive"));
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
