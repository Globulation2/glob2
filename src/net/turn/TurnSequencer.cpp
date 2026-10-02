// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "TurnSequencer.h"

#include <algorithm>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Turn
{
TurnSequencer::TurnSequencer(SequencerConfig config, std::uint32_t humanSeatMask, Admission admission,
                             SequencerOutput& output, std::uint64_t startMicros)
	: config(config), humanMask(humanSeatMask), admission(std::move(admission)), output(output), start(startMicros),
	  now(startMicros)
{
	if (!humanMask)
		throw std::invalid_argument("A turn match needs at least one human seat");
	if (!config.tickRateMilliHz || !config.bundleInterval || !config.checksumInterval)
		throw std::invalid_argument("Invalid turn timing");
	tickPeriod = ticksToMicros(1, config.tickRateMilliHz);
	for (unsigned s = 0; s < MAX_SEATS; ++s)
		seats[s].graceStart = startMicros;
	unsigned highest = 0;
	for (unsigned s = 0; s < MAX_SEATS; ++s)
		if (humanMask & (1u << s))
			highest = s;
	net.seats.resize(highest + 1);
}

nlohmann::json TurnSequencer::networkSummary() const
{
	return sequencerSummaryJson(net, humanMask, config.tickRateMilliHz, sentHorizon);
}

void TurnSequencer::transportRoundTrip(PeerId peer, std::uint64_t micros)
{
	const auto it = peers.find(peer);
	if (it == peers.end())
		return;
	if (auto* t = net.seat(it->second.seat))
		t->rttMicros.add(micros);
}

void TurnSequencer::notePending()
{
	net.peakPendingTicks = std::max<std::uint64_t>(net.peakPendingTicks, pending.size());
	net.peakPendingEntries = std::max(net.peakPendingEntries, pendingEntries);
	net.peakPendingBytes = std::max(net.peakPendingBytes, pendingTotalBytes);
}

std::uint32_t TurnSequencer::relayTick(std::uint64_t nowMicros) const
{
	if (nowMicros <= start)
		return 0;
	return static_cast<std::uint32_t>(microsToTicks(nowMicros - start, config.tickRateMilliHz));
}

PresenceState TurnSequencer::presence(std::uint8_t seat) const
{
	if (seat >= MAX_SEATS || !(humanMask & (1u << seat)))
		return PresenceState::Left;
	return seats[seat].state;
}

std::optional<std::uint32_t> TurnSequencer::agreedChecksum(std::uint32_t tick) const
{
	auto it = reports.find(tick);
	if (it == reports.end())
		return std::nullopt;
	return it->second.agreed;
}

void TurnSequencer::send(PeerId peer, const NetMessage& message)
{
	output.send(peer, TurnCodec::encode(message));
}

void TurnSequencer::reject(PeerId peer, RejectReason reason, const std::string& detail)
{
	Reject r;
	r.reason = reason;
	r.detail = detail.substr(0, MAX_REJECT_DETAIL_BYTES);
	send(peer, r);
	++counters.peersRejected;
	++net.rejectedPeers;
	dropPeer(peer);
	output.close(peer);
}

void TurnSequencer::dropPeer(PeerId peer)
{
	auto it = peers.find(peer);
	if (it == peers.end())
		return;
	const int seat = it->second.seat;
	peers.erase(it);
	if (seat < 0)
		return;
	Seat& s = seats[seat];
	if (!s.hasPeer || s.peer != peer)
		return;
	s.hasPeer = false;
	s.streaming = false;
	if (s.state != PresenceState::Left)
	{
		if (auto* t = net.seat(seat))
		{
			++t->disconnects;
			t->absent = true;
			t->graceSince = now;
		}
		s.graceStart = now;
		setState(static_cast<std::uint8_t>(seat), PresenceState::Reconnecting);
		event(static_cast<std::uint8_t>(seat), MatchEventKind::Disconnected);
	}
}

void TurnSequencer::setState(std::uint8_t seat, PresenceState state)
{
	if (seats[seat].state != state)
	{
		seats[seat].state = state;
		presenceDirty = true;
	}
}

void TurnSequencer::event(std::uint8_t seat, MatchEventKind kind)
{
	MatchEvent e;
	e.tick = relayTick(now);
	if (!events.empty() && events.back().tick > e.tick)
		e.tick = events.back().tick;
	e.seat = seat;
	e.kind = kind;
	events.push_back(e);
}

void TurnSequencer::onConnect(PeerId peer, std::uint64_t nowMicros)
{
	now = std::max(now, nowMicros);
	peers[peer] = Peer{};
}

void TurnSequencer::onDisconnect(PeerId peer, std::uint64_t nowMicros)
{
	now = std::max(now, nowMicros);
	dropPeer(peer);
}

void TurnSequencer::onReceive(PeerId peer, const std::uint8_t* data, std::size_t size, std::uint64_t nowMicros)
{
	now = std::max(now, nowMicros);
	auto it = peers.find(peer);
	if (it == peers.end())
		return;
	auto message = TurnCodec::decode(data, size);
	if (!message)
	{
		reject(peer, RejectReason::Malformed, "Malformed turn message");
		return;
	}
	const int seat = it->second.seat;
	const auto type = message->getMessageType();
	if (auto* t = net.seat(seat))
	{
		++t->framesReceived;
		t->bytesReceived += size;
	}
	if (seat < 0)
	{
		if (type != MSG_HELLO)
			reject(peer, RejectReason::Malformed, "Expected Hello");
		else
			handleHello(peer, static_cast<const Hello&>(*message), now);
		return;
	}
	switch (type)
	{
	case MSG_ORDER_SUBMIT:
		handleOrder(seat, static_cast<const OrderSubmit&>(*message), now);
		break;
	case MSG_CHECKSUM_REPORT:
		handleChecksum(seat, static_cast<const ChecksumReport&>(*message));
		break;
	case MSG_RESYNC_REQUEST:
		handleResync(peer, seat, static_cast<const ResyncRequest&>(*message).fromTick);
		break;
	case MSG_QUIT:
		sequenceQuit(static_cast<std::uint8_t>(seat), MatchEventKind::LeftByQuit, now);
		break;
	case MSG_PING:
	{
		const auto& ping = static_cast<const Ping&>(*message);
		seats[seat].executedTick = ping.executedTick;
		if (auto* t = net.seat(seat))
		{
			const std::uint32_t tickNow = relayTick(now);
			++t->pings;
			t->lagTicks.add(tickNow > ping.executedTick ? tickNow - ping.executedTick : 0);
		}
		Pong pong;
		pong.nonce = ping.nonce;
		pong.relayTick = relayTick(now);
		pong.lastClientSequence = seats[seat].lastClientSequence;
		send(peer, pong);
		break;
	}
	default:
		// Relay-to-client messages, or a second Hello.
		reject(peer, RejectReason::Malformed, "Unexpected turn message");
		break;
	}
}

void TurnSequencer::handleHello(PeerId peer, const Hello& hello, std::uint64_t)
{
	if (hello.protocolVersion != PROTOCOL_VERSION)
		return reject(peer, RejectReason::ProtocolVersion, "Turn protocol version mismatch");
	if (over)
		return reject(peer, RejectReason::MatchOver, "The match is over");
	const int seat = admission ? admission(hello.ticket) : -1;
	if (seat < 0 || seat >= static_cast<int>(MAX_SEATS) || !(humanMask & (1u << seat)))
		return reject(peer, RejectReason::BadTicket, "Ticket refused");
	Seat& s = seats[seat];
	if (s.state == PresenceState::Left)
		return reject(peer, RejectReason::SeatLeft, "This seat has left the match");
	// A valid ticket for a seat that still has a connection replaces it: the relay often
	// learns of a dead socket only after the client has reconnected.
	if (s.hasPeer && s.peer != peer)
	{
		const PeerId old = s.peer;
		peers.erase(old);
		s.hasPeer = false;
		output.close(old);
	}
	peers[peer].seat = seat;
	s.peer = peer;
	s.hasPeer = true;
	s.streaming = true;
	if (auto* t = net.seat(seat))
	{
		++t->connects;
		if (t->absent)
		{
			const std::uint64_t d = now > t->graceSince ? now - t->graceSince : 0;
			t->graceMicrosTotal += d;
			t->graceMicrosMax = std::max(t->graceMicrosMax, d);
			t->absent = false;
		}
	}

	std::uint32_t resumeFrom = hello.haveHorizon <= sentHorizon ? hello.haveHorizon : 0;
	if (s.awaitingResync && resumeFrom != 0)
		resumeFrom = 0;
	if (s.awaitingResync)
	{
		s.awaitingResync = false;
		event(static_cast<std::uint8_t>(seat), MatchEventKind::Resynced);
	}
	const std::uint32_t tickNow = relayTick(now);
	s.executedTick = resumeFrom;
	setState(static_cast<std::uint8_t>(seat), PresenceState::Connected);
	event(static_cast<std::uint8_t>(seat), MatchEventKind::Connected);

	Welcome w;
	w.seat = static_cast<std::uint8_t>(seat);
	w.humanSeatMask = humanMask;
	w.tickRateMilliHz = config.tickRateMilliHz;
	w.bundleInterval = config.bundleInterval;
	w.checksumInterval = config.checksumInterval;
	w.relayTick = tickNow;
	w.resumeFromTick = resumeFrom;
	w.graceTicks = static_cast<std::uint32_t>(config.graceMicros / tickPeriod);
	w.lastClientSequence = s.lastClientSequence;
	send(peer, w);
	send(peer, presenceSnapshot());
	sendLog(peer, resumeFrom);
}

void TurnSequencer::sendLog(PeerId peer, std::uint32_t fromTick)
{
	if (fromTick >= sentHorizon)
		return;
	auto first = std::lower_bound(log.begin(), log.end(), fromTick,
	                              [](const TurnEntry& e, std::uint32_t tick) { return e.tick < tick; });
	std::vector<TurnEntry> slice(first, log.end());
	auto it = peers.find(peer);
	auto* t = it != peers.end() ? net.seat(it->second.seat) : nullptr;
	for (const auto& bundle : splitIntoBundles(slice, fromTick, sentHorizon))
	{
		const auto payload = TurnCodec::encode(bundle);
		output.send(peer, payload);
		++counters.bundlesSent;
		if (t)
		{
			++t->logBundlesSent;
			t->logBundleBytes += payload.size();
		}
	}
}

bool TurnSequencer::assign(std::uint8_t seat, std::vector<std::uint8_t> order, std::uint32_t currentTick, bool floodLimit)
{
	Seat& s = seats[seat];
	// The earliest tick no client has been authorized to run. The relay's own clock
	// does not matter: only the horizon it has published binds anyone.
	std::uint32_t tick = std::max(sentHorizon, s.nextFreeTick);
	while (pendingBytes[tick] + order.size() > config.tickByteBudget)
		++tick;
	if (floodLimit && tick > currentTick + config.maxAheadTicks)
		return false;
	pendingBytes[tick] += order.size();
	if (auto* t = net.seat(seat))
	{
		if (!order.empty() && order[0] == ORDER_TYPE_VOICE)
		{
			++t->voiceSequenced;
			t->voiceBytes += order.size();
		}
		else
		{
			++t->ordersSequenced;
			t->orderBytes += order.size();
		}
		// The earliest tick an order can get is the first unbroadcast one.
		if (tick > sentHorizon)
		{
			++t->ordersDeferred;
			t->deferTicks.add(tick - sentHorizon);
		}
		// How far the seat's next free tick runs ahead of the relay clock.
		const std::uint32_t nextFree = tick + 1;
		t->maxQueuedAhead = std::max<std::uint64_t>(t->maxQueuedAhead, nextFree > currentTick ? nextFree - currentTick : 0);
	}
	++pendingEntries;
	pendingTotalBytes += order.size();
	auto& slot = pending[tick];
	TurnEntry entry;
	entry.tick = tick;
	entry.seat = seat;
	entry.order = std::move(order);
	auto pos = std::lower_bound(slot.begin(), slot.end(), seat,
	                            [](const TurnEntry& e, std::uint8_t sv) { return e.seat < sv; });
	slot.insert(pos, std::move(entry));
	s.nextFreeTick = tick + 1;
	++counters.ordersSequenced;
	notePending();
	return true;
}

void TurnSequencer::handleOrder(int seat, const OrderSubmit& submit, std::uint64_t)
{
	Seat& s = seats[seat];
	if (s.state == PresenceState::Left || over)
		return;
	auto* t = net.seat(seat);
	if (submit.clientSequence <= s.lastClientSequence)
	{
		if (t)
			++t->duplicatesIgnored;
		return; // A resubmission after reconnect that we already sequenced.
	}
	s.lastClientSequence = submit.clientSequence;
	const std::uint8_t type = submit.order[0];
	if (type == ORDER_TYPE_NULL || type == ORDER_TYPE_ADJUST_LATENCY)
	{
		++counters.ordersDropped;
		if (t)
			++t->ordersDropped;
		return;
	}
	if (type == ORDER_TYPE_PLAYER_QUIT)
	{
		std::uint8_t expected[5];
		encodePlayerQuitOrder(static_cast<std::uint8_t>(seat), expected);
		if (submit.order.size() != 5 || !std::equal(expected, expected + 5, submit.order.begin()))
		{
			++counters.ordersDropped;
			if (t)
				++t->ordersDropped;
			return;
		}
		sequenceQuit(static_cast<std::uint8_t>(seat), MatchEventKind::LeftByQuit, now);
		return;
	}
	if (!assign(static_cast<std::uint8_t>(seat), submit.order, relayTick(now)))
	{
		++counters.ordersDropped;
		if (t)
		{
			++t->ordersDropped;
			++t->floodRejections;
		}
		reject(s.peer, RejectReason::Flooding, "Too many orders queued");
	}
	else if (onSequenced)
		onSequenced(static_cast<std::uint8_t>(seat), submit.clientSequence, s.nextFreeTick - 1, relayTick(now));
}

void TurnSequencer::sequenceQuit(std::uint8_t seat, MatchEventKind why, std::uint64_t)
{
	Seat& s = seats[seat];
	if (s.state == PresenceState::Left)
		return;
	std::vector<std::uint8_t> order(5);
	encodePlayerQuitOrder(seat, order.data());
	// The flood limit never applies to the relay's own quit order.
	assign(seat, std::move(order), relayTick(now), false);
	s.awaitingResync = false;
	if (auto* t = net.seat(seat))
	{
		t->leftTick = relayTick(now);
		t->leftByGrace = why == MatchEventKind::LeftByGrace;
		t->leftByQuit = why == MatchEventKind::LeftByQuit;
		if (t->absent)
		{
			const std::uint64_t d = now > t->graceSince ? now - t->graceSince : 0;
			t->graceMicrosTotal += d;
			t->graceMicrosMax = std::max(t->graceMicrosMax, d);
			t->absent = false;
		}
	}
	setState(seat, PresenceState::Left);
	event(seat, why);
	if (s.hasPeer)
	{
		const PeerId peer = s.peer;
		s.hasPeer = false;
		s.streaming = false;
		peers.erase(peer);
		output.close(peer);
	}
	bool anyone = false;
	for (unsigned i = 0; i < MAX_SEATS; ++i)
		if ((humanMask & (1u << i)) && seats[i].state != PresenceState::Left)
			anyone = true;
	if (!anyone)
		finish(now);
}

void TurnSequencer::emitUpTo(std::uint32_t newHorizon)
{
	if (newHorizon <= sentHorizon)
		return;
	std::vector<TurnEntry> entries;
	while (!pending.empty() && pending.begin()->first < newHorizon)
	{
		for (auto& e : pending.begin()->second)
		{
			pendingTotalBytes -= std::min<std::uint64_t>(pendingTotalBytes, e.order.size());
			if (pendingEntries)
				--pendingEntries;
			entries.push_back(std::move(e));
		}
		pendingBytes.erase(pending.begin()->first);
		pending.erase(pending.begin());
	}
	const auto bundles = splitIntoBundles(entries, sentHorizon, newHorizon);
	for (auto& e : entries)
		if (e.order[0] != ORDER_TYPE_VOICE)
			log.push_back(e);
	const std::uint32_t fromTick = sentHorizon;
	sentHorizon = newHorizon;
	if (onEmitted)
		onEmitted(fromTick, newHorizon);
	for (const auto& bundle : bundles)
	{
		const auto payload = TurnCodec::encode(bundle);
		++net.bundlesBroadcast;
		net.bundleBytesBroadcast += payload.size();
		for (unsigned i = 0; i < MAX_SEATS; ++i)
			if (seats[i].hasPeer && seats[i].streaming)
			{
				output.send(seats[i].peer, payload);
				++counters.bundlesSent;
				if (auto* t = net.seat(static_cast<int>(i)))
				{
					++t->bundlesSent;
					t->bundleBytes += payload.size();
				}
			}
	}
}

void TurnSequencer::handleChecksum(int seat, const ChecksumReport& report)
{
	Seat& s = seats[seat];
	if (s.awaitingResync || s.state == PresenceState::Left)
		return;
	if (report.tick % config.checksumInterval != 0 || report.tick > sentHorizon)
		return;
	if (auto* t = net.seat(seat))
	{
		const std::uint32_t tickNow = relayTick(now);
		++t->checksumReports;
		t->reportLatenessTicks.add(tickNow > report.tick ? tickNow - report.tick : 0);
	}
	TickReports& tr = reports[report.tick];
	const auto key = static_cast<std::uint8_t>(seat);
	tr.first.emplace(key, report.checksum);
	if (!tr.arbitrated)
	{
		tr.current[key] = report.checksum;
		bool complete = true;
		for (unsigned i = 0; i < MAX_SEATS; ++i)
			if ((humanMask & (1u << i)) && expectedReporter(seats[i]) && !tr.current.count(static_cast<std::uint8_t>(i)))
				complete = false;
		if (complete)
			arbitrate(report.tick);
		return;
	}
	// A late report, typically replayed after a reconnect or rejoin.
	if (tr.agreed && *tr.agreed != report.checksum)
	{
		if (auto* t = net.seat(seat))
			++t->lateMismatches;
		if (tr.support >= 2)
			tellRejoin(key, report.tick);
		else
			flag(report.tick, 1u << seat);
	}
}

bool TurnSequencer::expectedReporter(const Seat& s) const
{
	return s.hasPeer && !s.awaitingResync &&
	       (s.state == PresenceState::Connected || s.state == PresenceState::Lagging);
}

void TurnSequencer::arbitrate(std::uint32_t tick, bool timedOut)
{
	TickReports& tr = reports[tick];
	if (tr.arbitrated)
		return;
	tr.arbitrated = true;
	const std::size_t n = tr.current.size();
	if (n == 0)
		return;
	++net.arbitrations;
	if (timedOut)
		++net.timedOut;
	std::map<std::uint32_t, std::uint32_t> counts;
	for (const auto& r : tr.current)
		++counts[r.second];
	auto best = std::max_element(counts.begin(), counts.end(),
	                             [](const auto& a, const auto& b) { return a.second < b.second; });
	if (counts.size() == 1)
	{
		tr.agreed = best->first;
		tr.support = static_cast<std::uint32_t>(n);
		++net.unanimous;
		return;
	}
	std::uint32_t reporters = 0;
	for (const auto& r : tr.current)
		reporters |= 1u << r.first;
	if (n >= 3 && best->second * 2 > n)
	{
		tr.agreed = best->first;
		tr.support = best->second;
		++net.majority;
		for (const auto& r : tr.current)
			if (r.second != best->first)
				tellRejoin(r.first, tick);
		return;
	}
	++net.flaggedTicks;
	flag(tick, reporters);
}

void TurnSequencer::tellRejoin(std::uint8_t seat, std::uint32_t tick)
{
	Seat& s = seats[seat];
	if (s.awaitingResync || s.state == PresenceState::Left)
		return;
	if (s.rejoins >= config.maxRejoins)
	{
		flag(tick, 1u << seat);
		return;
	}
	++s.rejoins;
	if (auto* t = net.seat(seat))
		++t->toldToRejoin;
	s.awaitingResync = true;
	s.streaming = false;
	for (auto& r : reports)
		if (!r.second.arbitrated)
			r.second.current.erase(seat);
	setState(seat, PresenceState::Resyncing);
	event(seat, MatchEventKind::ToldToRejoin);
	if (s.hasPeer)
	{
		DesyncNotice notice;
		notice.tick = tick;
		notice.verdict = DesyncVerdict::Rejoin;
		notice.divergedSeatMask = 1u << seat;
		send(s.peer, notice);
	}
}

void TurnSequencer::flag(std::uint32_t tick, std::uint32_t seatMask)
{
	flagged = true;
	DesyncNotice notice;
	notice.tick = tick;
	notice.verdict = DesyncVerdict::Flagged;
	notice.divergedSeatMask = seatMask;
	for (unsigned i = 0; i < MAX_SEATS; ++i)
		if (seatMask & (1u << i))
		{
			if (auto* t = net.seat(static_cast<int>(i)))
				++t->flagged;
			event(static_cast<std::uint8_t>(i), MatchEventKind::Flagged);
			if (seats[i].hasPeer)
				send(seats[i].peer, notice);
		}
}

void TurnSequencer::handleResync(PeerId peer, int seat, std::uint32_t fromTick)
{
	Seat& s = seats[seat];
	if (fromTick > sentHorizon)
		return reject(peer, RejectReason::Malformed, "Resync beyond the horizon");
	if (s.awaitingResync)
	{
		if (fromTick != 0)
			return; // It must reload first; a stale request from before the notice.
		s.awaitingResync = false;
		setState(static_cast<std::uint8_t>(seat), PresenceState::Connected);
		event(static_cast<std::uint8_t>(seat), MatchEventKind::Resynced);
	}
	s.streaming = true;
	sendLog(peer, fromTick);
}

Presence TurnSequencer::presenceSnapshot() const
{
	Presence p;
	for (unsigned i = 0; i < MAX_SEATS; ++i)
	{
		if (!(humanMask & (1u << i)))
			continue;
		const Seat& s = seats[i];
		SeatPresence sp;
		sp.seat = static_cast<std::uint8_t>(i);
		sp.state = s.state;
		if (s.state == PresenceState::NotConnected || s.state == PresenceState::Reconnecting)
		{
			const std::uint64_t elapsed = now > s.graceStart ? now - s.graceStart : 0;
			const std::uint64_t left = elapsed < config.graceMicros ? config.graceMicros - elapsed : 0;
			sp.graceRemainingTicks = static_cast<std::uint32_t>(left / tickPeriod);
		}
		const std::uint32_t tick = relayTick(now);
		sp.lagTicks = tick > s.executedTick ? tick - s.executedTick : 0;
		p.seats.push_back(sp);
	}
	return p;
}

void TurnSequencer::broadcastPresence()
{
	const auto payload = TurnCodec::encode(presenceSnapshot());
	for (unsigned i = 0; i < MAX_SEATS; ++i)
		if (seats[i].hasPeer)
			output.send(seats[i].peer, payload);
	presenceDirty = false;
	lastPresenceTick = relayTick(now);
}

void TurnSequencer::update(std::uint64_t nowMicros)
{
	now = std::max(now, nowMicros);
	if (over)
		return;
	const std::uint32_t tick = relayTick(now);

	for (unsigned i = 0; i < MAX_SEATS && !over; ++i)
	{
		if (!(humanMask & (1u << i)))
			continue;
		Seat& s = seats[i];
		if ((s.state == PresenceState::NotConnected || s.state == PresenceState::Reconnecting) &&
		    now - s.graceStart >= config.graceMicros)
			sequenceQuit(static_cast<std::uint8_t>(i), MatchEventKind::LeftByGrace, now);
		else if (s.state == PresenceState::Connected || s.state == PresenceState::Lagging)
		{
			const bool lagging = tick > s.executedTick && tick - s.executedTick > config.lagThresholdTicks;
			setState(static_cast<std::uint8_t>(i), lagging ? PresenceState::Lagging : PresenceState::Connected);
		}
	}
	if (over)
		return;

	if (tick + 1 >= sentHorizon + config.bundleInterval)
		emitUpTo(tick + 1);

	for (auto& r : reports)
	{
		if (r.first + config.arbitrationTimeoutTicks > tick)
			break;
		if (!r.second.arbitrated)
			arbitrate(r.first, true);
	}

	if (presenceDirty || tick >= lastPresenceTick + config.presenceRefreshTicks)
		broadcastPresence();
}

void TurnSequencer::finish(std::uint64_t nowMicros)
{
	now = std::max(now, nowMicros);
	if (over)
		return;
	bool everyoneLeft = true;
	for (unsigned i = 0; i < MAX_SEATS; ++i)
		if ((humanMask & (1u << i)) && seats[i].state != PresenceState::Left)
			everyoneLeft = false;
	incomplete = !everyoneLeft;
	std::uint32_t end = relayTick(now) + 1;
	if (!pending.empty())
		end = std::max(end, pending.rbegin()->first + 1);
	emitUpTo(end);
	for (auto& r : reports)
		if (!r.second.arbitrated)
			arbitrate(r.first);
	over = true;
	broadcastPresence();
}

MatchRecord TurnSequencer::buildRecord(const std::string& matchId, const std::string& simVersion,
                                       const std::string& setupJson, const std::array<std::uint8_t, 32>& mapHash) const
{
	MatchRecord m;
	m.flags = (flagged ? MatchRecord::FLAG_DESYNC_FLAGGED : 0) | ((incomplete || !over) ? MatchRecord::FLAG_INCOMPLETE : 0);
	m.matchId = matchId;
	m.simVersion = simVersion;
	m.tickRateMilliHz = config.tickRateMilliHz;
	m.bundleInterval = config.bundleInterval;
	m.checksumInterval = config.checksumInterval;
	m.humanSeatMask = humanMask;
	m.endTick = sentHorizon;
	m.setupJson = setupJson;
	m.mapHash = mapHash;
	m.turns = log;
	for (const auto& r : reports)
		for (const auto& f : r.second.first)
		{
			ChecksumEntry c;
			c.tick = r.first;
			c.seat = f.first;
			c.checksum = f.second;
			m.reports.push_back(c);
		}
	m.events = events;
	return m;
}
}
