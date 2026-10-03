// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// LAN games on the turn protocol, end to end in one process: a LanRoom host (room and
// in-process relay) and LanRoom guests talk over real loopback WSS connections, exactly
// as two desktop instances do. The guests join the room, download the map by content
// hash, set their team and readiness, and the host starts. Each player then runs a real
// Engine started through RoomBackend::initGame, with AI seats and a bot clicking for
// the human; the cases check that every engine agrees on the checksum before every
// tick, through a dropped connection, a guest that restarts and rejoins by name, and
// the host leaving. A benchmark case measures input delay on loopback and on an
// emulated slower link (artifacts/tests/lan-input-delay.txt).
//
// The engines share one process and one GlobalContainer. Each keeps its own
// synchronized RNG stream, swapped in while it runs, as TurnEngineHarness does.

#include "EngineFixtures.h"
#include "Environment.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <thread>

#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "AINames.h"
#include "Engine.h"
#include "EngineTiming.h"
#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "LanRoom.h"
#include "MapCache.h"
#include "MatchRecord.h"
#include "MatchSetup.h"
#include "MersenneTwister.h"
#include "Order.h"
#include "Player.h"
#include "Team.h"
#include "TurnLatencyTrace.h"
#include "TurnLockstep.h"
#include "Utilities.h"
#include "VerifyMatch.h"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
struct RngScope
{
	MersenneTwister& own;
	explicit RngScope(MersenneTwister& stream) : own(stream) { std::swap(syncRandEngine(), own); }
	~RngScope() { std::swap(syncRandEngine(), own); }
};

std::uint64_t wallMs()
{
	return Lan::nowMicros() / 1000;
}

std::string mapPath()
{
	return (glob2test::sourceRoot() / "maps" / "FourSquares1.map.gz").string();
}

std::uint16_t testPort(int offset)
{
#ifndef _WIN32
	const int pid = static_cast<int>(getpid());
#else
	const int pid = static_cast<int>(std::random_device()());
#endif
	return static_cast<std::uint16_t>(21000 + (pid % 9000) * 4 + offset);
}

/// A link with a fixed one-way delay in each direction, around a guest's turn transport.
class DelayTransport final : public Turn::TurnTransport
{
public:
	DelayTransport(std::shared_ptr<Turn::TurnTransport> inner, std::uint64_t oneWayMicros)
		: inner(std::move(inner)), delay(oneWayMicros)
	{
	}
	State state() override
	{
		pumpOut();
		return inner->state();
	}
	void connect() override
	{
		out.clear();
		in.clear();
		inner->connect();
	}
	void close() override { inner->close(); }
	bool send(const std::vector<std::uint8_t>& payload) override
	{
		out.emplace_back(Lan::nowMicros() + delay, payload);
		pumpOut();
		return true;
	}
	bool receive(std::vector<std::uint8_t>& payload) override
	{
		pump();
		if (in.empty() || in.front().first > Lan::nowMicros())
			return false;
		payload = std::move(in.front().second);
		in.pop_front();
		return true;
	}
	void flush() override
	{
		pumpOut();
		inner->flush();
	}
	/// Moves frames along between the engine's frames, so the emulated delay is the
	/// link's alone and not rounded up to the next engine frame at either end.
	void pump()
	{
		pumpOut();
		std::vector<std::uint8_t> frame;
		while (inner->receive(frame))
			in.emplace_back(Lan::nowMicros() + delay, std::move(frame));
	}

private:
	void pumpOut()
	{
		const std::uint64_t now = Lan::nowMicros();
		while (!out.empty() && out.front().first <= now)
		{
			inner->send(out.front().second);
			out.pop_front();
		}
	}
	std::shared_ptr<Turn::TurnTransport> inner;
	std::uint64_t delay;
	std::deque<std::pair<std::uint64_t, std::vector<std::uint8_t>>> out, in;
};

/// Every emulated link in the process, pumped by LanMatch::step.
std::vector<std::weak_ptr<DelayTransport>>& delayLinks()
{
	static std::vector<std::weak_ptr<DelayTransport>> links;
	return links;
}

/// One player: a LanRoom and, once the host starts, an Engine.
struct LanPlayer
{
	std::string name;
	std::shared_ptr<Lan::LanRoom> room;
	std::unique_ptr<Engine> engine;
	MersenneTwister rng;
	std::mt19937 bot;
	std::uint64_t wakeAt = 0;
	bool launched = false;
	bool stopped = false;
	std::optional<RoomBackend::Event> finished;
	int seat = -1;
	double orderRate = 0.05;
	bool measure = false;
	int ordersQueued = 0;
	std::deque<std::uint64_t> queuedAt;
	std::vector<double> delaysMs;
	std::vector<std::map<std::uint32_t, Uint32>> lives;
	bool sawReconnecting = false;
	/// Clicks at random moments between engine frames, as a person does, instead of
	/// right after a tick.
	bool randomClicks = false;
	std::uint64_t nextClickAt = 0;
	turntest::LatencyTrace trace;

	LanPlayer(std::string name, std::shared_ptr<Lan::LanRoom> room, unsigned seed)
		: name(std::move(name)), room(std::move(room)), bot(seed)
	{
	}
	~LanPlayer()
	{
		RngScope scope(rng);
		engine.reset();
	}

	void pumpRoom()
	{
		if (!room)
			return;
		room->update();
		while (auto event = room->takeEvent())
		{
			if (event->kind == RoomBackend::Event::Launch)
				launched = true;
			else if (event->kind == RoomBackend::Event::Finished)
				finished = event;
		}
	}

	Turn::TurnSession& session() { return engine->turnLockstep()->turn(); }

	void startEngine()
	{
		RngScope scope(rng);
		engine = std::make_unique<Engine>();
		REQUIRE(room->initGame(*engine).run());
		room->gameStarted(true);
		auto* lockstep = engine->turnLockstep();
		REQUIRE(lockstep);
		seat = engine->gui.localPlayer;
		lives.emplace_back();
		lockstep->onChecksum = [this](std::uint32_t tick, Uint32 checksum) {
			if (tick == 0 && !lives.back().empty())
				lives.emplace_back();
			lives.back()[tick] = checksum;
		};
		lockstep->orderFilter = [this](std::uint32_t tick, int player, std::shared_ptr<Order> order) {
			if (player == engine->gui.localPlayer)
				trace.executed(tick, Lan::nowMicros());
			if (player == engine->gui.localPlayer && order && order->getOrderType() != ORDER_NULL && !queuedAt.empty())
			{
				if (measure)
					delaysMs.push_back((Lan::nowMicros() - queuedAt.front()) / 1000.0);
				queuedAt.pop_front();
			}
			return order;
		};
		lockstep->turn().onSubmitted = [this](std::uint32_t sequence) {
			trace.submitted(sequence, Lan::nowMicros(), session().executedTick());
		};
		lockstep->turn().onHorizon = [this](std::uint32_t horizon) { trace.received(horizon, Lan::nowMicros()); };
		engine->beginSession(wallMs());
		wakeAt = 0;
	}

	void frame()
	{
		if (!engine || stopped)
			return;
		RngScope scope(rng);
		const std::uint64_t now = wallMs();
		// As GameSessionScreen: between steps, read the relay connection every few ms.
		if (now < wakeAt && now >= lastPoll + TURN_POLL_MS)
		{
			engine->pollTurnSession(now);
			lastPoll = now;
		}
		for (int budget = 4000; budget > 0 && now >= wakeAt; --budget)
		{
			const bool catching = session().catchingUp();
			const std::uint32_t before = session().executedTick();
			const bool running = engine->stepSession(now);
			if (session().state() == Turn::TurnSession::State::Reconnecting)
				sawReconnecting = true;
			const std::uint32_t after = session().executedTick();
			if (!randomClicks && after > before && !catching && orderRate > 0 &&
			    std::uniform_real_distribution<double>(0, 1)(bot) < orderRate)
				queueBotOrder();
			if (!running)
			{
				stopped = true;
				engine->finishSessionForHost();
				room->gameEnded(false);
				break;
			}
			wakeAt = now + engine->sessionDelay(now);
			lastPoll = now;
		}
	}
	std::uint64_t lastPoll = 0;

	void queueBotOrder()
	{
		Game& game = engine->gui.game;
		Team* team = game.teams[engine->gui.localTeamNo];
		if (!team || !team->isAlive)
			return;
		std::uniform_int_distribution<int> offset(-10, 10);
		const int x = team->startPosX + offset(bot), y = team->startPosY + offset(bot);
		std::shared_ptr<Order> order;
		if (bot() % 2)
		{
			static const char* flags[] = {"explorationflag", "warflag", "clearingflag"};
			const int type = globalContainer->buildingsTypes.getTypeNum(flags[bot() % 3], 0, false);
			order = std::make_shared<OrderCreate>(team->teamNumber, x, y, type, 1 + bot() % 6, 1 + bot() % 6);
		}
		else
		{
			static const char* sites[] = {"inn", "swarm", "hospital", "racetrack"};
			const int type = globalContainer->buildingsTypes.getTypeNum(sites[bot() % 4], 0, true);
			order = std::make_shared<OrderCreate>(team->teamNumber, x, y, type, 1 + bot() % 5, 1 + bot() % 5);
		}
		engine->gui.orderQueue.push_back(order);
		queuedAt.push_back(Lan::nowMicros());
		trace.queued(queuedAt.back());
		++ordersQueued;
	}

	/// A random click between frames, at orderRate clicks per tick on average.
	void maybeClick()
	{
		if (!randomClicks || !engine || stopped || orderRate <= 0 || session().catchingUp())
			return;
		const std::uint64_t now = Lan::nowMicros();
		if (nextClickAt && now >= nextClickAt)
		{
			RngScope scope(rng);
			queueBotOrder();
		}
		if (!nextClickAt || now >= nextClickAt)
		{
			const double meanMicros = 40000.0 / orderRate;
			nextClickAt = now + static_cast<std::uint64_t>(std::exponential_distribution<double>(1.0 / meanMicros)(bot));
		}
	}
};

struct LanMatch
{
	std::vector<std::unique_ptr<LanPlayer>> players; ///< [0] is the host
	fs::path directory;

	LanPlayer& host() { return *players.front(); }
	Lan::LanHost& hostSide() { return *host().room->hostSide(); }

	void step()
	{
		hookRelay();
		for (auto& p : players)
		{
			p->pumpRoom();
			p->maybeClick();
			p->frame();
		}
		for (auto& weak : delayLinks())
			if (auto link = weak.lock())
				link->pump();
		std::this_thread::sleep_for(std::chrono::microseconds(500));
	}

	~LanMatch()
	{
		if (!relayHooked || players.empty() || !host().room || !host().room->hostSide())
			return;
		auto& side = hostSide();
		std::lock_guard<std::recursive_mutex> guard(side.mutex);
		if (side.relay)
		{
			side.relay->onSequenced = nullptr;
			side.relay->onEmitted = nullptr;
		}
	}

	/// Feeds the in-process relay's probes into each player's trace.
	bool relayHooked = false;
	void hookRelay()
	{
		if (relayHooked || players.empty() || !host().room->hostSide())
			return;
		auto& side = hostSide();
		std::lock_guard<std::recursive_mutex> guard(side.mutex);
		if (!side.relay)
			return;
		relayHooked = true;
		side.relay->onSequenced = [this](std::uint8_t seat, std::uint32_t sequence, std::uint32_t tick, std::uint32_t relayTick) {
			for (auto& p : players)
				if (p->seat == seat)
					p->trace.sequenced(sequence, tick, relayTick, Lan::nowMicros());
		};
		side.relay->onEmitted = [this](std::uint32_t, std::uint32_t horizon) {
			for (auto& p : players)
				p->trace.emitted(horizon, Lan::nowMicros());
		};
	}
	void runFor(std::uint64_t ms, const std::function<void()>& each = {})
	{
		const std::uint64_t end = wallMs() + ms;
		while (wallMs() < end)
		{
			step();
			if (each)
				each();
		}
	}
	bool runUntil(std::uint64_t timeoutMs, const std::function<bool()>& done)
	{
		const std::uint64_t end = wallMs() + timeoutMs;
		while (wallMs() < end)
		{
			if (done())
				return true;
			step();
		}
		return done();
	}
};

std::shared_ptr<Lan::LanRoom> hostRoom(std::uint16_t port, const fs::path& directory, std::uint8_t bundleInterval = 0)
{
	Lan::LanHost::Options options;
	if (bundleInterval)
		options.sequencer.bundleInterval = bundleInterval;
	options.hostName = "Host";
	options.map = Engine::loadMapHeader(mapPath());
	options.mapFile = mapPath();
	options.port = port;
	options.broadcast = false;
	options.recordPath = (directory / "match.g2mr").string();
	return Lan::LanRoom::host(std::move(options));
}

std::shared_ptr<Lan::LanRoom> guestRoom(const std::string& endpoint, const std::string& name, const fs::path& cache,
                                        std::uint64_t oneWayMicros = 0)
{
	Lan::LanClient::Options options;
	options.endpoint = endpoint;
	options.name = name;
	options.cacheDirectory = cache.string();
	if (oneWayMicros)
		options.wrapTransport = [oneWayMicros](std::shared_ptr<Turn::TurnTransport> inner) {
			auto link = std::make_shared<DelayTransport>(std::move(inner), oneWayMicros);
			delayLinks().push_back(link);
			return link;
		};
	return Lan::LanRoom::join(std::move(options));
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

/// Joins the guests, fills the free seats with AIs, readies everyone and starts.
void joinAndStart(LanMatch& m, const std::vector<std::string>& ais)
{
	auto& host = m.hostSide();
	const std::size_t humans = m.players.size();
	REQUIRE(m.runUntil(15000, [&] {
		if (host.state().members.size() != humans)
			return false;
		for (const auto& member : host.state().members)
			if (!member.hasMap)
				return false;
		return true;
	}));
	for (const auto& ai : ais)
		m.host().room->addAI(static_cast<AI::ImplementationID>(AINames::parseAIName(ai)));
	for (std::size_t i = 1; i < m.players.size(); ++i)
		m.players[i]->room->setReady(true);
	REQUIRE(m.runUntil(5000, [&] { return m.host().room->canStart(); }));
	m.host().room->start();
	REQUIRE(m.runUntil(5000, [&] {
		return std::all_of(m.players.begin(), m.players.end(), [](const auto& p) { return p->launched; });
	}));
	for (auto& p : m.players)
		p->startEngine();
}

/// Every run of every player agrees with every other at every tick it executed, and
/// with the relay's agreed checksums. Returns the number of ticks compared.
std::size_t requireIdenticalChecksums(const std::vector<const LanPlayer*>& players, Lan::LanHost* relay)
{
	std::map<std::uint32_t, Uint32> all;
	for (const auto* p : players)
		for (const auto& life : p->lives)
		{
			INFO(p->name);
			for (const auto& [tick, checksum] : life)
			{
				auto [it, inserted] = all.emplace(tick, checksum);
				if (!inserted && it->second != checksum)
					FAIL_CHECK("tick " << tick << ": " << std::hex << checksum << " != " << it->second);
			}
		}
	if (relay)
		for (const auto& [tick, checksum] : all)
			if (auto agreed = relay->agreedChecksum(tick))
				CHECK(*agreed == checksum);
	return all.size();
}

struct DelayStats
{
	std::size_t samples = 0;
	double mean = 0, p50 = 0, p95 = 0;
};

DelayStats stats(std::vector<double> values)
{
	DelayStats s;
	s.samples = values.size();
	if (values.empty())
		return s;
	std::sort(values.begin(), values.end());
	for (double v : values)
		s.mean += v;
	s.mean /= values.size();
	s.p50 = values[values.size() / 2];
	s.p95 = values[std::min(values.size() - 1, values.size() * 95 / 100)];
	return s;
}
}

TEST_SUITE("LanMatchHarness")
{
	GLOB2_TEST_CASE("a LAN host and two guests over loopback WSS play with AIs through a drop, a restart and the host "
	                "leaving, and agree at every tick",
	                "[network][slow][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		REQUIRE(GAGCore::setProcessEnvironment("GLOB2_LAN_ADDRESS", "127.0.0.1", 1) == 0);
		REQUIRE(std::getenv("GLOB2_LAN_ADDRESS") != nullptr);
		REQUIRE(std::string(std::getenv("GLOB2_LAN_ADDRESS")) == "127.0.0.1");
		LanMatch m;
		m.directory = glob2test::artifactDir() / "lan-match";
		fs::remove_all(m.directory);
		fs::create_directories(m.directory);
		const std::uint16_t port = testPort(0);
		m.players.push_back(std::make_unique<LanPlayer>("Host", hostRoom(port, m.directory), 101));
		const std::string endpoint = m.host().room->shareText();
		INFO("LAN pairing endpoint=" << endpoint);
		REQUIRE(endpoint.find("127.0.0.1:" + std::to_string(port) + "/yog#sha256=") != std::string::npos);
		const fs::path cacheA = m.directory / "cache-a", cacheB = m.directory / "cache-b";
		auto& host = m.hostSide();
		// The room: both guests join (A first, so it takes seat 1) and download the map
		// by content hash.
		m.players.push_back(std::make_unique<LanPlayer>("Guest A", guestRoom(endpoint, "Guest A", cacheA), 102));
		REQUIRE(m.runUntil(15000, [&] { return host.state().members.size() == 2; }));
		m.players.push_back(std::make_unique<LanPlayer>("Guest B", guestRoom(endpoint, "Guest B", cacheB), 103));
		REQUIRE(m.runUntil(15000, [&] { return host.state().members.size() == 3; }));
		REQUIRE(m.runUntil(15000, [&] {
			return m.players[1]->room->guestSide()->hasMap() && m.players[2]->room->guestSide()->hasMap();
		}));
		std::string source, copyA, copyB;
		REQUIRE(Online::readMapBytes(mapPath(), source));
		REQUIRE(Online::readMapBytes(m.players[1]->room->guestSide()->mapFile(), copyA));
		REQUIRE(Online::readMapBytes(m.players[2]->room->guestSide()->mapFile(), copyB));
		CHECK(copyA == source);
		CHECK(copyB == source);
		CHECK(fs::path(m.players[1]->room->guestSide()->mapFile()).parent_path() == cacheA / "online" / "maps");

		// A guest picks its own team; the slot list shows it to everyone.
		m.players[2]->room->changeTeam(2, 3);
		REQUIRE(m.runUntil(5000, [&] { return host.state().setup.seats.at(2).team == 3; }));
		REQUIRE(m.runUntil(5000, [&] {
			const auto slots = m.players[1]->room->slots();
			return slots.size() == 4 && slots[2].team == 3 && slots[2].name == "Guest B" && slots[3].open;
		}));
		CHECK_FALSE(m.host().room->canStart()); // guests are not ready yet
		joinAndStart(m, {"nicowar"});
		const Online::MatchSetup setup = host.state().setup;
		CHECK(setup.seats.size() == 4);
		CHECK(setup.seats[3].ai == "nicowar");
		CHECK(setup.seats[3].team == 2); // the free team
		CHECK(setup.humanSeatMask() == 0b0111);
		for (std::size_t i = 0; i < m.players.size(); ++i)
			CHECK(m.players[i]->seat == static_cast<int>(i));

		// Play; then guest B's connection drops and it reconnects by itself.
		m.runFor(10000);
		REQUIRE(host.relayRunning());
		const std::uint32_t beforeDrop = m.players[2]->session().executedTick();
		CHECK(beforeDrop > 150);
		bool hostSawReconnecting = false;
		m.players[2]->room->guestSide()->dropConnection();
		const auto& strings = *GAGCore::Toolkit::getStringTable();
		const std::string guestReconnecting =
			GAGCore::FormattableString(strings.getString("[turn player reconnecting %0]")).arg("Guest B");
		const std::string hostLost = strings.getString("[turn reconnecting]");
		bool hostNoticeShown = false, guestNoticeShown = false;
		auto shows = [](const std::vector<std::string>& lines, const std::string& line) {
			return std::find(lines.begin(), lines.end(), line) != lines.end();
		};
		m.runFor(6000, [&] {
			hostSawReconnecting |= host.presence(2) == Turn::PresenceState::Reconnecting;
			// The in-game notice: the host lists the reconnecting guest, the guest
			// shows its own lost connection.
			hostNoticeShown |= shows(m.host().engine->gui.connectionNotice(), guestReconnecting);
			guestNoticeShown |= shows(m.players[2]->engine->gui.connectionNotice(), hostLost);
		});
		CHECK(hostNoticeShown);
		CHECK(guestNoticeShown);
		CHECK(m.players[2]->sawReconnecting);
		CHECK(hostSawReconnecting);
		CHECK(host.presence(2) == Turn::PresenceState::Connected);
		CHECK(m.players[2]->session().state() == Turn::TurnSession::State::Running);
		CHECK(m.players[2]->session().executedTick() > beforeDrop + 100);

		// Guest A's game restarts (a crash): it rejoins the running match by name and
		// fast-forwards from tick 0.
		auto& crashed = *m.players[1];
		crashed.room->guestSide()->dropConnection();
		{
			RngScope scope(crashed.rng);
			crashed.engine.reset();
		}
		crashed.room.reset();
		crashed.stopped = true;
		m.runFor(1000);
		CHECK(host.presence(1) == Turn::PresenceState::Reconnecting);
		m.players.push_back(std::make_unique<LanPlayer>("Guest A (rejoined)", guestRoom(endpoint, "Guest A", cacheA), 104));
		auto& rejoined = *m.players.back();
		REQUIRE(m.runUntil(10000, [&] { return rejoined.launched; }));
		rejoined.startEngine();
		CHECK(rejoined.seat == 1);
		const std::uint32_t behind = host.horizon();
		REQUIRE(m.runUntil(20000, [&] {
			return rejoined.session().executedTick() >= behind && !rejoined.session().catchingUp();
		}));
		CHECK(m.runUntil(5000, [&] { return host.presence(1) == Turn::PresenceState::Connected; }));
		m.runFor(8000);

		// The host leaves: the guests' games end with a message, and the record verifies.
		for (auto& p : m.players)
			p->orderRate = 0;
		m.runFor(1000);
		m.host().engine->gui.isRunning = false;
		REQUIRE(m.runUntil(10000, [&] {
			return m.host().stopped && m.players[2]->stopped && rejoined.stopped && m.players[2]->finished &&
			       rejoined.finished;
		}));
		const std::string hostLeft = GAGCore::Toolkit::getStringTable()->getString("[lan host left]");
		CHECK(m.players[2]->finished->text == hostLeft);
		CHECK(rejoined.finished->text == hostLeft);
		CHECK(m.players[2]->finished->code == RoomBackend::GameCancelled);
		REQUIRE(m.host().finished);
		CHECK(m.host().finished->code == RoomBackend::StartedGame);

		std::vector<const LanPlayer*> all;
		for (auto& p : m.players)
			all.push_back(p.get());
		const std::size_t compared = requireIdenticalChecksums(all, &host);
		CHECK(compared > 600);
		CHECK(rejoined.lives.size() == 1);
		CHECK(rejoined.lives.front().count(0) == 1); // replayed from the start

		const auto record = Turn::MatchRecord::readFile((m.directory / "match.g2mr").string());
		CHECK(record.turns.size() > 20);
		const auto recorded = Online::MatchSetup::parse(record.setupJson);
		CHECK(recorded.seed == setup.seed);
		fs::create_directories(m.directory / "verify");
		const auto verdict = MatchVerifier::verify(record, recorded, mapPath(), m.directory / "verify");
		CHECK(verdict.verdict == "verified");
		// Next to the record: the relay's per-seat network summary, which saw the drop.
		const auto network = nlohmann::json::parse(glob2test::readFile(m.directory / "match.network.json"));
		CHECK(network.at("schema") == "RelayNetworkSummary");
		CHECK(network.at("end_tick") == record.endTick);
		std::uint64_t disconnects = 0, sequenced = 0;
		for (const auto& seat : network.at("seats"))
		{
			disconnects += seat.at("connection").at("disconnects").get<std::uint64_t>();
			sequenced += seat.at("orders").at("sequenced").get<std::uint64_t>();
		}
		CHECK(network.at("seats").size() == 3);
		CHECK(disconnects >= 1);
		CHECK(sequenced == record.turns.size());

		std::ostringstream out;
		out << "ticks compared " << compared << ", record turns " << record.turns.size() << ", end tick "
		    << record.endTick << ", verdict " << verdict.verdict << "\n";
		for (auto& p : m.players)
			out << p->name << ": seat " << p->seat << ", lives " << p->lives.size() << ", orders "
			    << p->ordersQueued << "\n";
		glob2test::writeFile(m.directory / "summary.txt", out.str());
		MESSAGE(out.str());
		for (auto& p : m.players)
		{
			RngScope scope(p->rng);
			p->engine.reset();
		}
	}

	// A short name: the case name is part of the artifact path, and the guest's map
	// cache below it must stay under Windows' 260-character path limit.
	GLOB2_TEST_CASE("an idle LAN host sleeps and wakes at once for guests and the host", "[network][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		GAGCore::setProcessEnvironment("GLOB2_LAN_ADDRESS", "127.0.0.1", 1);
		LanMatch m;
		m.directory = glob2test::artifactDir() / "wait";
		fs::remove_all(m.directory);
		fs::create_directories(m.directory);
		m.players.push_back(std::make_unique<LanPlayer>("Host", hostRoom(testPort(3), m.directory), 301));
		const std::string endpoint = m.host().room->shareText();
		m.players.push_back(std::make_unique<LanPlayer>("Guest", guestRoom(endpoint, "Guest", m.directory / "cache"), 302));
		auto& host = m.hostSide();
		REQUIRE(host.threaded());
		auto* guest = m.players[1]->room->guestSide();
		REQUIRE(m.runUntil(15000, [&] { return host.state().members.size() == 2 && guest->hasMap(); }));

		// Idle room: the worker no longer wakes every millisecond (1000 per second),
		// only for its idle bound and the guest's keep-alives.
		m.runFor(300);
		const std::uint64_t before = host.workerWakeups();
		m.runFor(1000);
		const std::uint64_t idle = host.workerWakeups() - before;
		MESSAGE("LAN host worker wake-ups in one idle second: " << idle);
#ifndef _WIN32
		// Windows sockets complete through completion ports, which a readiness poll
		// cannot see: there the worker keeps polling every millisecond (NetWait.h).
		CHECK(idle < 60);
#endif

		// A guest's message wakes the host at once, and so does the host's own action
		// made on another thread (its change reaches the guest). Best of three, so a
		// scheduling hiccup on a loaded machine does not decide it.
		using Clock = std::chrono::steady_clock;
		const auto elapsedMs = [](Clock::time_point since) {
			return static_cast<long long>(
				std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count());
		};
		const int guestSeat = host.state().members.at(1).seat;
		REQUIRE(guestSeat > 0);
		long long guestToHost = 1 << 30, hostToGuest = 1 << 30;
		const AI::ImplementationID castor = static_cast<AI::ImplementationID>(AINames::parseAIName("castor"));
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			const int team = host.state().setup.seats.at(guestSeat).team == 1 ? 2 : 1;
			auto started = Clock::now();
			m.players[1]->room->changeTeam(guestSeat, team);
			REQUIRE(m.runUntil(5000, [&] { return host.state().setup.seats.at(guestSeat).team == team; }));
			guestToHost = std::min(guestToHost, elapsedMs(started));

			const std::size_t seats = guest->state()->setup.seats.size();
			started = Clock::now();
			host.addAI(castor);
			REQUIRE(m.runUntil(5000, [&] {
				return guest->state()->setup.seats.size() == seats + 1 && !guest->state()->setup.seats.back().human;
			}));
			hostToGuest = std::min(hostToGuest, elapsedMs(started));
			started = Clock::now();
			host.kickSeat(static_cast<int>(seats));
			REQUIRE(m.runUntil(5000, [&] { return guest->state()->setup.seats.size() == seats; }));
			hostToGuest = std::min(hostToGuest, elapsedMs(started));
		}
		MESSAGE("guest to host: " << guestToHost << " ms, host to guest: " << hostToGuest << " ms (best of three)");
		// Far below the 100 ms idle bound: they did not wait for a timer.
		CHECK(guestToHost < 50);
		CHECK(hostToGuest < 50);
	}

	GLOB2_TEST_CASE("LAN input delay on loopback and on emulated slower links", "[network][slow][benchmark][artifacts]")
	{
		glob2test::HeadlessGlobals globals(harnessGlobals());
		REQUIRE(GAGCore::setProcessEnvironment("GLOB2_LAN_ADDRESS", "127.0.0.1", 1) == 0);
		REQUIRE(std::getenv("GLOB2_LAN_ADDRESS") != nullptr);
		REQUIRE(std::string(std::getenv("GLOB2_LAN_ADDRESS")) == "127.0.0.1");
		std::ostringstream table, stages;
		table << "LAN input delay: time from a click (an order queued by the GUI at a random moment between "
		         "frames) to its execution, host and one guest, FourSquares1 with one Nicowar AI, 25 s of play each.\n";
		table << "bundle interval | one-way delay | player | samples | mean ms | median ms | p95 ms | buffer target ticks | "
		         "rtt ms | stalls | long stalls | stalled ms\n";
		stages << "Per-stage breakdown of the same runs (" << turntest::LatencyTrace::header() << ")\n";
		int offset = 1;
		const char* only = std::getenv("GLOB2_LAN_DELAY_BUNDLE");
		for (std::uint8_t bundle : {std::uint8_t(2), std::uint8_t(1)})
		for (std::uint64_t oneWayMs : {0u, 25u, 50u})
		{
			if (only && std::atoi(only) != bundle)
				continue;
			LanMatch m;
			m.directory = glob2test::artifactDir() / ("lan-delay-" + std::to_string(bundle) + "-" + std::to_string(oneWayMs));
			fs::remove_all(m.directory);
			fs::create_directories(m.directory);
			m.players.push_back(std::make_unique<LanPlayer>("Host", hostRoom(testPort(offset++), m.directory, bundle), 201));
			const std::string endpoint = m.host().room->shareText();
			INFO("LAN pairing endpoint=" << endpoint);
			m.players.push_back(std::make_unique<LanPlayer>(
				"Guest", guestRoom(endpoint, "Guest", m.directory / "cache", oneWayMs * 1000), 202));
			joinAndStart(m, {"nicowar"});
			for (auto& p : m.players)
			{
				p->orderRate = 0.1;
				p->randomClicks = true;
			}
			m.runFor(5000); // let the delay controller settle
			std::vector<Turn::TurnSession::StallStats> before;
			for (auto& p : m.players)
			{
				p->measure = true;
				p->trace.measuring = true;
				before.push_back(p->session().stallStats());
			}
			const std::uint64_t wakeupsBefore = m.hostSide().workerWakeups();
			m.runFor(25000);
			const double wakeupsPerSecond = (m.hostSide().workerWakeups() - wakeupsBefore) / 25.0;
			std::vector<const LanPlayer*> all;
			for (auto& p : m.players)
				all.push_back(p.get());
			CHECK(requireIdenticalChecksums(all, &m.hostSide()) > 500);
			for (std::size_t i = 0; i < m.players.size(); ++i)
			{
				auto& p = m.players[i];
				const DelayStats s = stats(p->delaysMs);
				CHECK(s.samples > 20);
				const auto& now = p->session().stallStats();
				table << int(bundle) << " | " << oneWayMs << " ms | " << p->name << " | " << s.samples << " | " << s.mean << " | " << s.p50
				      << " | " << s.p95 << " | " << p->session().targetTicks() << " | "
				      << p->session().rttMicros() / 1000 << " | " << now.stalls - before[i].stalls << " | "
				      << now.longStalls - before[i].longStalls << " | "
				      << (now.stalledMicros - before[i].stalledMicros) / 1000 << "\n";
				stages << int(bundle) << " | " << oneWayMs << " ms | " << p->name << " | "
				       << turntest::LatencyTrace::row(p->trace.breakdown()) << "\n";
			}
			table << int(bundle) << " | " << oneWayMs << " ms | host relay thread wake-ups per second: " << wakeupsPerSecond
			      << "\n";
			for (auto& p : m.players)
			{
				RngScope scope(p->rng);
				p->engine.reset();
			}
			delayLinks().clear();
		}
		table << "\n" << stages.str();
		glob2test::writeFile(glob2test::artifactDir() / "lan-input-delay.txt", table.str());
		MESSAGE(table.str());
	}
}
