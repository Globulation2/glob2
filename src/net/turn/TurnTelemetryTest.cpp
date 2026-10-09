// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// Unit tests for the turn-game network telemetry aggregators (src/net/turn/TurnTelemetry):
// the histogram, the client session counters under a fake clock, the relay's per-seat
// counters, their JSON summaries and the relay's Prometheus text. TurnHarnessTest and
// TurnEngineHarness check the same telemetry end to end under simulated networks.

#include "Glob2Test.h"

#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

#include "MatchRecord.h"
#include "TurnSequencer.h"
#include "TurnTelemetry.h"
#include "TurnTestSupport.h"

using namespace Turn;
using namespace turntest;
using nlohmann::json;

namespace
{
struct RecordingOutput : SequencerOutput
{
	std::map<PeerId, std::vector<std::vector<std::uint8_t>>> frames;
	std::vector<PeerId> closed;
	void send(PeerId peer, const std::vector<std::uint8_t>& payload) override { frames[peer].push_back(payload); }
	void close(PeerId peer) override { closed.push_back(peer); }
};

std::vector<std::uint8_t> encodeOrder(std::uint32_t sequence, std::vector<std::uint8_t> order)
{
	OrderSubmit submit;
	submit.clientSequence = sequence;
	submit.order = std::move(order);
	return TurnCodec::encode(submit);
}

std::vector<std::uint8_t> hello(int seat, std::uint32_t haveHorizon = 0)
{
	Hello h;
	h.ticket = "seat:" + std::to_string(seat);
	h.haveHorizon = haveHorizon;
	return TurnCodec::encode(h);
}

int admit(const std::string& ticket)
{
	return ticket.rfind("seat:", 0) == 0 ? std::atoi(ticket.c_str() + 5) : -1;
}
}

TEST_SUITE("TurnTelemetry")
{
	GLOB2_TEST_CASE("histogram quantiles are within a bin of the data", "[network]")
	{
		Histogram h;
		CHECK(h.quantile(0.5) == 0);
		CHECK(h.count() == 0);
		for (std::uint64_t v = 1; v <= 1000; ++v)
			h.add(v * 1000);
		CHECK(h.count() == 1000);
		CHECK(h.min() == 1000);
		CHECK(h.max() == 1000000);
		CHECK(h.mean() == doctest::Approx(500500.0));
		const auto p50 = static_cast<double>(h.quantile(0.5));
		const auto p95 = static_cast<double>(h.quantile(0.95));
		CHECK(p50 == doctest::Approx(500000.0).epsilon(0.04));
		CHECK(p95 == doctest::Approx(950000.0).epsilon(0.04));
		CHECK(h.quantile(1.0) == 1000000);
		CHECK(h.quantile(0.0) == 1000);

		// Small values are exact; huge values share the last bin but keep the maximum.
		Histogram small;
		for (int i = 0; i < 10; ++i)
			small.add(3);
		CHECK(small.quantile(0.5) == 3);
		Histogram huge;
		huge.add(std::uint64_t(1) << 50);
		CHECK(Histogram::bin(std::uint64_t(1) << 50) == Histogram::BINS - 1);
		CHECK(huge.quantile(0.5) == (std::uint64_t(1) << 50));

		// Every bin's lower bound maps back to that bin.
		for (unsigned i = 0; i + 1 < Histogram::BINS; ++i)
		{
			INFO("bin " << i);
			CHECK(Histogram::bin(Histogram::binLow(i)) == i);
			CHECK(Histogram::bin(Histogram::binLow(i) + Histogram::binWidth(i) - 1) == i);
		}

		Histogram a, b;
		for (int i = 0; i < 100; ++i)
			a.add(10);
		for (int i = 0; i < 100; ++i)
			b.add(1000);
		a.merge(b);
		CHECK(a.count() == 200);
		CHECK(a.min() == 10);
		CHECK(a.max() == 1000);
		CHECK(a.quantile(0.25) == 10);
		CHECK(a.quantile(0.95) == doctest::Approx(1000).epsilon(0.04));
	}

	GLOB2_TEST_CASE("session telemetry measures stalls, catch-up, reconnects and input delay", "[network]")
	{
		SessionTelemetry t;
		t.intervalMicros = 5 * SECOND;
		t.update(0, 1 * SECOND);
		t.welcomed(1 * SECOND);
		CHECK(t.started());
		// Waiting for the first tick (players loading) is not a stall.
		t.readiness(true, false, 1 * SECOND);
		t.readiness(false, true, 2 * SECOND);
		CHECK(t.totals().stallMicros.count() == 0);
		// A real starvation: 300 ms without an authorized tick.
		t.readiness(true, false, 3 * SECOND);
		t.readiness(true, false, 3 * SECOND + 100 * MS);
		t.readiness(false, true, 3 * SECOND + 300 * MS);
		CHECK(t.totals().stallMicros.count() == 1);
		CHECK(t.totals().stallMicrosTotal == 300 * MS);
		CHECK(t.longestStallMicros() == 300 * MS);

		// Input delay: matched by content, in submission order, skipping voice.
		const std::vector<std::uint8_t> a{20, 1, 2}, b{20, 3, 4}, voice{ORDER_TYPE_VOICE, 9};
		t.orderSubmitted(a[0], a.size(), true);
		t.pendingInput(a.data(), a.size(), 10, 4 * SECOND);
		t.orderSubmitted(voice[0], voice.size(), true);
		t.pendingInput(voice.data(), voice.size(), 10, 4 * SECOND);
		t.orderSubmitted(b[0], b.size(), false);
		t.pendingInput(b.data(), b.size(), 11, 4 * SECOND + 40 * MS);
		t.ownOrderExecuted(voice.data(), voice.size(), 14, 4 * SECOND + 100 * MS);
		t.ownOrderExecuted(a.data(), a.size(), 14, 4 * SECOND + 150 * MS);
		// A replay of an older tick (after a reload) never matches a newer submission.
		t.ownOrderExecuted(b.data(), b.size(), 5, 4 * SECOND + 160 * MS);
		t.ownOrderExecuted(b.data(), b.size(), 15, 4 * SECOND + 240 * MS);
		CHECK(t.totals().inputDelayMicros.count() == 2);
		CHECK(t.totals().inputDelayMicros.min() == 150 * MS);
		CHECK(t.totals().inputDelayMicros.max() == 200 * MS);
		CHECK(t.totals().ordersSubmitted == 3);
		CHECK(t.totals().ordersQueuedOffline == 1);
		CHECK(t.totals().voiceSent == 1);
		CHECK(t.unmatchedInputDelay() == 0);

		// Live ticks feed the buffer and nudge; catch-up ticks do not.
		t.tickExecuted(3, 2, 1.02, false, 15, 5 * SECOND);
		t.tickExecuted(1, 2, 0.98, false, 16, 5 * SECOND);
		t.tickExecuted(2, 2, 1.0, false, 17, 5 * SECOND);
		t.catchUpState(true, 17, 6 * SECOND);
		for (int i = 0; i < 50; ++i)
			t.tickExecuted(40, 2, 1.0, true, 18 + i, 6 * SECOND);
		t.catchUpState(false, 67, 6 * SECOND + 500 * MS);
		CHECK(t.totals().liveTicks == 3);
		CHECK(t.totals().ticksFaster == 1);
		CHECK(t.totals().ticksSlower == 1);
		CHECK(t.totals().bufferTicks.max() == 3);
		CHECK(t.totals().catchUps == 1);
		CHECK(t.totals().catchUpTicks == 50);
		CHECK(t.totals().catchUpMicros == 500 * MS);
		CHECK(t.reloadFastForwardTicks() == 0);

		// A link loss and the Welcome that ends it; then a reload's fast-forward.
		t.linkLost(7 * SECOND);
		t.linkLost(7 * SECOND + 10 * MS); // still down: one loss
		t.welcomed(9 * SECOND);
		CHECK(t.totals().reconnects == 1);
		CHECK(t.totals().downtimeMicros == 2 * SECOND);
		CHECK(t.longestDowntimeMicros() == 2 * SECOND);
		t.reloadRequested();
		t.catchUpState(true, 70, 9 * SECOND);
		for (int i = 0; i < 70; ++i)
			t.tickExecuted(60, 2, 1.0, true, i + 1, 9 * SECOND);
		t.catchUpState(false, 70, 9 * SECOND + 700 * MS);
		t.reloadLoad(120 * MS);
		CHECK(t.totals().reloads == 1);
		CHECK(t.reloadFastForwardTicks() == 70);
		CHECK(t.reloadFastForwardMicros() == 700 * MS);
		CHECK(t.reloadLoadTotalMicros() == 120 * MS);

		// Presence of other seats: transitions and time per state.
		t.presence(1, PresenceState::Connected, 2 * SECOND);
		t.presence(1, PresenceState::Reconnecting, 8 * SECOND);
		t.presence(1, PresenceState::Reconnecting, 8 * SECOND + 500 * MS); // repeated snapshot
		t.presence(1, PresenceState::Connected, 10 * SECOND);
		CHECK(t.transitions(1, PresenceState::Reconnecting) == 1);
		CHECK(t.transitions(1, PresenceState::Connected) == 2);
		CHECK(t.timeIn(1, PresenceState::Reconnecting, 11 * SECOND) == 2 * SECOND);
		CHECK(t.timeIn(1, PresenceState::Connected, 11 * SECOND) == 7 * SECOND);

		// The time series closes an interval every 5 s of session time.
		t.pong(40 * MS, 5 * MS);
		t.update(80, 11 * SECOND);
		REQUIRE(t.series().size() == 1);
		const auto& p = t.series().front();
		CHECK(p.startMicros == 0);
		CHECK(p.endMicros == 10 * SECOND);
		CHECK(p.endTick == 80);
		CHECK(p.rtt.count == 1);
		CHECK(p.rtt.p50 == 40 * MS);
		CHECK(p.liveTicks == 3);
		CHECK(t.current().rttMicros.count() == 0); // the window restarted
		CHECK(t.totals().rttMicros.count() == 1);

		const json j = t.toJson(12 * SECOND);
		CHECK(j.at("stalls").at("count") == 1);
		CHECK(j.at("stalls").at("longest_us") == 300 * MS);
		CHECK(j.at("input_delay_us").at("count") == 2);
		CHECK(j.at("reconnects").at("downtime_us") == 2 * SECOND);
		CHECK(j.at("reloads").at("fast_forward_ticks") == 70);
		CHECK(j.at("series").at("points").size() == 1);
		CHECK(j.at("presence").at("seats").size() == 1);
		CHECK(j.at("presence").at("seats")[0].at("transitions").at("reconnecting") == 1);
	}

	GLOB2_TEST_CASE("the client network summary is one stable versioned object", "[network]")
	{
		SessionTelemetry t;
		t.update(0, SECOND);
		ClientNetworkContext context;
		context.simVersion = "125-49-abc";
		context.platform = "Linux";
		context.transport = "lan";
		context.seat = 1;
		context.humanSeatMask = 3;
		context.players = 3;
		context.finalTick = 1234;
		json j = clientNetworkSummary(t, context, 2 * SECOND, false);
		CHECK(j.at("schema") == "ClientNetworkSummary");
		CHECK(j.at("schema_version") == CLIENT_NETWORK_SUMMARY_VERSION);
		CHECK(j.at("match").at("transport") == "lan");
		CHECK(j.at("match").at("relay_id").is_null());
		CHECK(j.at("match").at("final_tick") == 1234);
		CHECK(j.at("order_validation").is_null());
		CHECK_FALSE(j.contains("series"));
		for (const char* key : {"rtt_us", "jitter_us", "input_delay_us", "jitter_buffer", "tick_rate_nudge", "stalls",
		                        "catch_up", "reconnects", "reloads", "traffic", "orders", "voice", "desync", "presence"})
			CHECK_MESSAGE(j.contains(key), key);

		context.transport = "online";
		context.relayId = "relay-1";
		context.orderValidationAvailable = true;
		ClientNetworkContext::SeatVerdicts v;
		v.seat = 0;
		v.accepted = 10;
		v.rejected = 1;
		v.reasons = {{"wrong_team", 1}};
		context.orderValidation.push_back(v);
		j = clientNetworkSummary(t, context, 2 * SECOND, true);
		CHECK(j.at("match").at("relay_id") == "relay-1");
		CHECK(j.at("order_validation").at("seats")[0].at("rejected_by_reason").at("wrong_team") == 1);
		CHECK(j.contains("series"));
		// Nothing identifying: no names, addresses or account fields anywhere.
		const std::string text = j.dump();
		for (const char* banned : {"account", "address", "\"ip\"", "name\""})
			CHECK(text.find(banned) == std::string::npos);
	}

	GLOB2_TEST_CASE("relay telemetry counts per-seat orders, deferrals, bundles, lateness and arbitration", "[network]")
	{
		RecordingOutput out;
		SequencerConfig config;
		config.graceMicros = 10 * SECOND;
		TurnSequencer relay(config, 0b111, admit, out, 0);
		for (PeerId p = 1; p <= 3; ++p)
		{
			relay.onConnect(p, 0);
			relay.onReceive(p, hello(static_cast<int>(p) - 1), 0);
		}
		// Seat 0 sends three orders at once: one per tick, so two are deferred.
		relay.onReceive(1, encodeOrder(1, {20, 1}), 10 * MS);
		relay.onReceive(1, encodeOrder(2, {20, 2}), 10 * MS);
		relay.onReceive(1, encodeOrder(3, {20, 3}), 10 * MS);
		relay.onReceive(1, encodeOrder(3, {20, 3}), 10 * MS); // a resubmission
		relay.onReceive(2, encodeOrder(1, {ORDER_TYPE_VOICE, 1, 2, 3}), 10 * MS);
		relay.onReceive(2, encodeOrder(2, {ORDER_TYPE_NULL}), 10 * MS);
		std::uint64_t now = 0;
		for (int i = 0; i < 100; ++i)
		{
			now += 20 * MS;
			relay.update(now);
		}
		// Checksums for tick 25: two agree, the third differs -> majority, seat 2 told to rejoin.
		auto report = [&](PeerId p, std::uint32_t tick, std::uint32_t checksum) {
			ChecksumReport r;
			r.tick = tick;
			r.checksum = checksum;
			relay.onReceive(p, TurnCodec::encode(r), now);
		};
		report(1, 25, 7);
		report(2, 25, 7);
		report(3, 25, 9);
		// A ping from seat 1 that is 30 ticks behind.
		Ping ping;
		ping.nonce = 1;
		ping.executedTick = relay.relayTick(now) - 30;
		relay.onReceive(2, TurnCodec::encode(ping), now);
		// Seat 1 disconnects for 2 s and comes back.
		relay.onDisconnect(2, now);
		now += 2 * SECOND;
		relay.update(now);
		relay.onConnect(4, now);
		relay.onReceive(4, hello(1), now); // resumes from tick 0: the log is replayed
		relay.update(now + 40 * MS);

		const SequencerTelemetry& t = relay.telemetry();
		REQUIRE(t.seats.size() == 3);
		const auto& s0 = t.seats[0];
		CHECK(s0.ordersSequenced == 3);
		CHECK(s0.ordersDeferred == 2);
		CHECK(s0.deferTicks.max() == 2);
		CHECK(s0.duplicatesIgnored == 1);
		CHECK(s0.maxQueuedAhead == 3);
		CHECK(s0.checksumReports == 1);
		CHECK(s0.reportLatenessTicks.count() == 1);
		CHECK(s0.bundlesSent > 10);
		CHECK(s0.framesReceived == 5); // 4 orders and a report; the Hello precedes admission
		const auto& s1 = t.seats[1];
		CHECK(s1.voiceSequenced == 1);
		CHECK(s1.ordersSequenced == 0);
		CHECK(s1.ordersDropped == 1);
		CHECK(s1.lagTicks.max() == 30);
		CHECK(s1.disconnects == 1);
		CHECK(s1.connects == 2);
		CHECK(s1.graceMicrosTotal == 2 * SECOND);
		CHECK(s1.logBundlesSent >= 1);
		CHECK(t.seats[2].toldToRejoin == 1);
		CHECK(t.arbitrations == 1);
		CHECK(t.majority == 1);
		CHECK(t.peakPendingEntries >= 3);
		CHECK(t.bundlesBroadcast > 10);

		const json summary = relay.networkSummary();
		CHECK(summary.at("schema") == "RelayNetworkSummary");
		CHECK(summary.at("schema_version") == 1);
		REQUIRE(summary.at("seats").size() == 3);
		CHECK(summary.at("seats")[0].at("orders").at("deferred") == 2);
		CHECK(summary.at("seats")[1].at("connection").at("grace_used_ms") == 2000);
		CHECK(summary.at("arbitration").at("majority") == 1);

		RelayNetworkTotals totals;
		totals.add(t);
		totals.add(t);
		std::ostringstream prom;
		totals.writePrometheus(prom);
		const std::string text = prom.str();
		CHECK(text.find("glob2_relay_net_orders_sequenced_total 6\n") != std::string::npos);
		CHECK(text.find("glob2_relay_net_orders_deferred_total 4\n") != std::string::npos);
		CHECK(text.find("# TYPE glob2_relay_net_lag_ticks summary") != std::string::npos);
		CHECK(text.find("glob2_relay_net_lag_ticks_count 2\n") != std::string::npos);
		// Every sample line is "name value" or "name{labels} value".
		std::istringstream lines(text);
		std::string line;
		while (std::getline(lines, line))
			if (!line.empty() && line[0] != '#')
				CHECK(line.rfind("glob2_relay_net_", 0) == 0);
	}

	GLOB2_TEST_CASE("a seat that never returns uses its whole grace period", "[network]")
	{
		RecordingOutput out;
		SequencerConfig config;
		config.graceMicros = 3 * SECOND;
		TurnSequencer relay(config, 0b11, admit, out, 0);
		relay.onConnect(1, 0);
		relay.onReceive(1, hello(0), 0);
		relay.onConnect(2, 0);
		relay.onReceive(2, hello(1), 0);
		relay.update(SECOND);
		relay.onDisconnect(2, SECOND);
		for (std::uint64_t now = SECOND; now <= 5 * SECOND; now += 20 * MS)
			relay.update(now);
		const auto& s1 = relay.telemetry().seats[1];
		CHECK(s1.leftByGrace);
		CHECK(s1.graceMicrosTotal >= 3 * SECOND);
		CHECK(s1.leftTick > 0);
		const json summary = relay.networkSummary();
		CHECK(summary.at("seats")[1].at("connection").at("left_by_grace") == true);

		// The record proves the same facts without wall-clock values.
		relay.finish(6 * SECOND);
		const json fromRecord = recordNetworkSummary(relay.buildRecord("m", "v", "{}", {}));
		CHECK(fromRecord.at("schema") == "RecordNetworkSummary");
		REQUIRE(fromRecord.at("seats").size() == 2);
		CHECK(fromRecord.at("seats")[1].at("disconnects") == 1);
		CHECK(fromRecord.at("seats")[1].at("left_by") == "grace");
		CHECK(fromRecord.at("seats")[1].at("disconnected_ticks") == 3 * DEFAULT_TICK_RATE_MILLIHZ / 1000);
	}
}
