// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// In-process multi-client harness for the turn protocol: 2-4 TurnSession clients and a
// TurnSequencer exchange real encoded frames over a simulated network with per-link
// latency, jitter, loss (as TCP-style retransmission delay), outages and restarts.
//
// Each client stands in for the engine: it pushes local AI orders, submits random
// human orders, folds every executed (tick, seat, order) into a state hash and reports
// that hash as its checksum, exactly where EngineRun calls the NetEngine method set.
// The harness checks that every client executes the same sequence as the relay's turn
// log. TurnEngineHarness.cpp runs the same network with real engines and game checksums.

#include "Glob2Test.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <sstream>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <random>

#include <nlohmann/json.hpp>

#include "MatchRecord.h"
#include "TurnSequencer.h"
#include "TurnSession.h"
#include "TurnLatencyTrace.h"
#include "TurnTestSupport.h"

using namespace Turn;
using namespace turntest;

namespace
{
struct Executed
{
	std::uint32_t tick;
	std::uint8_t seat;
	std::vector<std::uint8_t> order;
	bool operator==(const Executed& o) const { return tick == o.tick && seat == o.seat && order == o.order; }
};

/// Stands in for Engine: the same calls, in the same order, as EngineRun's loop.
class SimClient
{
public:
	SimClient(SimNetwork& net, int index, int seat, int players, std::vector<int> aiSeats, std::uint32_t seed)
		: net(net), index(index), seat(seat), players(players), aiSeats(std::move(aiSeats)), rng(seed)
	{
		restart();
	}

	/// A process restart: fresh session and engine state, same ticket.
	void restart()
	{
		session.reset();
		transport = std::make_unique<SimTransport>(net, index);
		TurnSessionConfig config;
		config.ticket = "seat:" + std::to_string(seat);
		session = std::make_unique<TurnSession>(players, *transport, config, bytesOrderCodec());
		session->onSubmitted = [this](std::uint32_t sequence) { trace.submitted(sequence, net.now, session->executedTick()); };
		session->onHorizon = [this](std::uint32_t horizon) { trace.received(horizon, net.now); };
		resetState();
		submittedAt.clear();
		nextTickAt = net.now;
		wasReady = true;
		++restarts;
	}

	void frame()
	{
		session->update(net.now);
		if (session->needsReload())
		{
			resetState(); // reload the initial game state
			session->reloadDone();
			++reloads;
		}
		if (stopped || session->state() == TurnSession::State::Ended)
			return;
		int budget = 2000; // ticks per frame while catching up
		while (budget-- > 0)
		{
			const bool catching = session->catchingUp();
			if (!catching && net.now < nextTickAt)
				break;
			// gatherAndAdvanceOrders
			if (wasReady)
			{
				if (orderRate > 0 && std::uniform_real_distribution<double>(0, 1)(rng) < orderRate)
				{
					auto bytes = randomOrder();
					// Input delay follows gameplay orders; voice has its own pace.
					if (bytes[0] != ORDER_TYPE_VOICE)
					{
						trace.queued(net.now);
						submittedAt.push_back(net.now);
					}
					session->addLocalOrder(makeBytesOrder(std::move(bytes)));
				}
			}
			for (int ai : aiSeats)
				if (!session->orderReceived(ai))
					session->pushOrder(makeBytesOrder(aiOrder(ai)), ai, true);
			if (wasReady)
				session->advanceStep(checksum());
			// executeOrdersAndStep
			const bool ready = session->tickReady();
			waitingMask = ready ? 0 : session->getWaitingOnMask();
			if (!ready)
			{
				wasReady = false;
				break;
			}
			const std::uint32_t tick = session->executedTick();
			for (int p = 0; p < players; ++p)
			{
				auto order = session->retrieveOrder(p);
				REQUIRE(order->sender == p);
				const auto bytes = wireBytes(*order);
				if (p == seat)
					trace.executed(tick, net.now);
				if (bytes[0] != ORDER_TYPE_NULL && p == seat && !submittedAt.empty() && bytes[0] != ORDER_TYPE_PLAYER_QUIT &&
				    bytes[0] != ORDER_TYPE_VOICE)
				{
					inputDelays.push_back(net.now - submittedAt.front());
					submittedAt.pop_front();
				}
				if (bytes[0] != ORDER_TYPE_NULL)
				{
					if (bytes[0] != ORDER_TYPE_VOICE)
						executed.push_back({tick, static_cast<std::uint8_t>(p), bytes});
					fold(tick, p, bytes);
				}
			}
			if (catching)
				++catchUpTicks;
			session->clearTopOrders();
			if (tick == corruptAtTick)
			{
				state ^= 0x5A5A5A5A; // a simulated engine divergence
				corruptAtTick = UINT32_MAX;
			}
			// As Engine::stepSessionImpl: after waiting for a late bundle, move the
			// schedule back by up to one tick rather than bursting.
			if (!wasReady && !catching && net.now > nextTickAt)
				nextTickAt += std::min(net.now - nextTickAt, session->tickPeriodMicros());
			wasReady = true;
			const std::uint64_t interval = session->tickIntervalMicros();
			nextTickAt = std::max(nextTickAt, net.now > 500 * MS ? net.now - 500 * MS : 0) + interval;
			maxBuffered = std::max(maxBuffered, session->bufferedTicks());
		}
	}

	std::uint32_t checksum() const { return static_cast<std::uint32_t>(state ^ (state >> 32)); }

	SimNetwork& net;
	int index;
	int seat;
	int players;
	std::vector<int> aiSeats;
	std::mt19937 rng;
	std::unique_ptr<SimTransport> transport;
	std::unique_ptr<TurnSession> session;
	std::vector<Executed> executed;
	std::uint64_t state = 0;
	std::uint64_t nextTickAt = 0;
	bool wasReady = true;
	bool stopped = false;
	double orderRate = 0.05;
	double voiceShare = 0.1;
	std::uint32_t corruptAtTick = UINT32_MAX;
	Uint32 waitingMask = 0;
	std::uint32_t maxBuffered = 0;
	int reloads = 0;
	int restarts = -1;
	TurnSession::StallStats stallsBefore;
	std::uint32_t catchUpTicks = 0;
	std::deque<std::uint64_t> submittedAt;
	std::vector<std::uint64_t> inputDelays; ///< submit to local execution, microseconds
	LatencyTrace trace;

	double meanInputDelayMs() const
	{
		if (inputDelays.empty())
			return 0;
		double sum = 0;
		for (auto d : inputDelays)
			sum += static_cast<double>(d);
		return sum / static_cast<double>(inputDelays.size()) / 1000.0;
	}

private:
	void resetState()
	{
		executed.clear();
		state = 1469598103934665603ull;
	}
	void fold(std::uint32_t tick, int player, const std::vector<std::uint8_t>& bytes)
	{
		if (bytes[0] == ORDER_TYPE_VOICE)
			return; // voice never changes simulation state
		auto mix = [this](std::uint64_t v) { state = (state ^ v) * 1099511628211ull; };
		mix(tick);
		mix(static_cast<std::uint64_t>(player));
		for (auto b : bytes)
			mix(b);
	}
	std::vector<std::uint8_t> randomOrder()
	{
		const bool voice = std::uniform_real_distribution<double>(0, 1)(rng) < voiceShare;
		std::vector<std::uint8_t> bytes(voice ? 400 : 3 + rng() % 20);
		bytes[0] = voice ? ORDER_TYPE_VOICE : 20;
		for (std::size_t i = 1; i < bytes.size(); ++i)
			bytes[i] = static_cast<std::uint8_t>(rng());
		return bytes;
	}
	// AI orders depend only on the shared state, as real AIs do.
	std::vector<std::uint8_t> aiOrder(int ai) const
	{
		const std::uint64_t h = state * 31 + static_cast<std::uint64_t>(ai);
		if (h % 7 != 0)
			return {ORDER_TYPE_NULL};
		return {40, static_cast<std::uint8_t>(h >> 8), static_cast<std::uint8_t>(h >> 16)};
	}
};

// Named so call sites spell the link list's type: with a bare braced list such as
// Match({{a}, {b}}), GCC also reads {a} as the links and {b} as aiSeats of a
// temporary Match and reports the call as ambiguous with the move constructor.
using Links = std::vector<LinkProfile>;

struct Match
{
	SimNetwork net;
	std::vector<std::unique_ptr<SimClient>> clients;

	std::uint8_t bundleInterval;

	Match(const Links& links, std::vector<int> aiSeats = {}, SequencerConfig config = {})
		: bundleInterval(config.bundleInterval)
	{
		const int humans = static_cast<int>(links.size());
		const int players = humans + static_cast<int>(aiSeats.size());
		net.links = links;
		net.outageUntil.assign(links.size(), 0);
		const std::uint32_t mask = (1u << humans) - 1;
		net.relay = std::make_unique<TurnSequencer>(
			config, mask, [humans](const std::string& t) {
				const int s = t.rfind("seat:", 0) == 0 ? std::atoi(t.c_str() + 5) : -1;
				return s < humans ? s : -1;
			},
			net, 0);
		for (int i = 0; i < humans; ++i)
			clients.push_back(std::make_unique<SimClient>(net, i, i, players, aiSeats, 1000 + i));
		net.relay->onSequenced = [this](std::uint8_t seat, std::uint32_t sequence, std::uint32_t tick, std::uint32_t relayTick) {
			if (seat < clients.size())
				clients[seat]->trace.sequenced(sequence, tick, relayTick, net.now);
		};
		net.relay->onEmitted = [this](std::uint32_t, std::uint32_t horizon) {
			for (auto& c : clients)
				c->trace.emitted(horizon, net.now);
		};
	}

	/// Starts counting input delay and stalls from now on.
	void startMeasuring()
	{
		for (auto& c : clients)
		{
			c->trace.measuring = true;
			c->inputDelays.clear();
			c->stallsBefore = c->session->stallStats();
		}
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

	/// Stops new orders and lets every client drain to the relay's horizon.
	void settle()
	{
		for (auto& c : clients)
			c->orderRate = 0;
		run(20 * SECOND);
	}

	std::vector<Executed> relayLog(std::uint32_t below) const
	{
		std::vector<Executed> out;
		for (const auto& e : net.relay->turnLog())
			if (e.tick < below)
				out.push_back({e.tick, e.seat, e.order});
		return out;
	}

	/// Every client executed the same sequence (AI orders included), its human orders
	/// are exactly the relay's log, and it is within its buffer of the relay's horizon.
	void requireIdenticalExecution()
	{
		std::uint32_t common = UINT32_MAX;
		for (auto& c : clients)
			common = std::min(common, c->session->executedTick());
		REQUIRE(common > 0);
		const auto reference = relayLog(common);
		const std::uint32_t humans = net.relay->humanSeats();
		std::vector<Executed> firstPrefix;
		std::uint64_t referenceState = 0;
		for (std::size_t i = 0; i < clients.size(); ++i)
		{
			auto& c = *clients[i];
			std::vector<Executed> prefix, human;
			for (const auto& e : c.executed)
				if (e.tick < common)
				{
					prefix.push_back(e);
					if (humans & (1u << e.seat))
						human.push_back(e);
				}
			INFO("client " << i);
			CHECK(human.size() == reference.size());
			CHECK(human == reference);
			if (i == 0)
				firstPrefix = prefix;
			else
				CHECK(prefix == firstPrefix);
			// Ticks the relay has issued but this client cannot have received yet: a
			// bundle's worth, plus the link's worst one-way delivery (latency, jitter
			// and one retransmission when the link loses frames). A client that waited
			// for a late bundle also keeps up to a tick of it as extra buffer each
			// time, and drains it at up to 5% speed.
			const LinkProfile& link = net.links[i];
			const std::uint64_t tick = c.session->tickPeriodMicros();
			const std::uint64_t worstDelivery = link.latency + link.jitter + (link.loss > 0 ? link.retransmit : 0);
			const std::uint64_t inFlight = bundleInterval + (worstDelivery + tick - 1) / tick;
			CHECK(net.relay->horizon() - c.session->executedTick() <= c.session->targetTicks() + inFlight + 4);
			if (c.session->executedTick() == common)
			{
				if (!referenceState)
					referenceState = c.state;
				else
					CHECK(c.state == referenceState);
			}
		}
	}

	void writeSummary(const std::string& name, const std::string& text)
	{
		std::ofstream(glob2test::artifactDir() / (name + ".txt")) << text;
	}
};
}

TEST_SUITE("TurnHarness")
{
	GLOB2_TEST_CASE("two clients on clean links execute identical turns", "[network-sim]")
	{
		Match m(Links{{20 * MS}, {30 * MS}});
		m.run(30 * SECOND);
		m.settle();
		m.requireIdenticalExecution();
		CHECK(m.net.relay->turnLog().size() > 50);
		CHECK_FALSE(m.net.relay->desyncFlagged());
		for (auto& c : m.clients)
		{
			CHECK(c->session->targetTicks() == 0);
			CHECK(c->reloads == 0);
			CHECK(c->session->stallStats().longStalls == 0);
		}
		// Checksums agreed at every reported tick.
		for (std::uint32_t t = 0; t + 250 < m.net.relay->horizon(); t += 25)
			CHECK(m.net.relay->agreedChecksum(t));
	}

	GLOB2_TEST_CASE("four clients with latency, jitter, loss and AI seats", "[network-sim]")
	{
		Match m(Links{{15 * MS}, {60 * MS, 80 * MS}, {120 * MS, 20 * MS, 0.03}, {40 * MS, 10 * MS, 0.01}}, {4, 5});
		m.run(60 * SECOND);
		m.settle();
		m.requireIdenticalExecution();
		CHECK_FALSE(m.net.relay->desyncFlagged());
		// Each client's buffer follows its own link, not the worst one.
		CHECK(m.clients[0]->session->targetTicks() <= 3);
		CHECK(m.clients[1]->session->targetTicks() >= 3);
		CHECK(m.clients[1]->session->targetTicks() > m.clients[0]->session->targetTicks());
		// Input delay follows each player's own connection.
		CHECK(m.clients[0]->meanInputDelayMs() < m.clients[3]->meanInputDelayMs());
		CHECK(m.clients[3]->meanInputDelayMs() < m.clients[2]->meanInputDelayMs());
		CHECK(m.clients[0]->meanInputDelayMs() < 100);
		std::ostringstream summary;
		summary << "client one_way_ms jitter_ms loss target_ticks measured_jitter_ms rtt_ms mean_input_delay_ms orders\n";
		for (std::size_t i = 0; i < m.clients.size(); ++i)
		{
			const auto& c = *m.clients[i];
			const auto& l = m.net.links[i];
			summary << i << ' ' << l.latency / MS << ' ' << l.jitter / MS << ' ' << l.loss << ' ' << c.session->targetTicks()
			        << ' ' << c.session->jitterMicros() / 1000 << ' ' << c.session->rttMicros() / 1000 << ' '
			        << c.meanInputDelayMs() << ' ' << c.inputDelays.size() << '\n';
		}
		m.writeSummary("four-client-delay", summary.str());
		MESSAGE(summary.str());

		// The record carries the same turns, without voice, and survives a round trip.
		m.net.relay->finish(m.net.now);
		std::array<std::uint8_t, 32> hash{};
		hash[0] = 1;
		const auto record = m.net.relay->buildRecord("harness", "test", "{}", hash);
		CHECK(record.turns == m.net.relay->turnLog());
		for (const auto& t : record.turns)
			CHECK(t.order[0] != ORDER_TYPE_VOICE);
		CHECK(MatchRecord::parse(record.serialize()) == record);
		CHECK((record.flags & MatchRecord::FLAG_INCOMPLETE) != 0);
		CHECK(record.reports.size() >= 4 * (record.endTick / 25 - 1));
	}

	GLOB2_TEST_CASE("a stalled client stalls only itself and resumes from its horizon", "[network-sim]")
	{
		Match m(Links{{20 * MS}, {25 * MS}, {30 * MS}});
		m.run(10 * SECOND);
		auto& victim = *m.clients[2];
		const std::uint32_t before0 = m.clients[0]->session->executedTick();
		const std::uint32_t victimBefore = victim.session->executedTick();
		m.net.outage(2, 6 * SECOND, *victim.transport);
		bool sawReconnecting = false;
		bool sawWaiting = false;
		m.run(6 * SECOND, [&] {
			sawReconnecting |= m.clients[0]->session->presence(2) == PresenceState::Reconnecting;
			sawWaiting |= (victim.waitingMask & (1u << 2)) != 0;
		});
		// The others kept full speed through the outage.
		CHECK(m.clients[0]->session->executedTick() - before0 >= 145);
		CHECK(victim.session->executedTick() - victimBefore < 60);
		CHECK(sawReconnecting);
		CHECK(sawWaiting);
		m.run(10 * SECOND);
		CHECK(victim.catchUpTicks >= 100); // replayed at uncapped speed
		CHECK(victim.reloads == 0); // incremental resume, no reload
		CHECK(victim.session->bufferedTicks() <= victim.session->targetTicks() + 3);
		m.settle();
		m.requireIdenticalExecution();
		CHECK(m.net.relay->presence(2) == PresenceState::Connected);
	}

	GLOB2_TEST_CASE("a restarted client reloads, replays the full log and rejoins", "[network-sim]")
	{
		Match m(Links{{20 * MS}, {50 * MS, 30 * MS}}, {2});
		m.run(20 * SECOND);
		const std::uint32_t horizonAtRestart = m.net.relay->horizon();
		m.clients[1]->transport->close();
		m.clients[1]->restart();
		m.run(3 * SECOND);
		CHECK(m.clients[1]->session->executedTick() >= horizonAtRestart);
		m.run(10 * SECOND);
		m.settle();
		m.requireIdenticalExecution();
		CHECK_FALSE(m.net.relay->desyncFlagged());
	}

	GLOB2_TEST_CASE("a diverged client in a three-player match is repaired by rejoin", "[network-sim]")
	{
		Match m(Links{{20 * MS}, {30 * MS, 10 * MS}, {40 * MS}});
		m.clients[1]->corruptAtTick = 300;
		m.run(40 * SECOND);
		CHECK(m.clients[1]->reloads == 1);
		m.settle();
		m.requireIdenticalExecution();
		CHECK_FALSE(m.net.relay->desyncFlagged());
		const auto record = m.net.relay->buildRecord("m", "v", "{}", {});
		bool told = false, resynced = false;
		for (const auto& e : record.events)
		{
			told |= e.kind == MatchEventKind::ToldToRejoin && e.seat == 1;
			resynced |= e.kind == MatchEventKind::Resynced && e.seat == 1;
		}
		CHECK(told);
		CHECK(resynced);
		// The record keeps the diverged report, so the verifier can name the client.
		bool divergent = false;
		for (const auto& r : record.reports)
			if (r.seat == 1 && r.tick == 325)
				divergent = r.checksum != *m.net.relay->agreedChecksum(325);
		CHECK(divergent);
	}

	GLOB2_TEST_CASE("two diverged clients are flagged and keep playing", "[network-sim]")
	{
		Match m(Links{{20 * MS}, {30 * MS}});
		m.clients[1]->corruptAtTick = 100;
		m.run(15 * SECOND);
		CHECK(m.net.relay->desyncFlagged());
		CHECK(m.clients[0]->session->desyncFlagged());
		CHECK(m.clients[1]->session->desyncFlagged());
		CHECK(m.clients[1]->reloads == 0);
		CHECK(m.clients[0]->session->executedTick() > 300);
	}

	GLOB2_TEST_CASE("the input delay rises with jitter and returns when it subsides", "[network-sim][artifacts]")
	{
		Match m(Links{{20 * MS}, {30 * MS}});
		std::ostringstream trace;
		trace << "seconds target_ticks jitter_ms buffered_ticks\n";
		std::uint64_t lastTrace = 0;
		auto sample = [&] {
			if (m.net.now - lastTrace < SECOND)
				return;
			lastTrace = m.net.now;
			auto& s = *m.clients[1]->session;
			trace << m.net.now / SECOND << ' ' << s.targetTicks() << ' ' << s.jitterMicros() / 1000 << ' '
			      << s.bufferedTicks() << '\n';
		};
		m.run(15 * SECOND, sample);
		const std::uint32_t baseline = m.clients[1]->session->targetTicks();
		CHECK(baseline == 0);
		m.net.links[1].jitter = 150 * MS;
		std::uint32_t peak = 0;
		m.run(20 * SECOND, [&] {
			sample();
			peak = std::max(peak, m.clients[1]->session->targetTicks());
		});
		CHECK(peak >= 4);
		CHECK(m.clients[0]->session->targetTicks() == 0); // the other client is unaffected
		m.net.links[1].jitter = 0;
		m.run(60 * SECOND, sample);
		CHECK(m.clients[1]->session->targetTicks() == baseline);
		m.settle();
		m.requireIdenticalExecution();
		m.writeSummary("jitter-trace", trace.str());
	}

	GLOB2_TEST_CASE("input delay stays within bounds on loopback and at 50 ms", "[network-sim]")
	{
		// Submit to execution with the default relay (one-tick bundles). The bounds are
		// the timing model's (docs/multiplayer/turn-protocol.md) with a little slack:
		// round trip + half a tick of relay quantization + up to a tick of buffer.
		struct Case
		{
			std::uint64_t oneWay;
			double meanMs, p95Ms;
		};
		const std::vector<Case> cases = {{250, 60, 80}, {50 * MS, 150, 170}};
		for (const Case& k : cases)
		{
			INFO("one way " << k.oneWay / 1000.0 << " ms");
			Match m(Links{{k.oneWay}, {250}}, {2});
			for (auto& c : m.clients)
				c->orderRate = 0.1;
			m.run(5 * SECOND);
			m.startMeasuring();
			m.run(30 * SECOND);
			auto& c = *m.clients[0];
			const auto b = c.trace.breakdown();
			REQUIRE(b.samples > 30);
			CHECK(b.total.mean <= k.meanMs);
			CHECK(b.total.p95 <= k.p95Ms);
			CHECK(c.session->targetTicks() == 0);
			CHECK(c.session->stallStats().longStalls == c.stallsBefore.longStalls);
			m.settle();
			m.requireIdenticalExecution();
		}
	}

	GLOB2_TEST_CASE("input delay and stalls per link profile", "[network-sim][benchmark][artifacts]")
	{
		struct Profile
		{
			const char* name;
			LinkProfile link;
		};
		const std::vector<Profile> profiles = {
			{"loopback", {250}},
			{"15 ms", {15 * MS}},
			{"25 ms", {25 * MS}},
			{"50 ms", {50 * MS}},
			{"30 ms, 80 ms jitter", {30 * MS, 80 * MS}},
			{"60 ms, 80 ms jitter", {60 * MS, 80 * MS}},
			{"30 ms, 80 ms jitter, 3% loss", {30 * MS, 80 * MS, 0.03}},
			{"120 ms, 3% loss", {120 * MS, 0, 0.03}},
		};
		std::ostringstream table, stages;
		table << "Simulated network, two humans and an AI; the measured player's link varies, the other is loopback. "
		         "Submit to execution, 10 s settle then 60 s measured, 5 ms frames; five network seeds per row "
		         "(delay: mean over seeds; target and jitter: largest at the end of a run; stalls: total).\n"
		         "bundle | link | mean ms | p95 ms | target ticks | measured jitter ms | stalls | long stalls | "
		         "stalled ms\n";
		stages << "bundle | link | " << LatencyTrace::header() << "\n";
		for (std::uint8_t bundle : {std::uint8_t(1), std::uint8_t(2)})
			for (const auto& profile : profiles)
			{
				// Five network seeds per profile: delay is their mean, stalls their sum.
				double meanSum = 0, p95Sum = 0;
				std::uint64_t stalls = 0, longStalls = 0, stalledMicros = 0;
				std::uint32_t target = 0;
				std::int64_t jitter = 0;
				constexpr int seeds = 5;
				for (int seed = 1; seed <= seeds; ++seed)
				{
					SequencerConfig config;
					config.bundleInterval = bundle;
					Match m(Links{profile.link, {250}}, {2}, config);
					m.net.rng.seed(static_cast<std::uint32_t>(seed));
					for (auto& c : m.clients)
						c->orderRate = 0.1;
					m.run(10 * SECOND);
					m.startMeasuring();
					m.run(60 * SECOND);
					auto& c = *m.clients[0];
					const auto b = c.trace.breakdown();
					const auto& st = c.session->stallStats();
					meanSum += b.total.mean;
					p95Sum += b.total.p95;
					stalls += st.stalls - c.stallsBefore.stalls;
					longStalls += st.longStalls - c.stallsBefore.longStalls;
					stalledMicros += st.stalledMicros - c.stallsBefore.stalledMicros;
					target = std::max(target, c.session->targetTicks());
					jitter = std::max(jitter, c.session->jitterMicros());
					if (seed == 1)
						stages << int(bundle) << " | " << profile.name << " | " << LatencyTrace::row(b) << "\n";
					m.settle();
					m.requireIdenticalExecution();
				}
				table << int(bundle) << " | " << profile.name << " | " << meanSum / seeds << " | " << p95Sum / seeds << " | "
				      << target << " | " << jitter / 1000 << " | " << stalls << " | " << longStalls << " | "
				      << stalledMicros / 1000 << "\n";
			}
		table << "\n" << stages.str();
		std::ofstream(glob2test::artifactDir() / "turn-delay-profiles.txt") << table.str();
		MESSAGE(table.str());
	}

	GLOB2_TEST_CASE("players who quit or never return are sequenced out", "[network-sim]")
	{
		SequencerConfig config;
		config.graceMicros = 5 * SECOND;
		Match m(Links{{20 * MS}, {30 * MS}, {25 * MS}}, {}, config);
		m.run(5 * SECOND);
		m.clients[2]->session->quit();
		m.clients[2]->stopped = true;
		m.run(2 * SECOND);
		CHECK(m.net.relay->presence(2) == PresenceState::Left);
		m.net.outage(1, 60 * SECOND, *m.clients[1]->transport);
		m.clients[1]->stopped = true;
		m.run(8 * SECOND);
		CHECK(m.net.relay->presence(1) == PresenceState::Left);
		int quits = 0;
		for (const auto& e : m.clients[0]->executed)
			quits += e.order[0] == ORDER_TYPE_PLAYER_QUIT;
		CHECK(quits == 2);
		CHECK_FALSE(m.net.relay->matchOver());
		m.clients[0]->session->addLocalOrder(makeBytesOrder({ORDER_TYPE_PLAYER_QUIT, 0, 0, 0, 0}));
		m.run(1 * SECOND);
		CHECK(m.net.relay->matchOver());
		const auto record = m.net.relay->buildRecord("m", "v", "{}", {});
		CHECK((record.flags & MatchRecord::FLAG_INCOMPLETE) == 0);
	}

	GLOB2_TEST_CASE("network telemetry follows injected latency, jitter, loss, an outage and a rejoin",
	                "[network-sim][artifacts]")
	{
		Match m(Links{{15 * MS}, {60 * MS, 80 * MS}, {120 * MS, 20 * MS, 0.03}}, {3});
		m.clients[2]->corruptAtTick = 900; // a divergence: client 2 is told to rejoin and reloads
		m.run(30 * SECOND);
		m.net.outage(1, 4 * SECOND, *m.clients[1]->transport);
		m.run(20 * SECOND);
		m.settle();
		m.requireIdenticalExecution();

		const auto& t0 = m.clients[0]->session->telemetry().totals();
		const auto& t1 = m.clients[1]->session->telemetry().totals();
		const auto& t2 = m.clients[2]->session->telemetry().totals();
		// Round trips follow each link (two one-way latencies plus up to one relay frame).
		CHECK(t0.rttMicros.count() > 50);
		CHECK(t0.rttMicros.quantile(0.5) >= 30 * MS);
		CHECK(t0.rttMicros.quantile(0.5) <= 45 * MS);
		CHECK(t2.rttMicros.quantile(0.5) >= 240 * MS);
		CHECK(t1.rttMicros.quantile(0.95) > t0.rttMicros.quantile(0.95));
		// Jitter and the buffer it calls for are each client's own.
		CHECK(t1.jitterMicros.quantile(0.95) > t0.jitterMicros.quantile(0.95));
		CHECK(t1.targetTicks.quantile(0.95) > t0.targetTicks.quantile(0.95));
		CHECK(t0.targetTicks.quantile(0.5) == 0); // a steady link holds no buffer (turn-protocol.md)
		CHECK(t0.liveTicks > 1000);
		// Input delay agrees with the harness's own measurement (which also counts voice,
		// and cannot tell a replayed order from a new one, so the reloading client 2 is
		// compared only by count).
		for (auto& c : m.clients)
		{
			const auto& h = c->session->telemetry().totals().inputDelayMicros;
			INFO("client " << c->index);
			REQUIRE(h.count() > 20);
			if (c->index == 2)
				continue;
			CHECK(h.count() <= c->inputDelays.size());
			CHECK(h.mean() / 1000.0 == doctest::Approx(c->meanInputDelayMs()).epsilon(0.25));
		}
		CHECK(t0.inputDelayMicros.mean() < t2.inputDelayMicros.mean());
		// The outage: a reconnect, its downtime, a long stall, then a catch-up.
		const auto& tel1 = m.clients[1]->session->telemetry();
		CHECK(t1.reconnects >= 1);
		CHECK(t1.downtimeMicros >= 2 * SECOND);
		CHECK(tel1.longestStallMicros() >= 3 * SECOND);
		CHECK(t1.catchUps >= 1);
		CHECK(t1.catchUpTicks >= 50);
		CHECK(t0.reconnects == 0);
		CHECK(m.clients[0]->session->telemetry().longestStallMicros() < SECOND);
		// The rejoin: a desync notice, a reload and its fast-forward.
		const auto& tel2 = m.clients[2]->session->telemetry();
		CHECK(t2.desyncRejoins == 1);
		CHECK(t2.reloads == 1);
		CHECK(t2.resyncRequests >= 1);
		CHECK(tel2.reloadFastForwardTicks() >= 800);
		// Everyone saw seat 1 reconnect.
		const auto& tel0 = m.clients[0]->session->telemetry();
		CHECK(tel0.transitions(1, PresenceState::Reconnecting) >= 1);
		CHECK(tel0.timeIn(1, PresenceState::Reconnecting, m.net.now) >= 2 * SECOND);
		CHECK(tel0.transitions(2, PresenceState::Resyncing) == 1);
		// Traffic and voice.
		CHECK(t0.bundlesReceived > 500);
		CHECK(t0.bytesReceived > t0.bundleBytes);
		CHECK(t0.voiceSent > 0);
		CHECK(t0.voiceReceived > 0);
		// The time series: one point per 5 s.
		CHECK(tel0.series().size() >= 13);

		// Relay side.
		const auto& relay = m.net.relay->telemetry();
		CHECK(relay.seats[1].disconnects >= 1);
		CHECK(relay.seats[1].graceMicrosTotal >= 2 * SECOND);
		CHECK(relay.seats[1].lagTicks.max() > 50);
		CHECK(relay.seats[2].toldToRejoin == 1);
		CHECK(relay.seats[2].logBundlesSent >= 1);
		std::uint64_t sequenced = 0;
		for (const auto& seat : relay.seats)
		{
			sequenced += seat.ordersSequenced;
			CHECK(seat.voiceSequenced > 0);
			CHECK(seat.checksumReports > 20);
		}
		CHECK(sequenced == m.net.relay->turnLog().size());
		CHECK(relay.majority >= 1);
		CHECK(relay.unanimous + relay.majority + relay.flaggedTicks == relay.arbitrations);

		std::ostringstream out;
		out << "client one_way_ms jitter_ms loss rtt_p50_ms rtt_p95_ms jitter_p95_ms target_p95 input_delay_mean_ms "
		       "input_delay_p95_ms stalls longest_stall_ms reconnects downtime_ms reloads catch_up_ticks\n";
		for (std::size_t i = 0; i < m.clients.size(); ++i)
		{
			const auto& tel = m.clients[i]->session->telemetry();
			const auto& c = tel.totals();
			const auto& l = m.net.links[i];
			out << i << ' ' << l.latency / MS << ' ' << l.jitter / MS << ' ' << l.loss << ' '
			    << c.rttMicros.quantile(0.5) / MS << ' ' << c.rttMicros.quantile(0.95) / MS << ' '
			    << c.jitterMicros.quantile(0.95) / MS << ' ' << c.targetTicks.quantile(0.95) << ' '
			    << c.inputDelayMicros.mean() / 1000.0 << ' ' << c.inputDelayMicros.quantile(0.95) / MS << ' '
			    << c.stallMicros.count() << ' ' << tel.longestStallMicros() / MS << ' ' << c.reconnects << ' '
			    << c.downtimeMicros / MS << ' ' << c.reloads << ' ' << c.catchUpTicks << '\n';
		}
		m.writeSummary("network-telemetry", out.str());
		std::ofstream(glob2test::artifactDir() / "network-telemetry-relay.json") << m.net.relay->networkSummary().dump(1);
		std::ofstream(glob2test::artifactDir() / "network-telemetry-client1.json")
		    << m.clients[1]->session->telemetry().toJson(m.net.now).dump(1);
		MESSAGE(out.str());
	}
}
