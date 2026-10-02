// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// Unit tests for the turn protocol building blocks in src/net/turn: codecs, the
// TurnSequencer relay core under a fake clock, the jitter buffer and delay controller,
// TurnSession against a scripted transport, and the match record format.

#include "Glob2Test.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <memory>

#include "JitterBuffer.h"
#include "Marshaling.h"
#include "MatchRecord.h"
#include "NetConsts.h"
#include "TurnMessages.h"
#include "TurnSequencer.h"
#include "TurnSession.h"
#include "TurnTestSupport.h"

using namespace Turn;
using namespace turntest;

static_assert(ORDER_TYPE_NULL == ORDER_NULL, "relay order ids must mirror NetConsts.h");
static_assert(ORDER_TYPE_PLAYER_QUIT == ORDER_PLAYER_QUIT_GAME, "relay order ids must mirror NetConsts.h");
static_assert(ORDER_TYPE_VOICE == ORDER_VOICE_DATA, "relay order ids must mirror NetConsts.h");
static_assert(ORDER_TYPE_ADJUST_LATENCY == ORDER_ADJUST_LATENCY, "relay order ids must mirror NetConsts.h");

namespace
{
constexpr std::uint64_t TICK = 40 * MS;

template <typename T>
std::shared_ptr<T> roundTrip(const T& message)
{
	const auto bytes = TurnCodec::encode(message);
	auto decoded = TurnCodec::decode(bytes);
	REQUIRE(decoded);
	REQUIRE(decoded->getMessageType() == message.getMessageType());
	CHECK(*decoded == message);
	CHECK(TurnCodec::encode(*decoded) == bytes);
	return std::static_pointer_cast<T>(decoded);
}

std::vector<std::uint8_t> order(std::uint8_t type, std::size_t size, std::uint8_t fill = 7)
{
	std::vector<std::uint8_t> bytes(size, fill);
	bytes[0] = type;
	return bytes;
}

TurnBundle sampleBundle()
{
	TurnBundle b;
	b.fromTick = 10;
	b.horizonTick = 14;
	b.entries.push_back({10, 0, order(20, 9)});
	b.entries.push_back({10, 3, order(40, 3)});
	b.entries.push_back({13, 1, order(72, 300)});
	return b;
}

// Records what the sequencer sends, decoded, per peer.
struct RecordingOutput : SequencerOutput
{
	std::map<PeerId, std::vector<std::shared_ptr<NetMessage>>> inbox;
	std::vector<PeerId> closed;
	void send(PeerId peer, const std::vector<std::uint8_t>& payload) override
	{
		auto message = TurnCodec::decode(payload);
		REQUIRE(message);
		inbox[peer].push_back(message);
	}
	void close(PeerId peer) override { closed.push_back(peer); }

	template <typename T>
	std::vector<std::shared_ptr<T>> take(PeerId peer, std::uint8_t type)
	{
		std::vector<std::shared_ptr<T>> found;
		auto& box = inbox[peer];
		for (auto it = box.begin(); it != box.end();)
			if ((*it)->getMessageType() == type)
			{
				found.push_back(std::static_pointer_cast<T>(*it));
				it = box.erase(it);
			}
			else
				++it;
		return found;
	}
	std::vector<TurnEntry> bundleEntries(PeerId peer, std::uint32_t* horizon = nullptr)
	{
		std::vector<TurnEntry> entries;
		for (auto& b : take<TurnBundle>(peer, MSG_TURN_BUNDLE))
		{
			entries.insert(entries.end(), b->entries.begin(), b->entries.end());
			if (horizon)
				*horizon = b->horizonTick;
		}
		return entries;
	}
	bool wasClosed(PeerId peer) const { return std::find(closed.begin(), closed.end(), peer) != closed.end(); }
};

// Tickets are "seat:N".
int ticketSeat(const std::string& ticket)
{
	if (ticket.rfind("seat:", 0) != 0)
		return -1;
	return std::atoi(ticket.c_str() + 5);
}

struct RelayFixture
{
	RecordingOutput out;
	std::uint64_t now = 1000000;
	TurnSequencer relay;
	explicit RelayFixture(std::uint32_t mask = 0b111, SequencerConfig config = {})
		: relay(config, mask, ticketSeat, out, 1000000) {}

	void at(std::uint64_t micros)
	{
		now = 1000000 + micros;
		relay.update(now);
	}
	void atTick(std::uint32_t tick, std::uint64_t offset = 1 * MS) { at(tick * TICK + offset); }
	void join(PeerId peer, int seat, std::uint32_t haveHorizon = 0)
	{
		relay.onConnect(peer, now);
		Hello h;
		h.ticket = "seat:" + std::to_string(seat);
		h.haveHorizon = haveHorizon;
		relay.onReceive(peer, TurnCodec::encode(h), now);
	}
	void submit(PeerId peer, std::uint32_t seq, std::vector<std::uint8_t> bytes)
	{
		OrderSubmit s;
		s.clientSequence = seq;
		s.order = std::move(bytes);
		relay.onReceive(peer, TurnCodec::encode(s), now);
	}
	void report(PeerId peer, std::uint32_t tick, std::uint32_t checksum)
	{
		ChecksumReport r;
		r.tick = tick;
		r.checksum = checksum;
		relay.onReceive(peer, TurnCodec::encode(r), now);
	}
	void raw(PeerId peer, const NetMessage& m) { relay.onReceive(peer, TurnCodec::encode(m), now); }
};
}

TEST_SUITE("TurnMessages")
{
	TEST_CASE("every message round-trips")
	{
		Hello h;
		h.ticket = std::string(MAX_TICKET_BYTES, 'x');
		h.haveHorizon = 1234;
		roundTrip(h);

		Welcome w;
		w.seat = 3;
		w.humanSeatMask = 0b1001;
		w.relayTick = 77;
		w.resumeFromTick = 50;
		w.graceTicks = 4500;
		w.lastClientSequence = 9;
		roundTrip(w);

		Reject r;
		r.reason = RejectReason::SeatLeft;
		r.detail = "gone";
		roundTrip(r);

		OrderSubmit o;
		o.clientSequence = 42;
		o.order = order(20, MAX_ORDER_BYTES);
		roundTrip(o);

		auto b = roundTrip(sampleBundle());
		CHECK(b->entries == sampleBundle().entries);
		TurnBundle empty;
		empty.fromTick = 4;
		empty.horizonTick = 6;
		roundTrip(empty);

		ChecksumReport c;
		c.tick = 25;
		c.checksum = 0xDEADBEEF;
		roundTrip(c);

		Presence p;
		p.seats.push_back({0, PresenceState::Connected, 0, 3});
		p.seats.push_back({5, PresenceState::Reconnecting, 4000, 80});
		roundTrip(p);

		ResyncRequest rr;
		rr.fromTick = 88;
		roundTrip(rr);

		DesyncNotice d;
		d.tick = 50;
		d.verdict = DesyncVerdict::Rejoin;
		d.divergedSeatMask = 4;
		roundTrip(d);

		Quit q;
		q.reason = QuitReason::GameFinished;
		roundTrip(q);

		Ping ping;
		ping.nonce = 5;
		ping.executedTick = 99;
		roundTrip(ping);

		Pong pong;
		pong.nonce = 5;
		pong.relayTick = 101;
		pong.lastClientSequence = 3;
		roundTrip(pong);
	}

	TEST_CASE("every truncation and any trailing byte is rejected")
	{
		std::vector<std::vector<std::uint8_t>> samples;
		Hello h;
		h.ticket = "seat:1";
		samples.push_back(TurnCodec::encode(h));
		samples.push_back(TurnCodec::encode(sampleBundle()));
		Presence p;
		p.seats.push_back({2, PresenceState::Lagging, 0, 60});
		samples.push_back(TurnCodec::encode(p));
		Welcome w;
		w.humanSeatMask = 1;
		samples.push_back(TurnCodec::encode(w));
		for (const auto& bytes : samples)
		{
			for (std::size_t n = 0; n < bytes.size(); ++n)
				CHECK_FALSE(TurnCodec::decode(bytes.data(), n));
			auto longer = bytes;
			longer.push_back(0);
			CHECK_FALSE(TurnCodec::decode(longer));
		}
	}

	TEST_CASE("malformed fields and non-turn types are rejected")
	{
		CHECK_FALSE(TurnCodec::decode(nullptr, 0));
		const std::vector<std::uint8_t> legacy = {0x01, 0, 0};
		CHECK_FALSE(TurnCodec::decode(legacy));
		const std::vector<std::uint8_t> reserved = {0xBF};
		CHECK_FALSE(TurnCodec::decode(reserved));
		std::vector<std::uint8_t> huge(MAX_FRAME_BYTES + 1, 0);
		huge[0] = MSG_PING;
		CHECK_FALSE(TurnCodec::decode(huge));

		// An order one byte over the limit, and an empty order.
		OrderSubmit o;
		o.order = order(20, MAX_ORDER_BYTES);
		auto bytes = TurnCodec::encode(o);
		bytes[5] = static_cast<std::uint8_t>((MAX_ORDER_BYTES + 1) >> 8);
		bytes[6] = static_cast<std::uint8_t>((MAX_ORDER_BYTES + 1) & 0xFF);
		bytes.push_back(1);
		CHECK_FALSE(TurnCodec::decode(bytes));
		const std::vector<std::uint8_t> emptyOrder = {MSG_ORDER_SUBMIT, 0, 0, 0, 1, 0, 0};
		CHECK_FALSE(TurnCodec::decode(emptyOrder));

		// A ticket length beyond the cap fails before any allocation.
		const std::vector<std::uint8_t> bigTicket = {MSG_HELLO, 0, 1, 0x7F, 0xFF, 0xFF, 0xFF};
		CHECK_FALSE(TurnCodec::decode(bigTicket));

		// Bundle invariants: order, range and seat.
		auto bad = sampleBundle();
		std::swap(bad.entries[0], bad.entries[1]);
		CHECK_THROWS(TurnCodec::encode(bad));
		auto good = TurnCodec::encode(sampleBundle());
		auto outOfRange = good;
		outOfRange[4] = 13; // fromTick 10 -> 13, above the first entry
		CHECK_FALSE(TurnCodec::decode(outOfRange));
		auto badSeat = good;
		badSeat[11 + 4] = 40; // first entry's seat
		CHECK_FALSE(TurnCodec::decode(badSeat));
		auto backwards = good;
		backwards[8] = 9; // horizon 14 -> 9, below fromTick
		CHECK_FALSE(TurnCodec::decode(backwards));
		auto lyingCount = good;
		lyingCount[10] = 0xFF;
		CHECK_FALSE(TurnCodec::decode(lyingCount));

		// Enumerations and Welcome consistency.
		const std::vector<std::uint8_t> badReject = {MSG_REJECT, 9, 0, 0, 0, 0};
		CHECK_FALSE(TurnCodec::decode(badReject));
		const std::vector<std::uint8_t> badVerdict = {MSG_DESYNC_NOTICE, 0, 0, 0, 0, 3, 0, 0, 0, 0};
		CHECK_FALSE(TurnCodec::decode(badVerdict));
		const std::vector<std::uint8_t> badQuit = {MSG_QUIT, 2};
		CHECK_FALSE(TurnCodec::decode(badQuit));
		Presence p;
		p.seats.push_back({1, PresenceState::Connected, 0, 0});
		auto pb = TurnCodec::encode(p);
		pb[3] = PRESENCE_STATE_MAX + 1;
		CHECK_FALSE(TurnCodec::decode(pb));
		Presence dup;
		dup.seats.push_back({1, PresenceState::Connected, 0, 0});
		dup.seats.push_back({1, PresenceState::Connected, 0, 0});
		CHECK_FALSE(TurnCodec::decode(TurnCodec::encode(dup)));
		Welcome w;
		w.seat = 2;
		w.humanSeatMask = 0b011;
		CHECK_FALSE(TurnCodec::decode(TurnCodec::encode(w)));
	}

	TEST_CASE("bundles split at tick boundaries under the frame limit")
	{
		std::vector<TurnEntry> entries;
		for (std::uint32_t t = 100; t < 110; ++t)
			for (std::uint8_t s = 0; s < 4; ++s)
				entries.push_back({t, s, order(72, 2000)});
		const auto bundles = splitIntoBundles(entries, 100, 112, 20000);
		REQUIRE(bundles.size() > 1);
		std::uint32_t expectedFrom = 100;
		std::vector<TurnEntry> joined;
		for (const auto& b : bundles)
		{
			CHECK(b.fromTick == expectedFrom);
			CHECK(b.payloadBytes() <= 20000);
			CHECK(TurnCodec::encode(b).size() == b.payloadBytes());
			expectedFrom = b.horizonTick;
			joined.insert(joined.end(), b.entries.begin(), b.entries.end());
			CHECK_NOTHROW(b.validate());
		}
		CHECK(expectedFrom == 112);
		CHECK(joined == entries);
		CHECK(splitIntoBundles({}, 5, 7).size() == 1);
		CHECK(splitIntoBundles({}, 7, 7).empty());
	}

	TEST_CASE("the quit order matches PlayerQuitsGameOrder's encoding")
	{
		std::uint8_t mine[5];
		encodePlayerQuitOrder(7, mine);
		Uint8 engine[5] = {ORDER_PLAYER_QUIT_GAME};
		addUint32(engine, 7, 1);
		CHECK(std::memcmp(mine, engine, 5) == 0);
	}
}

TEST_SUITE("TurnSequencer")
{
	TEST_CASE("bundles every two ticks from match start, empty or not")
	{
		RelayFixture f(0b1);
		f.join(1, 0);
		f.out.inbox.clear();
		f.atTick(0);
		CHECK(f.out.take<TurnBundle>(1, MSG_TURN_BUNDLE).empty());
		f.atTick(1);
		auto first = f.out.take<TurnBundle>(1, MSG_TURN_BUNDLE);
		REQUIRE(first.size() == 1);
		CHECK(first[0]->fromTick == 0);
		CHECK(first[0]->horizonTick == 2);
		CHECK(first[0]->entries.empty());
		f.atTick(2);
		CHECK(f.out.take<TurnBundle>(1, MSG_TURN_BUNDLE).empty());
		f.atTick(3);
		auto second = f.out.take<TurnBundle>(1, MSG_TURN_BUNDLE);
		REQUIRE(second.size() == 1);
		CHECK(second[0]->fromTick == 2);
		CHECK(second[0]->horizonTick == 4);
		// A stalled host catches up with one bundle covering the gap.
		f.atTick(20);
		auto gap = f.out.take<TurnBundle>(1, MSG_TURN_BUNDLE);
		REQUIRE(gap.size() == 1);
		CHECK(gap[0]->fromTick == 4);
		CHECK(gap[0]->horizonTick == 21);
		CHECK(f.relay.horizon() == 21);
	}

	TEST_CASE("orders execute the tick after arrival, one per seat per tick")
	{
		RelayFixture f(0b11);
		f.join(1, 0);
		f.join(2, 1);
		f.atTick(10, 5 * MS);
		const std::uint32_t r = f.relay.relayTick(f.now);
		REQUIRE(r == 10);
		for (std::uint32_t i = 1; i <= 4; ++i)
			f.submit(1, i, order(20, 6, static_cast<std::uint8_t>(i)));
		f.submit(2, 1, order(20, 6, 9));
		f.out.inbox[1].clear();
		f.atTick(20);
		const auto entries = f.out.bundleEntries(1);
		REQUIRE(entries.size() == 5);
		std::vector<std::pair<std::uint32_t, int>> got;
		for (const auto& e : entries)
			got.push_back({e.tick, e.seat});
		const std::vector<std::pair<std::uint32_t, int>> want = {{11, 0}, {11, 1}, {12, 0}, {13, 0}, {14, 0}};
		CHECK(got == want);
		CHECK(entries[0].order[1] == 1);
		CHECK(entries[4].order[1] == 4);
		// Never into a tick already broadcast.
		f.submit(1, 5, order(20, 6));
		f.atTick(23);
		const auto later = f.out.bundleEntries(1);
		REQUIRE(later.size() == 1);
		CHECK(later[0].tick >= 21);
	}

	TEST_CASE("a full tick spills orders to the next one")
	{
		SequencerConfig config;
		config.tickByteBudget = 5000;
		RelayFixture f(0b111, config);
		f.join(1, 0);
		f.join(2, 1);
		f.join(3, 2);
		f.atTick(5);
		f.submit(1, 1, order(72, 2000));
		f.submit(2, 1, order(72, 2000));
		f.submit(3, 1, order(72, 2000));
		f.out.inbox[1].clear();
		f.atTick(12);
		const auto entries = f.out.bundleEntries(1);
		REQUIRE(entries.size() == 3);
		CHECK(entries[0].tick == 6);
		CHECK(entries[1].tick == 6);
		CHECK(entries[2].tick == 7);
	}

	TEST_CASE("duplicates, nulls and latency adjustments are dropped; floods are refused")
	{
		RelayFixture f(0b11);
		f.join(1, 0);
		f.join(2, 1);
		f.atTick(3);
		f.submit(1, 1, order(20, 4));
		f.submit(1, 1, order(20, 4)); // resubmitted after a reconnect
		f.submit(1, 2, {ORDER_TYPE_NULL});
		f.submit(1, 3, {ORDER_TYPE_ADJUST_LATENCY, 0, 1});
		std::vector<std::uint8_t> quitOther(5);
		encodePlayerQuitOrder(1, quitOther.data());
		f.submit(1, 4, quitOther); // seat 0 cannot quit seat 1
		f.out.inbox[1].clear();
		f.atTick(10);
		CHECK(f.out.bundleEntries(1).size() == 1);
		CHECK(f.relay.presence(1) == PresenceState::Connected);

		for (std::uint32_t i = 0; i < 300 && !f.out.wasClosed(2); ++i)
			f.submit(2, 10 + i, order(20, 4));
		CHECK(f.out.wasClosed(2));
		auto rejects = f.out.take<Reject>(2, MSG_REJECT);
		REQUIRE(rejects.size() == 1);
		CHECK(rejects[0]->reason == RejectReason::Flooding);
	}

	TEST_CASE("admission: tickets, versions, first message and seat takeover")
	{
		RelayFixture f(0b011);
		f.join(1, 2); // seat 2 is not a human seat
		CHECK(f.out.take<Reject>(1, MSG_REJECT).at(0)->reason == RejectReason::BadTicket);
		f.relay.onConnect(2, f.now);
		Hello old;
		old.protocolVersion = PROTOCOL_VERSION + 1;
		old.ticket = "seat:0";
		f.raw(2, old);
		CHECK(f.out.take<Reject>(2, MSG_REJECT).at(0)->reason == RejectReason::ProtocolVersion);
		f.relay.onConnect(3, f.now);
		f.raw(3, Ping());
		CHECK(f.out.take<Reject>(3, MSG_REJECT).at(0)->reason == RejectReason::Malformed);
		f.relay.onConnect(4, f.now);
		const std::vector<std::uint8_t> garbage = {MSG_ORDER_SUBMIT, 1};
		f.relay.onReceive(4, garbage, f.now);
		CHECK(f.out.wasClosed(4));

		f.join(5, 0);
		auto welcome = f.out.take<Welcome>(5, MSG_WELCOME);
		REQUIRE(welcome.size() == 1);
		CHECK(welcome[0]->seat == 0);
		CHECK(welcome[0]->humanSeatMask == 0b011);
		CHECK(welcome[0]->graceTicks == 4500);
		// The same ticket on a new connection replaces the old one.
		f.join(6, 0);
		CHECK(f.out.wasClosed(5));
		CHECK(f.relay.presence(0) == PresenceState::Connected);
		f.relay.onDisconnect(5, f.now); // a late close of the replaced socket changes nothing
		CHECK(f.relay.presence(0) == PresenceState::Connected);
	}

	TEST_CASE("resume serves the log from the client's horizon, or from zero")
	{
		RelayFixture f(0b1);
		f.join(1, 0);
		f.atTick(2);
		f.submit(1, 1, order(20, 4));
		f.atTick(8);
		f.submit(1, 2, order(72, 40)); // voice: broadcast but not logged
		f.submit(1, 3, order(20, 5));
		f.atTick(14);
		REQUIRE(f.relay.horizon() == 15);
		const auto live = f.out.bundleEntries(1);
		CHECK(live.size() == 3);
		CHECK(f.relay.turnLog().size() == 2);
		f.relay.onDisconnect(1, f.now);
		CHECK(f.relay.presence(0) == PresenceState::Reconnecting);

		f.join(2, 0, 9);
		auto w = f.out.take<Welcome>(2, MSG_WELCOME);
		CHECK(w.at(0)->resumeFromTick == 9);
		std::uint32_t horizon = 0;
		auto resumed = f.out.bundleEntries(2, &horizon);
		CHECK(horizon == 15);
		REQUIRE(resumed.size() == 1);
		CHECK(resumed[0].order[0] == 20);
		CHECK(resumed[0].order.size() == 5);

		f.relay.onDisconnect(2, f.now);
		f.join(3, 0, 999); // beyond our horizon: start over
		CHECK(f.out.take<Welcome>(3, MSG_WELCOME).at(0)->resumeFromTick == 0);
		CHECK(f.out.bundleEntries(3) == f.relay.turnLog());
	}

	TEST_CASE("grace expiry and explicit quits sequence PlayerQuitsGameOrder")
	{
		SequencerConfig config;
		config.graceMicros = 10 * 1000 * MS;
		RelayFixture f(0b111, config);
		f.join(1, 0);
		f.join(2, 1);
		// Seat 2 never connects: it is quit when the grace period from match start ends.
		f.at(9 * 1000 * MS);
		CHECK(f.relay.presence(2) == PresenceState::NotConnected);
		f.at(10 * 1000 * MS);
		CHECK(f.relay.presence(2) == PresenceState::Left);
		// Seat 1 drops, returns within grace, drops again and stays away.
		f.relay.onDisconnect(2, f.now);
		f.at(15 * 1000 * MS);
		f.join(3, 1, f.relay.horizon());
		CHECK(f.relay.presence(1) == PresenceState::Connected);
		f.relay.onDisconnect(3, f.now);
		f.at(24 * 1000 * MS);
		CHECK(f.relay.presence(1) == PresenceState::Reconnecting);
		f.at(25 * 1000 * MS + 1);
		CHECK(f.relay.presence(1) == PresenceState::Left);
		f.join(4, 1);
		CHECK(f.out.take<Reject>(4, MSG_REJECT).at(0)->reason == RejectReason::SeatLeft);

		f.at(26 * 1000 * MS);
		std::vector<TurnEntry> quits;
		for (const auto& e : f.relay.turnLog())
			if (e.order[0] == ORDER_TYPE_PLAYER_QUIT)
				quits.push_back(e);
		REQUIRE(quits.size() == 2);
		CHECK(quits[0].seat == 2);
		CHECK(quits[0].tick == 251); // the tick after expiry at relay tick 250
		CHECK(quits[1].seat == 1);
		std::uint8_t expected[5];
		encodePlayerQuitOrder(1, expected);
		CHECK(std::equal(expected, expected + 5, quits[1].order.begin()));

		// The last seat quits by order; the match is over and its record complete.
		CHECK_FALSE(f.relay.matchOver());
		std::vector<std::uint8_t> quitSelf(5);
		encodePlayerQuitOrder(0, quitSelf.data());
		f.submit(1, 1, quitSelf);
		CHECK(f.out.wasClosed(1));
		CHECK(f.relay.matchOver());
		CHECK(f.relay.turnLog().back().seat == 0);
		const auto record = f.relay.buildRecord("m", "v", "{}", {});
		CHECK((record.flags & MatchRecord::FLAG_INCOMPLETE) == 0);
		CHECK(record.endTick > record.turns.back().tick);
		std::size_t leftByGrace = 0;
		for (const auto& e : record.events)
			leftByGrace += e.kind == MatchEventKind::LeftByGrace;
		CHECK(leftByGrace == 2);
	}

	TEST_CASE("an explicit Quit message leaves at once")
	{
		RelayFixture f(0b11);
		f.join(1, 0);
		f.join(2, 1);
		f.atTick(4);
		f.raw(2, Quit());
		CHECK(f.relay.presence(1) == PresenceState::Left);
		CHECK(f.out.wasClosed(2));
		f.atTick(10);
		REQUIRE(f.relay.turnLog().size() == 1);
		CHECK(f.relay.turnLog()[0].tick == 5);
		CHECK(f.relay.turnLog()[0].seat == 1);
	}

	TEST_CASE("a decided game ends when nobody is connected, without waiting out grace")
	{
		SequencerConfig config;
		config.graceMicros = 180 * 1000 * MS;
		// The guest closed its window without a Quit, then the host leaves the results
		// screen with Quit(GameFinished): the match ends at once.
		{
			RelayFixture f(0b11, config);
			f.join(1, 0);
			f.join(2, 1);
			f.atTick(100);
			f.relay.onDisconnect(2, f.now);
			CHECK(f.relay.presence(1) == PresenceState::Reconnecting);
			f.atTick(120);
			Quit finished;
			finished.reason = QuitReason::GameFinished;
			f.raw(1, finished);
			CHECK(f.relay.gameDecided());
			CHECK(f.relay.matchOver());
			CHECK(f.relay.presence(0) == PresenceState::Left);
			CHECK(f.relay.presence(1) == PresenceState::Left);
			const auto record = f.relay.buildRecord("m", "v", "{}", {});
			CHECK((record.flags & MatchRecord::FLAG_INCOMPLETE) == 0);
			std::size_t byGrace = 0;
			for (const auto& e : record.events)
				byGrace += e.kind == MatchEventKind::LeftByGrace && e.seat == 1;
			CHECK(byGrace == 1);
		}
		// The other order: the winner leaves first while the guest still watches the end;
		// the guest's later disconnect ends the match.
		{
			RelayFixture f(0b11, config);
			f.join(1, 0);
			f.join(2, 1);
			f.atTick(100);
			Quit finished;
			finished.reason = QuitReason::GameFinished;
			f.raw(1, finished);
			CHECK_FALSE(f.relay.matchOver());
			f.atTick(150);
			f.relay.onDisconnect(2, f.now);
			CHECK(f.relay.matchOver());
		}
		// A plain PlayerQuit decides nothing: a disconnected seat keeps its grace.
		{
			RelayFixture f(0b11, config);
			f.join(1, 0);
			f.join(2, 1);
			f.atTick(100);
			f.relay.onDisconnect(2, f.now);
			f.raw(1, Quit());
			CHECK_FALSE(f.relay.gameDecided());
			CHECK_FALSE(f.relay.matchOver());
			CHECK(f.relay.presence(1) == PresenceState::Reconnecting);
		}
	}

	TEST_CASE("three clients: the majority wins and the minority rejoins")
	{
		RelayFixture f(0b111);
		f.join(1, 0);
		f.join(2, 1);
		f.join(3, 2);
		f.atTick(30);
		f.report(1, 25, 111);
		f.report(2, 25, 111);
		CHECK_FALSE(f.relay.agreedChecksum(25));
		f.report(3, 25, 999);
		REQUIRE(f.relay.agreedChecksum(25));
		CHECK(*f.relay.agreedChecksum(25) == 111);
		CHECK_FALSE(f.relay.desyncFlagged());
		auto notice = f.out.take<DesyncNotice>(3, MSG_DESYNC_NOTICE);
		REQUIRE(notice.size() == 1);
		CHECK(notice[0]->verdict == DesyncVerdict::Rejoin);
		CHECK(notice[0]->divergedSeatMask == 0b100);
		CHECK(f.out.take<DesyncNotice>(1, MSG_DESYNC_NOTICE).empty());
		CHECK(f.relay.presence(2) == PresenceState::Resyncing);

		// While resyncing it gets no live bundles and its reports are ignored.
		f.out.inbox[3].clear();
		f.atTick(60);
		CHECK(f.out.take<TurnBundle>(3, MSG_TURN_BUNDLE).empty());
		f.report(1, 50, 222);
		f.report(2, 50, 222);
		CHECK(f.relay.agreedChecksum(50)); // seat 2 is not awaited
		// After reloading it asks for the log from zero and replays.
		ResyncRequest rr;
		rr.fromTick = 0;
		f.raw(3, rr);
		CHECK(f.relay.presence(2) == PresenceState::Connected);
		std::uint32_t horizon = 0;
		f.out.bundleEntries(3, &horizon);
		CHECK(horizon == f.relay.horizon());
		f.report(3, 25, 111);
		f.report(3, 50, 222);
		CHECK(f.out.take<DesyncNotice>(3, MSG_DESYNC_NOTICE).empty());
		// A replay that still diverges is sent back again.
		f.report(3, 50, 223);
		CHECK(f.out.take<DesyncNotice>(3, MSG_DESYNC_NOTICE).size() == 1);

		const auto record = f.relay.buildRecord("m", "v", "{}", {});
		const std::vector<ChecksumEntry> firstReports = {{25, 0, 111}, {25, 1, 111}, {25, 2, 999}, {50, 0, 222}, {50, 1, 222}, {50, 2, 222}};
		CHECK(record.reports == firstReports);
	}

	TEST_CASE("two clients that disagree are flagged, not dropped")
	{
		RelayFixture f(0b11);
		f.join(1, 0);
		f.join(2, 1);
		f.atTick(30);
		f.report(1, 25, 1);
		f.report(2, 25, 2);
		CHECK(f.relay.desyncFlagged());
		CHECK_FALSE(f.relay.agreedChecksum(25));
		for (PeerId p : {PeerId(1), PeerId(2)})
		{
			auto n = f.out.take<DesyncNotice>(p, MSG_DESYNC_NOTICE);
			REQUIRE(n.size() == 1);
			CHECK(n[0]->verdict == DesyncVerdict::Flagged);
			CHECK(n[0]->divergedSeatMask == 0b11);
		}
		CHECK(f.relay.presence(0) == PresenceState::Connected);
		CHECK(f.relay.presence(1) == PresenceState::Connected);
		CHECK((f.relay.buildRecord("m", "v", "{}", {}).flags & MatchRecord::FLAG_DESYNC_FLAGGED) != 0);
	}

	TEST_CASE("no strict majority is flagged; a silent seat times out of arbitration")
	{
		RelayFixture f(0b1111);
		for (int s = 0; s < 4; ++s)
			f.join(PeerId(s + 1), s);
		f.atTick(30);
		f.report(1, 25, 1);
		f.report(2, 25, 1);
		f.report(3, 25, 2);
		f.report(4, 25, 2);
		CHECK(f.relay.desyncFlagged());

		RelayFixture g(0b111);
		for (int s = 0; s < 3; ++s)
			g.join(PeerId(s + 1), s);
		g.atTick(30);
		g.report(1, 25, 7);
		g.report(2, 25, 7);
		g.atTick(274);
		CHECK_FALSE(g.relay.agreedChecksum(25));
		g.atTick(276);
		REQUIRE(g.relay.agreedChecksum(25));
		CHECK(*g.relay.agreedChecksum(25) == 7);
		g.report(3, 25, 8); // late and wrong: two seats agreed, so it rejoins
		CHECK(g.out.take<DesyncNotice>(3, MSG_DESYNC_NOTICE).size() == 1);
	}

	TEST_CASE("presence reports lag, reconnecting and grace remaining")
	{
		RelayFixture f(0b11);
		f.join(1, 0);
		f.join(2, 1);
		f.atTick(100);
		Ping ping;
		ping.nonce = 1;
		ping.executedTick = 98;
		f.raw(1, ping);
		auto pong = f.out.take<Pong>(1, MSG_PONG);
		REQUIRE(pong.size() == 1);
		CHECK(pong[0]->relayTick == 100);
		f.relay.onDisconnect(2, f.now);
		f.out.inbox[1].clear();
		f.atTick(101);
		auto presence = f.out.take<Presence>(1, MSG_PRESENCE);
		REQUIRE(!presence.empty());
		const auto& seats = presence.back()->seats;
		REQUIRE(seats.size() == 2);
		CHECK(seats[0].state == PresenceState::Connected);
		CHECK(seats[0].lagTicks == 3);
		CHECK(seats[1].state == PresenceState::Reconnecting);
		CHECK(seats[1].graceRemainingTicks == 4500 - 1);
		f.atTick(200);
		CHECK(f.relay.presence(0) == PresenceState::Lagging);
	}
}

TEST_SUITE("TurnJitterBuffer")
{
	TEST_CASE("jitter is the p95 delay above the fastest delivery")
	{
		JitterEstimator j(100);
		CHECK(j.jitterMicros() == 0);
		for (int i = 0; i < 100; ++i)
			j.addOffset(50000 + (i < 95 ? 0 : 30000));
		CHECK(j.jitterMicros() == 0); // five slow samples sit above the 95th percentile
		j.addOffset(50000 + 30000);
		j.addOffset(50000 + 30000);
		CHECK(j.jitterMicros() == 30000);
		// A constant offset (latency or clock difference) is not jitter.
		JitterEstimator k(10);
		for (int i = 0; i < 10; ++i)
			k.addSample(1000000 + i * 80000, static_cast<std::uint32_t>(i * 2), 40000);
		CHECK(k.jitterMicros() == 0);
	}

	TEST_CASE("the target rises at once and falls one tick per hold period")
	{
		JitterBuffer b;
		CHECK(b.requiredTicks(0, TICK) == 2);
		CHECK(b.requiredTicks(1, TICK) == 3);
		CHECK(b.requiredTicks(130 * MS, TICK) == 6);
		CHECK(b.requiredTicks(100000 * MS, TICK) == 50);
		std::uint64_t t = 0;
		CHECK(b.update(0, TICK, t) == 2);
		CHECK(b.update(130 * MS, TICK, t += 100 * MS) == 6);
		// Jitter vanishes: hold, then one tick per 5 s.
		CHECK(b.update(0, TICK, t += 100 * MS) == 6);
		CHECK(b.update(0, TICK, t += 4900 * MS) == 6);
		CHECK(b.update(0, TICK, t += 100 * MS) == 5);
		CHECK(b.update(0, TICK, t += 4999 * MS) == 5);
		CHECK(b.update(0, TICK, t += 1 * MS) == 4);
		// A burst during the hold restarts it.
		CHECK(b.update(50 * MS, TICK, t += 4 * 1000 * MS) == 4);
		CHECK(b.update(0, TICK, t += 4 * 1000 * MS) == 4);
		CHECK(b.update(0, TICK, t += 1000 * MS) == 4);
		CHECK(b.update(0, TICK, t += 4000 * MS) == 3);
		CHECK(b.update(0, TICK, t += 5000 * MS) == 2);
		CHECK(b.update(0, TICK, t += 50000 * MS) == 2);
	}

	TEST_CASE("the delay controller nudges by at most five percent and catches up")
	{
		DelayController d;
		CHECK(d.rateMultiplier(3) == 1.0);
		d.onTick(3, 3);
		CHECK(d.tickIntervalMicros(TICK, 3) == TICK);
		for (int i = 0; i < 200; ++i)
			d.onTick(12, 3);
		CHECK(d.rateMultiplier(3) == doctest::Approx(1.05));
		CHECK(d.tickIntervalMicros(TICK, 3) == 38095);
		for (int i = 0; i < 300; ++i)
			d.onTick(0, 3);
		CHECK(d.rateMultiplier(3) == doctest::Approx(0.95));
		for (int i = 0; i < 300; ++i)
			d.onTick(4, 3);
		CHECK(d.rateMultiplier(3) == doctest::Approx(1.02).epsilon(0.01));
		CHECK_FALSE(d.catchingUp());
		d.observe(28, 3);
		CHECK_FALSE(d.catchingUp());
		d.observe(29, 2);
		CHECK(d.catchingUp());
		CHECK(d.tickIntervalMicros(TICK, 2) == 0);
		d.observe(10, 2);
		CHECK(d.catchingUp());
		d.observe(4, 2);
		CHECK_FALSE(d.catchingUp());
		CHECK(d.averageBuffered() == 4.0);
	}
}

TEST_SUITE("TurnSession")
{
	TEST_CASE("executes bundled human orders and local AI orders tick by tick")
	{
		ScriptedTransport link;
		TurnSessionConfig config;
		config.ticket = "seat:1";
		TurnSession session(3, link, config, bytesOrderCodec());
		session.update(0);
		CHECK(link.connectCalls == 1);
		link.linkState = TurnTransport::State::Connected;
		session.update(1);
		auto hello = link.sentOf<Hello>(MSG_HELLO);
		REQUIRE(hello.size() == 1);
		CHECK(hello[0]->ticket == "seat:1");
		CHECK(hello[0]->haveHorizon == 0);

		Welcome w;
		w.seat = 1;
		w.humanSeatMask = 0b011; // seat 2 is an AI
		link.deliver(w);
		TurnBundle b;
		b.fromTick = 0;
		b.horizonTick = 2;
		b.entries.push_back({1, 0, order(20, 3, 5)});
		link.deliver(b);
		Presence everyone;
		everyone.seats.push_back({0, PresenceState::Connected, 0, 0});
		everyone.seats.push_back({1, PresenceState::Connected, 0, 0});
		link.deliver(everyone);
		session.update(2);
		CHECK(session.state() == TurnSession::State::Running);
		CHECK(session.localSeat() == 1);
		CHECK(session.horizon() == 2);

		session.advanceStep(0xAAAA);
		auto reports = link.sentOf<ChecksumReport>(MSG_CHECKSUM_REPORT);
		REQUIRE(reports.size() == 1);
		CHECK(reports[0]->tick == 0);
		CHECK_FALSE(session.tickReady()); // the AI seat has not pushed yet
		CHECK(session.getWaitingOnMask() == 0b100);
		CHECK(session.orderReceived(0));
		CHECK_FALSE(session.orderReceived(2));
		session.pushOrder(makeBytesOrder(order(20, 2, 9)), 2, true);
		REQUIRE(session.tickReady());
		CHECK(session.retrieveOrder(0)->getOrderType() == ORDER_NULL);
		CHECK(session.retrieveOrder(1)->getOrderType() == ORDER_NULL);
		CHECK(session.retrieveOrder(2)->getOrderType() == 20);
		CHECK(session.retrieveOrder(2)->sender == 2);
		session.clearTopOrders();
		session.advanceStep(0xBBBB);
		session.pushOrder(makeBytesOrder(order(20, 2)), 2, true);
		REQUIRE(session.tickReady());
		auto human = session.retrieveOrder(0);
		CHECK(human->getOrderType() == 20);
		CHECK(human->sender == 0);
		session.clearTopOrders();
		CHECK(session.executedTick() == 2);
		session.advanceStep(0xCCCC);
		session.pushOrder(makeBytesOrder(order(20, 2)), 2, true);
		CHECK_FALSE(session.tickReady()); // horizon reached
		CHECK(session.getWaitingOnMask() == 0b010); // nobody else is absent: it is our link
		Presence away;
		away.seats.push_back({0, PresenceState::Reconnecting, 4000, 0});
		away.seats.push_back({1, PresenceState::Connected, 0, 0});
		link.deliver(away);
		session.update(3);
		CHECK(session.getWaitingOnMask() == 0b001);
		CHECK(link.sentOf<ChecksumReport>(MSG_CHECKSUM_REPORT).empty()); // only every 25 ticks
	}

	TEST_CASE("local orders are submitted, and resubmitted after a reconnect")
	{
		ScriptedTransport link;
		TurnSessionConfig config;
		config.ticket = "seat:0";
		TurnSession session(1, link, config, bytesOrderCodec());
		link.linkState = TurnTransport::State::Connected;
		session.update(0);
		Welcome w;
		w.humanSeatMask = 1;
		link.deliver(w);
		session.update(1);
		session.addLocalOrder(std::make_shared<NullOrder>());
		session.addLocalOrder(makeBytesOrder(order(20, 4, 1)));
		session.addLocalOrder(makeBytesOrder(order(20, 4, 2)));
		session.addLocalOrder(makeBytesOrder(order(20, 4, 3)));
		auto submits = link.sentOf<OrderSubmit>(MSG_ORDER_SUBMIT);
		REQUIRE(submits.size() == 3);
		CHECK(submits[0]->clientSequence == 1);
		CHECK(submits[2]->clientSequence == 3);

		TurnBundle b;
		b.fromTick = 0;
		b.horizonTick = 6;
		link.deliver(b);
		session.update(2);
		link.sent.clear();
		// The link drops; the relay had only received the first order.
		link.linkState = TurnTransport::State::Disconnected;
		session.update(3);
		CHECK(session.state() == TurnSession::State::Reconnecting);
		session.addLocalOrder(makeBytesOrder(order(20, 4, 4))); // queued while down
		CHECK(link.sent.empty());
		session.update(300000);
		CHECK(link.connectCalls == 2);
		link.linkState = TurnTransport::State::Connected;
		session.update(300001);
		auto hello = link.sentOf<Hello>(MSG_HELLO);
		REQUIRE(hello.size() == 1);
		CHECK(hello[0]->haveHorizon == 6);
		Welcome again;
		again.humanSeatMask = 1;
		again.resumeFromTick = 6;
		again.lastClientSequence = 1;
		link.deliver(again);
		session.update(300002);
		submits = link.sentOf<OrderSubmit>(MSG_ORDER_SUBMIT);
		REQUIRE(submits.size() == 3);
		CHECK(submits[0]->clientSequence == 2);
		CHECK(submits[2]->clientSequence == 4);
		CHECK(submits[2]->order[1] == 4);
		CHECK(session.horizon() == 6);
		CHECK_FALSE(session.needsReload());
	}

	TEST_CASE("a gap asks for a resync, and a rejoin notice asks for a reload")
	{
		ScriptedTransport link;
		TurnSessionConfig config;
		config.ticket = "seat:0";
		TurnSession session(1, link, config, bytesOrderCodec());
		link.linkState = TurnTransport::State::Connected;
		session.update(0);
		Welcome w;
		w.humanSeatMask = 1;
		link.deliver(w);
		TurnBundle b;
		b.fromTick = 0;
		b.horizonTick = 4;
		link.deliver(b);
		TurnBundle gap;
		gap.fromTick = 6;
		gap.horizonTick = 8;
		link.deliver(gap);
		link.deliver(gap);
		session.update(1);
		auto requests = link.sentOf<ResyncRequest>(MSG_RESYNC_REQUEST);
		REQUIRE(requests.size() == 1);
		CHECK(requests[0]->fromTick == 4);
		TurnBundle fill;
		fill.fromTick = 4;
		fill.horizonTick = 8;
		link.deliver(fill);
		session.update(2);
		CHECK(session.horizon() == 8);

		for (int i = 0; i < 5; ++i)
		{
			session.advanceStep(0);
			REQUIRE(session.tickReady());
			session.clearTopOrders();
		}
		DesyncNotice notice;
		notice.verdict = DesyncVerdict::Rejoin;
		notice.divergedSeatMask = 1;
		link.deliver(notice);
		session.update(3);
		CHECK(session.needsReload());
		CHECK(session.catchingUp());
		CHECK_FALSE(session.tickReady());
		CHECK(session.horizon() == 0);
		CHECK(link.sentOf<ResyncRequest>(MSG_RESYNC_REQUEST).empty());
		session.reloadDone();
		CHECK(session.executedTick() == 0);
		requests = link.sentOf<ResyncRequest>(MSG_RESYNC_REQUEST);
		REQUIRE(requests.size() == 1);
		CHECK(requests[0]->fromTick == 0);

		DesyncNotice flagged;
		flagged.verdict = DesyncVerdict::Flagged;
		flagged.divergedSeatMask = 3;
		link.deliver(flagged);
		session.update(4);
		CHECK(session.desyncFlagged());
		CHECK_FALSE(session.needsReload());
	}

	TEST_CASE("a reject ends the session")
	{
		ScriptedTransport link;
		TurnSession session(1, link, TurnSessionConfig(), bytesOrderCodec());
		link.linkState = TurnTransport::State::Connected;
		session.update(0);
		Reject r;
		r.reason = RejectReason::BadTicket;
		link.deliver(r);
		session.update(1);
		CHECK(session.state() == TurnSession::State::Rejected);
		CHECK(session.rejectReason() == RejectReason::BadTicket);
		CHECK(link.closeCalls == 1);
	}
}

TEST_SUITE("TurnMatchRecord")
{
	MatchRecord sampleRecord()
	{
		MatchRecord m;
		m.flags = MatchRecord::FLAG_DESYNC_FLAGGED;
		m.matchId = "match-123";
		m.simVersion = "100:49:abcdef";
		m.humanSeatMask = 0b101;
		m.endTick = 500;
		m.setupJson = R"({"seats":[{"seat":0},{"seat":2}],"seed":42})";
		for (std::size_t i = 0; i < m.mapHash.size(); ++i)
			m.mapHash[i] = static_cast<std::uint8_t>(i * 7);
		m.turns.push_back({3, 0, order(20, 29)});
		m.turns.push_back({3, 2, order(40, 3)});
		m.turns.push_back({499, 2, {67, 0, 0, 0, 2}});
		m.reports.push_back({0, 0, 1});
		m.reports.push_back({0, 2, 1});
		m.reports.push_back({25, 0, 0xFFFFFFFF});
		m.events.push_back({0, 0, MatchEventKind::Connected});
		m.events.push_back({499, 2, MatchEventKind::LeftByQuit});
		return m;
	}

	TEST_CASE("round-trips through bytes and files")
	{
		const auto m = sampleRecord();
		const auto bytes = m.serialize();
		CHECK(std::memcmp(bytes.data(), "G2MR", 4) == 0);
		CHECK(MatchRecord::parse(bytes) == m);
		glob2test::TempDir dir("turn-record");
		const auto path = dir.path / "sample.g2mr";
		m.writeFile(path.string());
		CHECK(MatchRecord::readFile(path.string()) == m);
		MatchRecord empty;
		CHECK(MatchRecord::parse(empty.serialize()) == empty);
	}

	TEST_CASE("rejects corruption, truncation, newer versions and disorder")
	{
		const auto bytes = sampleRecord().serialize();
		for (std::size_t n = 0; n < bytes.size(); n += 7)
			CHECK_THROWS_AS(MatchRecord::parse(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + n)), MatchRecordError);
		auto flipped = bytes;
		flipped[40] ^= 1;
		CHECK_THROWS_AS(MatchRecord::parse(flipped), MatchRecordError);
		auto newer = bytes;
		newer[5] = 2;
		CHECK_THROWS_WITH_AS(MatchRecord::parse(newer), doctest::Contains("version"), MatchRecordError);
		auto magic = bytes;
		magic[0] = 'X';
		CHECK_THROWS_AS(MatchRecord::parse(magic), MatchRecordError);

		auto disordered = sampleRecord();
		std::swap(disordered.turns[0], disordered.turns[1]);
		CHECK_THROWS_AS(disordered.serialize(), MatchRecordError);
		auto pastEnd = sampleRecord();
		pastEnd.turns.back().tick = 500;
		CHECK_THROWS_AS(pastEnd.serialize(), MatchRecordError);
	}

	TEST_CASE("CRC-32 matches the IEEE check value")
	{
		const char* check = "123456789";
		CHECK(crc32(reinterpret_cast<const std::uint8_t*>(check), 9) == 0xCBF43926u);
	}
}
