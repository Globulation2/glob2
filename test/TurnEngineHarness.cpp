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

#include "ConnectionOverlay.h"
#include "EndGameScreen.h"
#include "Engine.h"
#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Headless.h"
#include "MatchRecord.h"
#include "MatchSetup.h"
#include "MersenneTwister.h"
#include "BinaryStream.h"
#include "Brush.h"
#include "Building.h"
#include "MapHeader.h"
#include "StreamBackend.h"
#include "Unit.h"
#include "Order.h"
#include "OrderValidation.h"
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

/// A setup on `map`: humans on the first teams, then one team per entry of `ais`,
/// every team its own alliance. An entry "closed" closes its team (a closed seat
/// after every player seat, as a room sends an empty seat). Unused map teams stay
/// listed (the contract lists every map team).
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
	std::vector<Online::SetupSeat> closed;
	for (std::size_t a = 0; a < ais.size(); ++a)
	{
		Online::SetupSeat seat;
		seat.team = humans + static_cast<int>(a);
		seat.human = false;
		if (ais[a] == "closed")
		{
			seat.closed = true;
			closed.push_back(seat);
			continue;
		}
		seat.seat = static_cast<int>(setup.seats.size());
		seat.ai = ais[a];
		seat.name = "AI " + std::to_string(a + 1);
		setup.seats.push_back(seat);
	}
	for (auto& seat : closed)
	{
		seat.seat = static_cast<int>(setup.seats.size());
		setup.seats.push_back(seat);
	}
	setup.validateSemantics();
	return setup;
}

/// The session configuration every EngineClient starts with (a case may change it
/// for its matches through SessionConfigScope).
Turn::TurnSessionConfig& sessionConfig()
{
	static Turn::TurnSessionConfig config;
	return config;
}

struct SessionConfigScope
{
	Turn::TurnSessionConfig saved = sessionConfig();
	explicit SessionConfigScope(const Turn::TurnSessionConfig& config) { sessionConfig() = config; }
	~SessionConfigScope() { sessionConfig() = saved; }
};

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
		start.config = sessionConfig();
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
			{
				trace.executed(tick, net.now);
				if (onOwnOrder)
					onOwnOrder(tick, *order);
			}
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
			trace.queued(net.now);
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
	/// Sees every order of this client's own seat as it executes (after validation).
	std::function<void(std::uint32_t tick, Order& order)> onOwnOrder;
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
		: EngineMatch(makeSetup(mapPath(mapName), static_cast<int>(links.size()), ais, seed), mapPath(mapName), links,
		              config)
	{
	}

	/// A match of `given` on the map or save `mapFile`; its human seats are 0..links-1.
	EngineMatch(Online::MatchSetup given, std::string mapFile, const std::vector<LinkProfile>& links,
	            Turn::SequencerConfig config = {})
		: setup(std::move(given)), map(std::move(mapFile))
	{
		const int humans = static_cast<int>(links.size());
		REQUIRE(setup.humanSeatMask() == (1u << humans) - 1);
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
struct DragResult
{
	std::size_t queuedMoves = 0, executedMoves = 0;
	turntest::LatencyTrace::Stage move;
	double maxMove = 0;
	double dropMs = -1; ///< drop queued -> executed locally; -1 if it never executed
	double keyMs = -1;  ///< last key press -> executed locally
	std::uint64_t flooded = 0, coalesced = 0, queuedMax = 0, maxAhead = 0;
	bool rejected = false, identical = false, verified = false;
};

/// Seat 0 drags one of its flags for 10 s through GameGUI::queueFlagMove, as the mouse
/// does (a new cell every other 5 ms frame), while holding a key that sets a building's
/// worker count about 29 times a second; then drops the flag and releases the key.
/// Measures, on seat 0's own client, how long each queued flag position took to
/// execute (positions are unique, so a coalesced one simply never executes).
DragResult dragAndHold(const Turn::TurnSessionConfig& config, const fs::path& verifyDirectory)
{
	SessionConfigScope configScope(config);
	EngineMatch m("FourSquares1", {{15 * MS}, {15 * MS}}, {"nicowar"});
	auto& c = *m.clients[0];
	c.orderRate = 0;
	m.clients[1]->orderRate = 0.05;
	m.run(2 * SECOND);
	Game& game = c.engine->gui.game;
	Team* team = game.teams[c.engine->gui.localTeamNo];
	{
		RngScope scope(c.rng);
		const int flagType = globalContainer->buildingsTypes.getTypeNum("explorationflag", 0, false);
		c.engine->gui.orderQueue.push_back(std::make_shared<OrderCreate>(team->teamNumber, team->startPosX + 2,
		                                                                 team->startPosY + 2, flagType, 3, 3));
	}
	m.run(2 * SECOND);
	Uint16 flagGid = 0, hallGid = 0;
	bool haveFlag = false, haveHall = false;
	for (int i = 0; i < Building::MAX_COUNT; ++i)
		if (const Building* b = team->myBuildings[i])
		{
			if (b->type->isVirtual && !haveFlag)
				flagGid = b->gid, haveFlag = true;
			else if (!b->type->isVirtual && b->type->maxUnitWorking > 0 && !haveHall)
				hallGid = b->gid, haveHall = true;
		}
	REQUIRE(haveFlag);
	REQUIRE(haveHall);

	std::map<std::pair<int, int>, std::uint64_t> queuedAt;
	std::set<std::pair<int, int>> executed;
	std::vector<double> delays;
	std::pair<int, int> dropAt{-1, -1};
	std::uint64_t dropQueued = 0, keyQueued = 0;
	constexpr Uint16 LAST_KEY_VALUE = 10;
	DragResult r;
	c.onOwnOrder = [&](std::uint32_t, Order& order) {
		if (order.getOrderType() == ORDER_MOVE_FLAG)
		{
			auto& move = static_cast<OrderMoveFlag&>(order);
			const std::pair<int, int> at{move.x, move.y};
			if (move.gid != flagGid || !executed.insert(at).second)
				return;
			auto it = queuedAt.find(at);
			if (it == queuedAt.end())
				return;
			const double ms = double(m.net.now - it->second) / 1000.0;
			if (at == dropAt && move.drop)
				r.dropMs = double(m.net.now - dropQueued) / 1000.0;
			else
				delays.push_back(ms);
		}
		else if (order.getOrderType() == ORDER_MODIFY_BUILDING)
		{
			auto& modify = static_cast<OrderModifyBuilding&>(order);
			if (modify.gid == hallGid && modify.numberRequested == LAST_KEY_VALUE && r.keyMs < 0)
				r.keyMs = double(m.net.now - keyQueued) / 1000.0;
		}
	};
	auto queueMove = [&](int i, bool drop) {
		RngScope scope(c.rng);
		Building* flag = game.lookupBuilding(flagGid);
		REQUIRE(flag);
		const int x = (team->startPosX + (i % 30) - 15) & game.map.getMaskW();
		const int y = (team->startPosY + (i / 30) - 20) & game.map.getMaskH();
		queuedAt.emplace(std::make_pair(x, y), m.net.now);
		c.engine->gui.queueFlagMove(*flag, x, y, drop);
		if (drop)
		{
			dropAt = {x, y};
			dropQueued = m.net.now;
		}
	};
	auto press = [&](Uint16 value) {
		RngScope scope(c.rng);
		c.engine->gui.orderQueue.push_back(std::make_shared<OrderModifyBuilding>(hallGid, value));
		keyQueued = m.net.now;
	};
	int frame = 0, cell = 0;
	m.run(10 * SECOND, [&] {
		if (frame % 2 == 0)
			queueMove(cell++, false);
		if (frame % 7 == 0)
			press(static_cast<Uint16>(1 + (frame / 7) % 8));
		++frame;
	});
	queueMove(cell++, true);
	press(LAST_KEY_VALUE);
	m.run(3 * SECOND);

	r.queuedMoves = queuedAt.size() - 1;
	r.executedMoves = delays.size();
	r.move = turntest::LatencyTrace::summarize(delays);
	for (double d : delays)
		r.maxMove = std::max(r.maxMove, d);
	r.flooded = m.net.relay->stats().ordersFlooded;
	r.maxAhead = m.net.relay->telemetry().seats[0].maxQueuedAhead;
	r.coalesced = c.session().telemetry().totals().ordersCoalesced;
	r.queuedMax = c.session().telemetry().queuedMax();
	r.rejected = c.session().state() == Turn::TurnSession::State::Rejected;
	c.onOwnOrder = nullptr;
	const std::uint32_t end = m.finish();
	r.identical = m.requireIdenticalChecksums() == end + 1;
	if (!verifyDirectory.empty())
	{
		const Verified v = verifyRecord(m.record("drag"), m, verifyDirectory);
		r.verified = v.verdict.verdict == "verified";
	}
	return r;
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

	GLOB2_TEST_CASE("a player who quits leaves through the relay's quit order and the rest play on", "[network-sim]")
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
		// It left with a Quit (the quit order is the relay's), and an undecided game
		// stays undecided: the others' reconnect grace still applies.
		CHECK_FALSE(m.net.relay->gameDecided());
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

	// What the others see when someone leaves (docs/multiplayer/client.md): the leaver
	// stays in the connection panel as Left, and the player left standing wins and is
	// told why on the results.
	GLOB2_TEST_CASE("a player who leaves stays listed as Left, and the one left standing learns why it won",
	                "[network-sim]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		EngineMatch m("FourSquares1", {{20 * MS}, {30 * MS}}, {"closed", "closed"});
		for (auto& c : m.clients)
			c->orderRate = 0;
		m.run(5 * SECOND);
		auto& leaver = *m.clients[1];
		const std::string leaverName = leaver.engine->gui.game.gameHeader.getBasePlayer(1).name;
		REQUIRE_FALSE(leaverName.empty());
		{
			RngScope scope(leaver.rng);
			leaver.engine->gui.orderQueue.push_back(std::make_shared<PlayerQuitsGameOrder>(leaver.seat));
			leaver.engine->gui.flushOutgoingAndExit = true;
		}
		m.run(5 * SECOND);
		CHECK(leaver.stopped);
		auto& winner = *m.clients[0];
		REQUIRE_FALSE(winner.stopped);
		const ConnectionSnapshot snapshot = winner.engine->turnConnectionSnapshot();
		const auto row = std::find_if(snapshot.rows.begin(), snapshot.rows.end(), [](const ConnectionRow& r) { return r.seat == 1; });
		REQUIRE(row != snapshot.rows.end());
		CHECK(row->state == ConnectionRow::State::Left);
		CHECK(row->name == leaverName);
		Game& game = winner.engine->gui.game;
		REQUIRE(game.teams[0]->hasWon);
		const auto described = EndGameScreen::describe(game, *game.teams[0]);
		CHECK(described.outcome == EndGameScreen::Outcome::Victory);
		CHECK(described.reason.find(leaverName) != std::string::npos);
		// The leaver's own view of its game: it left, which counts as a loss.
		Game& left = leaver.engine->gui.game;
		CHECK(EndGameScreen::describe(left, *left.teams[1]).outcome == EndGameScreen::Outcome::Left);
	}

	// Online rooms send empty and locked seats as closed seats. A closed team has no
	// player, so it starts without a colony and has lost at once, exactly like a
	// "Closed" colony in a custom game, and a player who defeats every real opponent
	// wins. Rooms used to send them as AI `none`: idle colonies that stay alive, so
	// opponents-defeated never fired and the game only ended by sudden death.
	GLOB2_TEST_CASE("closed seats are closed colonies: the last player standing wins everywhere and the record verifies",
	                "[network-sim]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		struct Outcome
		{
			std::uint32_t eliminatedAt = 0; ///< game tick team 1 lost (0: it did not)
			bool survivorWon = false;       ///< on every client
		};
		// Two humans on FourSquares1 (four teams); teams 2 and 3 are `seats`. At 10 s
		// player 2 gives up: it deletes every building it has, and its starving colony
		// dies about half a minute later.
		auto play = [](const std::vector<std::string>& seats, const std::string& name, bool verify) {
			EngineMatch m("FourSquares1", {{20 * MS}, {45 * MS, 10 * MS, 0.01}}, seats, 4711);
			for (auto& c : m.clients)
				c->orderRate = 0;
			m.run(10 * SECOND);
			Outcome outcome;
			for (auto& c : m.clients)
			{
				Game& game = c->engine->gui.game;
				for (int team = 2; team < 4; ++team)
				{
					INFO("client " << c->index << " team " << team);
					// A closed team was removed at the start and has lost; an idle
					// AI `none` colony is still alive.
					CHECK(game.teams[team]->hasLost == (seats[0] == "closed"));
					CHECK(game.teams[team]->isAlive == (seats[0] != "closed"));
				}
				CHECK(game.gameHeader.getNumberOfPlayers() == (seats[0] == "closed" ? 2 : 4));
			}
			auto& loser = *m.clients[1];
			{
				RngScope scope(loser.rng);
				Team* team = loser.engine->gui.game.teams[1];
				for (int i = 0; i < Building::MAX_COUNT; ++i)
					if (const Building* b = team->myBuildings[i])
						loser.engine->gui.orderQueue.push_back(std::make_shared<OrderDelete>(b->gid));
				REQUIRE(!loser.engine->gui.orderQueue.empty());
			}
			// Up to five game minutes for the colony to starve.
			for (int second = 0; second < 300 && !outcome.eliminatedAt; ++second)
			{
				m.run(SECOND);
				Game& game = m.clients[0]->engine->gui.game;
				if (game.teams[1]->hasLost)
					outcome.eliminatedAt = game.stepCounter;
			}
			REQUIRE(outcome.eliminatedAt > 0);
			m.run(10 * SECOND);
			outcome.survivorWon = true;
			for (auto& c : m.clients)
			{
				Game& game = c->engine->gui.game;
				INFO("client " << c->index);
				CHECK(game.teams[1]->hasLost);
				CHECK_FALSE(game.teams[0]->hasLost);
				outcome.survivorWon &= game.teams[0]->hasWon;
			}
			const std::uint32_t end = m.finish();
			CHECK_FALSE(m.net.relay->desyncFlagged());
			CHECK(m.requireIdenticalChecksums() > outcome.eliminatedAt);
			CHECK(end > outcome.eliminatedAt);
			if (verify)
			{
				const Verified v = verifyRecord(m.record(name), m, glob2test::artifactDir() / name);
				CHECK(v.verdict.verdict == "verified");
				for (auto& c : m.clients)
					requireSameOutcomes(v.result, liveTeams(*c));
				CHECK(v.result.at("winning_teams") == json::array({0}));
				CHECK(v.result.at("unresolved") == false);
				for (int team = 1; team < 4; ++team)
					CHECK(v.result.at("teams")[team].at("outcome") == "lost");
				// The players are seats 0 and 1; the closed seats are no players.
				CHECK(v.result.at("players").size() == 2);
			}
			MESSAGE(name << ": team 1 eliminated at tick " << outcome.eliminatedAt << ", survivor won: "
			             << outcome.survivorWon);
			return outcome;
		};
		// The old representation of the same room: the survivor never wins. (First:
		// the verifier below leaves its replay writer installed.)
		const Outcome idle = play({"none", "none"}, "idle-seats", false);
		CHECK_FALSE(idle.survivorWon);
		const Outcome closed = play({"closed", "closed"}, "closed-seats", true);
		CHECK(closed.survivorWon);
	}

	// An uploaded save as the map, reteamed: the two returning players take saved
	// teams 3 and 1, and the room's other seats were empty, so teams 0 and 2 are closed.
	// Their saved colonies are cleared at the start, as on a new map.
	GLOB2_TEST_CASE("a save with closed seats: reteamed players keep their colonies, closed ones are cleared",
	                "[network-sim]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		const fs::path directory = glob2test::artifactDir() / "closed-save";
		fs::create_directories(directory);
		const std::string save = (directory / "source.game").string();
		{
			// Four live colonies, 20 seconds in.
			EngineMatch source("FourSquares1", {{20 * MS}, {30 * MS}}, {"numbi", "numbi"}, 515);
			source.run(20 * SECOND);
			auto& c = *source.clients[0];
			RngScope scope(c.rng);
			FILE* file = std::fopen(save.c_str(), "wb");
			REQUIRE(file);
			GAGCore::BinaryOutputStream out(new GAGCore::FileStreamBackend(file));
			c.engine->gui.save(&out, "closed seats source");
		}
		const MapHeader saved = Engine::loadMapHeader(save);
		REQUIRE(saved.getIsSavedGame());
		REQUIRE(saved.getNumberOfTeams() == 4);

		Online::MatchSetup setup;
		setup.simVersion = Online::currentSimVersion();
		setup.seed = 99;
		setup.map.kind = Online::MapSource::Kind::Upload;
		setup.map.format = Online::MapSource::Format::Save;
		setup.map.hash = Online::mapContentHash(save);
		for (int t = 0; t < 4; ++t)
			setup.teams.push_back({t, t});
		auto human = [&](int seat, int team) {
			Online::SetupSeat s;
			s.seat = seat;
			s.team = team;
			s.name = "Returning " + std::to_string(seat + 1);
			setup.seats.push_back(s);
		};
		auto closed = [&](int seat, int team) {
			Online::SetupSeat s;
			s.seat = seat;
			s.team = team;
			s.human = false;
			s.closed = true;
			setup.seats.push_back(s);
		};
		human(0, 3);
		human(1, 1);
		closed(2, 0);
		closed(3, 2);
		setup.validateSemantics();
		// Through the JSON every client and the verifier parse.
		setup = Online::MatchSetup::parse(setup.dump());

		EngineMatch m(setup, save, {{20 * MS}, {40 * MS}});
		m.run(10 * SECOND);
		for (auto& c : m.clients)
		{
			INFO("client " << c->index);
			Game& game = c->engine->gui.game;
			CHECK(game.gameHeader.getNumberOfPlayers() == 2);
			CHECK(c->engine->gui.localTeamNo == (c->seat == 0 ? 3 : 1));
			for (int team : {0, 2})
			{
				INFO("closed team " << team);
				CHECK(game.teams[team]->hasLost);
				CHECK(game.teams[team]->playersMask == 0);
				for (int i = 0; i < Unit::MAX_COUNT; ++i)
					CHECK(game.teams[team]->myUnits[i] == nullptr);
				for (int i = 0; i < Building::MAX_COUNT; ++i)
					CHECK(game.teams[team]->myBuildings[i] == nullptr);
			}
			for (int team : {1, 3})
			{
				INFO("reteamed team " << team);
				CHECK(game.teams[team]->isAlive);
				int units = 0;
				for (int i = 0; i < Unit::MAX_COUNT; ++i)
					units += game.teams[team]->myUnits[i] != nullptr;
				CHECK(units > 0);
			}
		}
		const std::uint32_t end = m.finish();
		CHECK_FALSE(m.net.relay->desyncFlagged());
		CHECK(m.requireIdenticalChecksums() == end + 1);
		const Verified v = verifyRecord(m.record("closed-save"), m, directory);
		CHECK(v.verdict.verdict == "verified");
		for (auto& c : m.clients)
			requireSameOutcomes(v.result, liveTeams(*c));
		CHECK(v.result.at("teams")[0].at("outcome") == "lost");
		CHECK(v.result.at("teams")[2].at("outcome") == "lost");
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
	GLOB2_TEST_CASE("a 10 s flag drag and a held key keep the input delay bounded, without a flood",
	                "[network-sim][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		Turn::TurnSessionConfig paced;
		// Before this change every order left at once: as many as the GUI made.
		Turn::TurnSessionConfig unpaced;
		unpaced.orderBurst = 1u << 30;
		// The unpaced match first: verifying a record leaves the process's replay
		// writer set, which a later match in the same process would trip over.
		const DragResult before = dragAndHold(unpaced, {});
		const DragResult now = dragAndHold(paced, glob2test::artifactDir() / "drag-paced");

		std::ostringstream table;
		table << "A 10 s drag of one flag (a new cell every 10 ms) while a key changes a building's worker count "
		         "~29 times a second, then the drop; two engines on clean 15 ms links (FourSquares1, Nicowar AI).\n"
		         "client | queued moves | executed moves | move delay mean / p95 / max ms | drop delay ms | "
		         "last key press ms | orders flooded at the relay | coalesced | local queue max | relay queue ahead max ticks\n";
		for (const auto* r : {&before, &now})
			table << (r == &now ? "paced (this change)" : "unpaced (before)") << " | " << r->queuedMoves << " | "
			      << r->executedMoves << " | " << r->move.mean << " / " << r->move.p95 << " / " << r->maxMove << " | "
			      << r->dropMs << " | " << r->keyMs << " | " << r->flooded << " | " << r->coalesced << " | "
			      << r->queuedMax << " | " << r->maxAhead << "\n";
		std::ofstream(glob2test::artifactDir() / "turn-engine-drag.txt") << table.str();
		MESSAGE(table.str());

		CHECK(now.identical);
		CHECK(now.verified);
		CHECK_FALSE(now.rejected);
		CHECK(now.flooded == 0);
		CHECK(now.executedMoves > 100);
		CHECK(now.maxMove < 300);
		CHECK(now.dropMs >= 0);
		CHECK(now.dropMs < 300);
		// The last key press shares its tick with the drop queued in the same frame, and
		// the relay sequences one order per seat per tick, so it leaves a tick later.
		CHECK(now.keyMs >= 0);
		CHECK(now.keyMs < 350);
		CHECK(now.maxAhead <= 6);
		// The old behaviour, for the record: seconds of delay and a flood.
		CHECK(before.identical);
		CHECK(before.maxMove > 2000);
		CHECK(before.flooded > 0);
	}

	GLOB2_TEST_CASE("voice packets do not hold up a player's orders", "[network-sim][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		auto play = [](bool talk, std::size_t& voiceSent, std::uint64_t& voiceHeard) {
			EngineMatch m("FourSquares1", {{15 * MS}, {15 * MS}}, {"nicowar"});
			for (auto& c : m.clients)
				c->orderRate = 0.1;
			m.run(5 * SECOND);
			auto& c = *m.clients[0];
			c.trace.measuring = true;
			std::uint64_t nextVoice = m.net.now;
			voiceSent = 0;
			m.run(30 * SECOND, [&] {
				// A heavy talker: four packets a second (the recorder sends one about
				// every 1-2 s), each 600 bytes.
				if (!talk || m.net.now < nextVoice)
					return;
				nextVoice = m.net.now + 250 * MS;
				std::vector<Uint8> frames(600, 0x5a);
				RngScope scope(c.rng);
				c.engine->gui.orderQueue.push_back(std::make_shared<OrderVoiceData>(~0u, frames.size(), 30, frames.data()));
				++voiceSent;
			});
			c.trace.measuring = false;
			m.run(2 * SECOND);
			voiceHeard = m.clients[1]->session().telemetry().totals().voiceReceived;
			const auto b = c.trace.breakdown();
			const std::uint32_t end = m.finish();
			CHECK(m.requireIdenticalChecksums() == end + 1);
			return b;
		};
		std::size_t sent = 0, quietSent = 0;
		std::uint64_t heard = 0, quietHeard = 0;
		const auto quiet = play(false, quietSent, quietHeard);
		const auto talking = play(true, sent, heard);
		std::ostringstream table;
		table << "Bot orders (10% of ticks) on clean 15 ms links, 30 s, with and without four 600-byte voice packets a "
		         "second from the same player.\nrun | orders | mean ms | p95 ms | voice sent | voice received\n"
		      << "quiet | " << quiet.samples << " | " << quiet.total.mean << " | " << quiet.total.p95 << " | 0 | "
		      << quietHeard << "\n"
		      << "talking | " << talking.samples << " | " << talking.total.mean << " | " << talking.total.p95 << " | "
		      << sent << " | " << heard << "\n";
		std::ofstream(glob2test::artifactDir() / "turn-engine-voice.txt") << table.str();
		MESSAGE(table.str());
		CHECK(talking.samples > 50);
		CHECK(heard == sent);
		CHECK(talking.total.mean < quiet.total.mean + 20);
		CHECK(talking.total.p95 < 300);
	}

	GLOB2_TEST_CASE("a pause limit is enforced identically everywhere and the record verifies", "[network-sim][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		auto setup = makeSetup(mapPath("FourSquares1"), 2, {"nicowar"}, 4242);
		setup.pauseLimit = Online::PauseLimit{2, 4}; // per seat: two pauses, four seconds (100 ticks) in all
		EngineMatch m(setup, mapPath("FourSquares1"), {{15 * MS}, {40 * MS, 10 * MS}});
		for (auto& c : m.clients)
			c->orderRate = 0.04;
		std::vector<std::string> notices;
		for (auto& c : m.clients)
			c->engine->turnLockstep()->onPauseNotice = [&notices, seat = c->seat](Turn::PauseNotice n, int who) {
				notices.push_back(std::to_string(seat) + (n == Turn::PauseNotice::Refused ? " refused " : " expired ") +
				                  std::to_string(who));
			};
		auto pause = [&](int client, bool on) {
			auto& c = *m.clients[client];
			RngScope scope(c.rng);
			c.engine->gui.orderQueue.push_back(std::make_shared<PauseGameOrder>(on));
		};
		auto paused = [&](int client) -> bool { return m.clients[client]->engine->gui.gamePaused; };
		auto& lock0 = *m.clients[0]->engine->turnLockstep();

		m.run(4 * SECOND);
		// 1. Seat 0 pauses; seat 1 resumes it after about half a second. That costs seat
		// 0 one pause and the ticks it lasted, and seat 1 nothing.
		pause(0, true);
		m.run(500 * MS);
		CHECK(paused(0));
		CHECK(paused(1));
		pause(1, false);
		m.run(700 * MS);
		CHECK_FALSE(paused(0));
		CHECK_FALSE(paused(1));
		CHECK(lock0.pausesUsed(0) == 1);
		const std::uint32_t usedByFirst = lock0.pauseTicksUsed(0);
		CHECK(usedByFirst > 5);
		CHECK(usedByFirst < 40);
		CHECK(lock0.pauseTicksUsed(1) == 0);
		// 2. Seat 0 pauses again and never resumes: the game resumes by itself when its
		// 100 ticks are used up.
		pause(0, true);
		m.run(500 * MS);
		CHECK(paused(0));
		CHECK(paused(1));
		m.run(4 * SECOND);
		CHECK_FALSE(paused(0));
		CHECK_FALSE(paused(1));
		CHECK(lock0.pausesUsed(0) == 2);
		CHECK(lock0.pauseTicksUsed(0) == 100);
		// 3. A third pause of seat 0 is refused everywhere.
		pause(0, true);
		m.run(1 * SECOND);
		CHECK_FALSE(paused(0));
		CHECK_FALSE(paused(1));
		CHECK(lock0.orderAudit().seats[0].reasons[static_cast<std::size_t>(OrderValidation::Reason::PauseLimit)] == 1);
		// 4. Seat 1's own budget is untouched by seat 0's pauses.
		pause(1, true);
		m.run(1 * SECOND);
		CHECK(paused(0));
		m.run(3500 * MS);
		CHECK_FALSE(paused(0));
		CHECK(lock0.pauseTicksUsed(1) == 100);
		m.run(2 * SECOND);
		const std::uint32_t end = m.finish();
		CHECK(m.requireIdenticalChecksums() == end + 1);
		// The game skipped exactly the paused ticks, the same on both clients.
		const std::uint32_t pausedTicks = lock0.pauseTicksUsed(0) + lock0.pauseTicksUsed(1);
		for (auto& c : m.clients)
		{
			INFO("client " << c->index);
			CHECK(c->engine->turnLockstep()->pauseTicksUsed(0) == lock0.pauseTicksUsed(0));
			CHECK(c->engine->gui.game.stepCounter + pausedTicks == end);
			requireSameAudit(c->engine->turnLockstep()->orderAudit(), lock0.orderAudit());
		}
		std::sort(notices.begin(), notices.end());
		const std::vector<std::string> expected = {"0 expired 0", "0 expired 1", "0 refused 0",
		                                           "1 expired 0", "1 expired 1", "1 refused 0"};
		CHECK(notices == expected);

		const auto record = m.record("pause-limit");
		const auto directory = glob2test::artifactDir() / "pause-limit";
		const Verified v = verifyRecord(record, m, directory);
		CHECK(v.verdict.verdict == "verified");
		requireSameAudit(v.verdict.orders, lock0.orderAudit());
		CHECK(v.verdictJson.dump().find("pause_limit") != std::string::npos);
		for (auto& c : m.clients)
			requireSameOutcomes(v.result, liveTeams(*c));
		glob2test::writeFile(directory / "summary.txt", summary(m, end) + "paused ticks: seat 0 " +
		                                                    std::to_string(lock0.pauseTicksUsed(0)) + ", seat 1 " +
		                                                    std::to_string(lock0.pauseTicksUsed(1)) + "\n");
	}

	GLOB2_TEST_CASE("without a pause limit a pause lasts until someone resumes it", "[network-sim]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		EngineMatch m("FourSquares1", {{15 * MS}, {15 * MS}}, {"nicowar"});
		m.run(3 * SECOND);
		{
			auto& c = *m.clients[0];
			RngScope scope(c.rng);
			c.engine->gui.orderQueue.push_back(std::make_shared<PauseGameOrder>(true));
		}
		m.run(6 * SECOND);
		CHECK(m.clients[0]->engine->gui.gamePaused);
		CHECK(m.clients[1]->engine->gui.gamePaused);
		{
			auto& c = *m.clients[1];
			RngScope scope(c.rng);
			c.engine->gui.orderQueue.push_back(std::make_shared<PauseGameOrder>(false));
		}
		m.run(1 * SECOND);
		CHECK_FALSE(m.clients[0]->engine->gui.gamePaused);
		const std::uint32_t end = m.finish();
		CHECK(m.requireIdenticalChecksums() == end + 1);
	}

	GLOB2_TEST_CASE("one client claiming the game finished does not end it for a player who is reconnecting",
	                "[network-sim]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		EngineMatch m("FourSquares1", {{15 * MS}, {30 * MS}}, {"nicowar"});
		m.run(5 * SECOND);
		auto& liar = *m.clients[0];
		auto& other = *m.clients[1];
		m.net.outage(1, 8 * SECOND, *other.transport);
		m.run(1 * SECOND);
		CHECK(m.net.relay->presence(1) == Turn::PresenceState::Reconnecting);
		// A modified client leaves saying the game is decided while it is not.
		CHECK_FALSE(liar.engine->gui.game.isGameEnded);
		liar.session().quit(Turn::QuitReason::GameFinished);
		liar.stopped = true;
		m.run(500 * MS);
		CHECK(m.net.relay->presence(0) == Turn::PresenceState::Left);
		CHECK_FALSE(m.net.relay->matchOver());
		CHECK_FALSE(m.net.relay->gameDecided());
		// The other player comes back within its grace and plays on.
		m.run(10 * SECOND);
		CHECK(other.session().state() == Turn::TurnSession::State::Running);
		const std::uint32_t resumed = other.session().executedTick();
		m.run(5 * SECOND);
		CHECK(other.session().executedTick() > resumed + 100);
		CHECK_FALSE(m.net.relay->matchOver());
		const std::uint32_t end = m.finish();
		m.clients.erase(m.clients.begin());
		CHECK(m.requireIdenticalChecksums() == end + 1);
		CHECK_FALSE(m.net.relay->gameDecided());
		const Verified v = verifyRecord(m.record("lying-finish"), m, glob2test::artifactDir() / "lying-finish");
		CHECK(v.verdict.verdict == "verified");
	}

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

	GLOB2_TEST_CASE("engines report network telemetry, and its output changes nothing they execute",
	                "[network-sim][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		struct Run
		{
			std::vector<std::map<std::uint32_t, Uint32>> checksums;
			std::vector<json> summaries;
			json relay;
			std::string output;
		};
		auto play = [](bool output) {
			if (output)
				glob2test::setEnv("GLOB2_TEAM_TIMELINE", "1");
			Run run;
			std::ostringstream captured;
			std::streambuf* previous = output ? std::cout.rdbuf(captured.rdbuf()) : nullptr;
			{
				EngineMatch m("FourSquares1", {{20 * MS}, {70 * MS, 50 * MS, 0.03}}, {"nicowar"});
				m.run(20 * SECOND);
				m.net.outage(1, 3 * SECOND, *m.clients[1]->transport);
				m.run(15 * SECOND);
				m.finish();
				m.requireIdenticalChecksums();
				for (auto& c : m.clients)
				{
					run.checksums.push_back(c->lives.back());
					run.summaries.push_back(c->engine->turnNetworkSummary());
					RngScope scope(c->rng);
					c->engine->abortSession(); // tears down: final telemetry records
				}
				run.relay = m.net.relay->networkSummary();
			}
			if (previous)
				std::cout.rdbuf(previous);
			if (output)
				glob2test::unsetEnv("GLOB2_TEAM_TIMELINE");
			run.output = captured.str();
			return run;
		};
		const Run quiet = play(false);
		const Run loud = play(true);
		// Telemetry output on or off: every client executes exactly the same game.
		REQUIRE(quiet.checksums.size() == 2);
		CHECK(quiet.checksums == loud.checksums);
		CHECK(quiet.checksums[0].size() > 800);

		for (std::size_t i = 0; i < loud.summaries.size(); ++i)
		{
			const json& s = loud.summaries[i];
			INFO("client " << i << ": " << s.dump());
			REQUIRE(s.is_object());
			CHECK(s.at("schema") == "ClientNetworkSummary");
			CHECK(s.at("match").at("seat") == i);
			CHECK(s.at("match").at("transport") == "online");
			CHECK(s.at("match").at("sim_version") == Online::currentSimVersion().key());
			CHECK(s.at("rtt_us").at("count").get<int>() > 20);
			CHECK(s.at("input_delay_us").at("count").get<int>() > 5);
			CHECK(s.at("traffic").at("bundles_received").get<int>() > 300);
			CHECK(s.at("series").at("points").size() >= 6);
		}
		const json& c0 = loud.summaries[0];
		const json& c1 = loud.summaries[1];
		CHECK(c0.at("rtt_us").at("p50").get<std::uint64_t>() >= 40 * MS);
		CHECK(c1.at("rtt_us").at("p50") > c0.at("rtt_us").at("p50"));
		CHECK(c1.at("jitter_buffer").at("target_ticks").at("p95") > c0.at("jitter_buffer").at("target_ticks").at("p95"));
		CHECK(c1.at("reconnects").at("count").get<int>() >= 1);
		CHECK(c1.at("stalls").at("longest_us").get<std::uint64_t>() >= 2 * SECOND);
		CHECK(c1.at("catch_up").at("episodes").get<int>() >= 1);
		CHECK(c0.at("reconnects").at("count") == 0);
		CHECK(loud.relay.at("seats")[1].at("connection").at("disconnects").get<int>() >= 1);
		// The deterministic order check: every human seat, the same verdicts on every
		// client, and an unmodified client has nothing refused.
		const json& checks = c0.at("order_validation");
		REQUIRE(checks.is_object());
		REQUIRE(checks.at("seats").size() == 2);
		CHECK(checks == c1.at("order_validation"));
		for (const json& seat : checks.at("seats"))
		{
			CHECK(seat.at("rejected") == 0);
			CHECK(seat.at("rejected_by_reason").empty());
		}
		CHECK(checks.at("seats")[0].at("accepted").get<int>() + checks.at("seats")[1].at("accepted").get<int>() > 0);

		// The standard telemetry stream carries the same data.
		for (const char* record : {"GLOB2_NET_SESSION ", "GLOB2_NET_SAMPLE ", "GLOB2_NET_FINAL ", "GLOB2_NET_SEAT ",
		                           "GLOB2_NET_SUMMARY {"})
			CHECK_MESSAGE(loud.output.find(record) != std::string::npos, record);
		CHECK(quiet.output.empty());

		const auto directory = glob2test::artifactDir() / "network-telemetry";
		fs::create_directories(directory);
		glob2test::writeFile(directory / "client0.network.json", c0.dump(1));
		glob2test::writeFile(directory / "client1.network.json", c1.dump(1));
		glob2test::writeFile(directory / "relay.network.json", loud.relay.dump(1));
		glob2test::writeFile(directory / "timeline-records.txt", loud.output);
	}
}

