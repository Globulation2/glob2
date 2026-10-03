// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "TurnSession.h"

#include <algorithm>
#include <iostream>

#include "NetConsts.h"
#include "Version.h"

namespace Turn
{
namespace
{
/// Coalescing key of an order whose effect is an absolute setting: of one building or
/// flag (its id is the first two bytes after the type, in every one of these
/// encodings), or of the pause state. A later queued order with the same key makes an
/// earlier unsent one redundant. 0 for orders that must all execute (creating,
/// deleting, upgrading, brush strokes, chat, alliances, map marks, quitting).
std::uint32_t coalesceKey(const std::vector<std::uint8_t>& b)
{
	if (b.empty())
		return 0;
	switch (b[0])
	{
	case ORDER_MOVE_FLAG:
	case ORDER_MODIFY_BUILDING:
	case ORDER_MODIFY_EXCHANGE:
	case ORDER_MODIFY_SWARM:
	case ORDER_MODIFY_FLAG:
	case ORDER_MODIFY_CLEARING_FLAG:
	case ORDER_MODIFY_MIN_LEVEL_TO_FLAG:
	case ORDER_CHANGE_PRIORITY:
		if (b.size() < 3)
			return 0;
		return (1u << 24) | (std::uint32_t(b[0]) << 16) | (std::uint32_t(b[1]) << 8) | b[2];
	case ORDER_PAUSE_GAME:
		return (1u << 24) | (std::uint32_t(b[0]) << 16) | 0xFFFFu;
	default:
		return 0;
	}
}

/// OrderMoveFlag's wire layout: type, gid (2), x (4), y (4), drop (1).
constexpr std::size_t MOVE_FLAG_BYTES = 12;
constexpr std::size_t MOVE_FLAG_DROP_OFFSET = 11;
}

OrderCodec defaultOrderCodec()
{
	OrderCodec codec;
	codec.encode = [](Order& order) {
		std::vector<std::uint8_t> bytes;
		const int length = order.getDataLength();
		if (length < 0)
			return bytes;
		bytes.reserve(1 + length);
		bytes.push_back(order.getOrderType());
		if (length > 0)
		{
			const Uint8* data = order.getData();
			bytes.insert(bytes.end(), data, data + length);
		}
		return bytes;
	};
	codec.decode = [](const std::uint8_t* data, std::size_t size) {
		return Order::getOrder(data, static_cast<int>(size), VERSION_MINOR);
	};
	return codec;
}

TurnSession::TurnSession(int numberOfPlayers, TurnTransport& transport, TurnSessionConfig config, OrderCodec codec)
	: numberOfPlayers(numberOfPlayers), transport(transport), config(std::move(config)), codec(std::move(codec)),
	  backoff(this->config.reconnectInitialMicros), jitter(this->config.jitterWindow), buffer(this->config.jitter),
	  delay(this->config.delay)
{
	aiOrders.resize(numberOfPlayers > 0 ? numberOfPlayers : 0);
	seatPresence.fill(PresenceState::NotConnected);
}

TurnSession::SeatPresence TurnSession::seatPresenceInfo(int seatNumber) const
{
	if (seatNumber < 0 || seatNumber >= static_cast<int>(MAX_SEATS))
		return SeatPresence{PresenceState::Left};
	SeatPresence details = seatDetails[seatNumber];
	details.state = seatPresence[seatNumber];
	details.relayRttMicros = seatRtt[seatNumber];
	return details;
}

PresenceState TurnSession::presence(int seatNumber) const
{
	if (seatNumber < 0 || seatNumber >= static_cast<int>(MAX_SEATS))
		return PresenceState::Left;
	return seatPresence[seatNumber];
}

void TurnSession::send(const NetMessage& message)
{
	// Every message leaves at once: an order queued until the next frame's poll would
	// add up to a frame to its input delay.
	const auto payload = TurnCodec::encode(message);
	stats.frameSent(payload.size());
	transport.send(payload);
	transport.flush();
}

void TurnSession::update(std::uint64_t nowMicros)
{
	now = std::max(now, nowMicros);
	stats.update(executed, now);
	if (currentState == State::Rejected || currentState == State::Ended)
		return;

	if (!connectStarted)
	{
		connectStarted = true;
		transport.connect();
	}

	const auto linkState = transport.state();
	if (linkState == TurnTransport::State::Disconnected)
	{
		if (currentState == State::Running || currentState == State::AwaitingWelcome)
		{
			stats.linkLost(now);
			currentState = State::Reconnecting;
			retryAt = now + backoff;
		}
		else if (now >= retryAt)
		{
			++attempts;
			transport.connect();
			retryAt = now + backoff;
			backoff = std::min(backoff * 2, config.reconnectMaxMicros);
		}
		helloSent = false;
		return;
	}
	if (linkState == TurnTransport::State::Connecting)
		return;

	if (!helloSent)
	{
		Hello hello;
		hello.protocolVersion = helloVersion;
		hello.ticket = config.ticket;
		hello.haveHorizon = reloadPending ? 0 : horizonTick;
		send(hello);
		helloSent = true;
		currentState = State::AwaitingWelcome;
	}

	std::vector<std::uint8_t> payload;
	while (transport.receive(payload))
	{
		// The first frame after a silence: what was in flight across the gap (a
		// pong, the backlog's arrival spread) measures the gap, not the link.
		if (currentState == State::Running && maxSeenHorizon > 0 && now - std::min(now, lastFrameAt) >= config.stallMicros)
		{
			forgetLinkHistory();
			jitterQuietUntil = now + config.recoveryQuietMicros;
		}
		lastFrameAt = now;
		stats.frameReceived(payload.size());
		auto message = TurnCodec::decode(payload);
		if (message && message->getMessageType() == MSG_TURN_BUNDLE)
			stats.bundle(payload.size(), static_cast<const TurnBundle&>(*message).entries.size());
		if (!message)
		{
			std::cerr << "Turn session: malformed message from relay; reconnecting\n";
			stats.linkLost(now);
			transport.close();
			currentState = State::Reconnecting;
			helloSent = false;
			retryAt = now + backoff;
			return;
		}
		handle(*message);
		if (currentState == State::Rejected || currentState == State::Reconnecting)
			return;
	}
	// After draining: a client that did not run for a while (a long frame, a hidden
	// tab) finds what arrived meanwhile before it judges the link.
	watchLink();
	if (currentState == State::Reconnecting)
		return;

	if (currentState == State::Running && now - lastPingAt >= config.pingIntervalMicros)
	{
		Ping ping;
		ping.nonce = ++pingNonce;
		ping.executedTick = executed;
		pingsInFlight[ping.nonce] = now;
		while (pingsInFlight.size() > 16)
			pingsInFlight.erase(pingsInFlight.begin());
		send(ping);
		lastPingAt = now;
	}
	pump();
	buffer.update(jitter.jitterMicros(), tickPeriod, now);
	delay.observe(bufferedTicks(), buffer.targetTicks());
	if (desyncRejoin && currentState == State::Running && !catchingUp())
		desyncRejoin = false;
	stats.catchUpState(catchingUp(), executed, now);
}

void TurnSession::watchLink()
{
	// Armed once the match clock runs (the first bundle): while players load the
	// relay sends only presence.
	if (currentState != State::Running || maxSeenHorizon == 0)
	{
		stallShown = false;
		return;
	}
	const std::uint64_t silence = now - std::min(now, lastFrameAt);
	const std::uint64_t starved = now - std::min(now, lastHorizonAt);
	if (silence >= config.silentReconnectMicros)
	{
		// TCP can take minutes to notice a dead path; start over now. Unacknowledged
		// orders and checksum reports go again after Welcome.
		std::cerr << "Turn session: nothing from the relay for " << silence / 1000 << " ms; reconnecting\n";
		stats.linkLost(now);
		transport.close();
		currentState = State::Reconnecting;
		helloSent = false;
		retryAt = now;
		stallShown = false;
		forgetLinkHistory();
		return;
	}
	const bool trouble = silence >= config.stallMicros || starved >= config.stallMicros;
	if (trouble && !stallShown)
	{
		stallShown = true;
		stallSince = lastHorizonAt;
	}
	else if (!trouble && stallShown)
	{
		// The horizon moves again. After a silence the receive loop has already
		// started the delay estimate over; a horizon that stalled while frames kept
		// coming leaves a burst of ticks whose spread is no link jitter either.
		stallShown = false;
		jitter.clear();
		buffer.reset();
		jitterQuietUntil = std::max(jitterQuietUntil, now + config.recoveryQuietMicros);
	}
}

void TurnSession::forgetLinkHistory()
{
	jitter.clear();
	buffer.reset();
	pingsInFlight.clear();
	// The relay's next SeatLatency measures the link again.
	seatRtt.fill(0);
	rtt = 0;
	if (maxSeenHorizon > 0)
		ownRelayRttFrom = now + config.stallMicros + config.pingIntervalMicros * 4;
}

void TurnSession::handle(const NetMessage& message)
{
	switch (message.getMessageType())
	{
	case MSG_WELCOME:
		onWelcome(static_cast<const Welcome&>(message));
		break;
	case MSG_REJECT:
		if (static_cast<const Reject&>(message).reason == RejectReason::ProtocolVersion &&
		    currentState == State::AwaitingWelcome && helloVersion > MIN_PROTOCOL_VERSION)
		{
			// An older relay: offer the oldest version this client speaks, once.
			std::cerr << "Turn session: relay refused protocol " << helloVersion << "; retrying with "
			          << MIN_PROTOCOL_VERSION << "\n";
			helloVersion = MIN_PROTOCOL_VERSION;
			transport.close();
			currentState = State::Reconnecting;
			helloSent = false;
			retryAt = now;
			break;
		}
		if (static_cast<const Reject&>(message).reason == RejectReason::Flooding)
		{
			// The relay closed us for sending too much. The seat is still ours: come
			// back after the usual backoff, which also lets its limit refill.
			std::cerr << "Turn session: relay says we sent too much; reconnecting\n";
			stats.linkLost(now);
			transport.close();
			currentState = State::Reconnecting;
			helloSent = false;
			retryAt = now + backoff;
			break;
		}
		rejection = static_cast<const Reject&>(message).reason;
		std::cerr << "Turn session: relay refused us: " << static_cast<const Reject&>(message).detail << "\n";
		currentState = State::Rejected;
		transport.close();
		break;
	case MSG_TURN_BUNDLE:
		onBundle(static_cast<const TurnBundle&>(message));
		break;
	case MSG_PRESENCE:
		for (const auto& p : static_cast<const Presence&>(message).seats)
		{
			seatPresence[p.seat] = p.state;
			seatDetails[p.seat] = {p.state, p.graceRemainingTicks, p.lagTicks, now};
			stats.presence(p.seat, p.state, now);
		}
		break;
	case MSG_SEAT_LATENCY:
		seatRtt.fill(0);
		for (const auto& p : static_cast<const SeatLatency&>(message).seats)
			if (p.seat != seat || now >= ownRelayRttFrom)
				seatRtt[p.seat] = p.rttMicros;
		break;
	case MSG_DESYNC_NOTICE:
		onDesync(static_cast<const DesyncNotice&>(message));
		break;
	case MSG_PONG:
	{
		const auto& pong = static_cast<const Pong&>(message);
		auto it = pingsInFlight.find(pong.nonce);
		if (it != pingsInFlight.end())
		{
			rtt = static_cast<std::int64_t>(now - it->second);
			pingsInFlight.erase(pingsInFlight.begin(), std::next(it));
			stats.pong(static_cast<std::uint64_t>(rtt), jitter.jitterMicros());
		}
		while (!outstanding.empty() && outstanding.front().sequence <= pong.lastClientSequence)
			outstanding.pop_front();
		break;
	}
	default:
		break; // Client-to-relay types never arrive here from a correct relay.
	}
}

void TurnSession::onWelcome(const Welcome& w)
{
	if (!supportedProtocol(w.protocolVersion) || w.protocolVersion > helloVersion || (seat >= 0 && w.seat != seat))
	{
		currentState = State::Rejected;
		rejection = RejectReason::ProtocolVersion;
		transport.close();
		return;
	}
	negotiated = w.protocolVersion;
	seat = w.seat;
	humanMask = w.humanSeatMask;
	grace = w.graceTicks;
	attempts = 0;
	tickRate = w.tickRateMilliHz;
	tickPeriod = ticksToMicros(1, tickRate);
	checksumInterval = w.checksumInterval;
	bundleInterval = w.bundleInterval;
	delay.setBundleInterval(bundleInterval);
	if (w.resumeFromTick != horizonTick)
	{
		// The relay serves the log from tick 0: drop what we hold. If we had already
		// executed ticks, the engine must reload the initial state.
		resetTurns();
		if (executed > 0)
		{
			reloadPending = true;
			reloadNeedsRequest = false;
			stats.reloadRequested();
		}
	}
	resyncOutstanding = false;
	liveThreshold = std::max(w.relayTick + 1, maxSeenHorizon);
	// A new connection: measure it afresh (a reconnect follows a gap whose
	// arrival times and pongs say nothing about the new link).
	forgetLinkHistory();
	if (maxSeenHorizon > 0)
		jitterQuietUntil = now + config.recoveryQuietMicros;
	lastFrameAt = lastHorizonAt = now;
	stallShown = false;
	nextSequence = std::max(nextSequence, w.lastClientSequence + 1);
	while (!outstanding.empty() && outstanding.front().sequence <= w.lastClientSequence)
		outstanding.pop_front();
	currentState = State::Running;
	stats.welcomed(now);
	backoff = config.reconnectInitialMicros;
	refillCredit();
	for (const auto& o : outstanding)
	{
		// Resent orders take relay ticks like new ones; the credit may go negative.
		credit -= static_cast<std::int64_t>(tickPeriod);
		OrderSubmit submit;
		submit.clientSequence = o.sequence;
		submit.order = o.bytes;
		send(submit);
		stats.orderFrameSent(true);
	}
	while (!unsentReports.empty())
	{
		send(unsentReports.front());
		unsentReports.pop_front();
	}
}

void TurnSession::resetTurns()
{
	turns.clear();
	horizonTick = 0;
}

void TurnSession::onBundle(const TurnBundle& b)
{
	const std::uint32_t threshold = std::max(liveThreshold, maxSeenHorizon);
	if (b.horizonTick > maxSeenHorizon)
		lastHorizonAt = now;
	maxSeenHorizon = std::max(maxSeenHorizon, b.horizonTick);
	if (b.fromTick != horizonTick)
	{
		if (!resyncOutstanding && !reloadPending)
		{
			ResyncRequest request;
			request.fromTick = horizonTick;
			send(request);
			stats.resyncRequested();
			resyncOutstanding = true;
		}
		return;
	}
	resyncOutstanding = false;
	for (const auto& e : b.entries)
	{
		if (e.seat != seat && !e.order.empty() && e.order[0] == ORDER_TYPE_VOICE)
			stats.voiceReceived(e.order.size());
		turns[e.tick].push_back(e);
	}
	horizonTick = b.horizonTick;
	if (onHorizon && b.horizonTick > b.fromTick)
		onHorizon(b.horizonTick);
	if (b.horizonTick > threshold && now >= jitterQuietUntil)
		jitter.addSample(static_cast<std::int64_t>(now), b.horizonTick, tickPeriod);
	delay.observe(bufferedTicks(), buffer.targetTicks());
}

void TurnSession::onDesync(const DesyncNotice& d)
{
	if (d.verdict == DesyncVerdict::Flagged)
	{
		flagged = true;
		stats.desync(true);
		return;
	}
	if (seat < 0 || !(d.divergedSeatMask & (1u << seat)))
		return;
	std::cerr << "Turn session: diverged at tick " << d.tick << "; reloading\n";
	desyncRejoin = true;
	stats.desync(false);
	stats.reloadRequested();
	resetTurns();
	reloadPending = true;
	reloadNeedsRequest = true;
}

void TurnSession::reloadDone()
{
	if (!reloadPending)
		return;
	reloadPending = false;
	executed = 0;
	step = 0;
	for (auto& q : aiOrders)
		q.clear();
	delay.reset();
	delay.startCatchUp();
	if (reloadNeedsRequest && linkUp())
	{
		ResyncRequest request;
		request.fromTick = 0;
		send(request);
		stats.resyncRequested();
		resyncOutstanding = true;
	}
	reloadNeedsRequest = false;
	stats.catchUpState(catchingUp(), executed, now);
}

void TurnSession::addLocalOrder(std::shared_ptr<Order> order)
{
	if (!order || currentState == State::Ended || currentState == State::Rejected)
		return;
	const Uint8 type = order->getOrderType();
	if (type == ORDER_TYPE_NULL || type == ORDER_TYPE_ADJUST_LATENCY)
	{
		stats.orderDropped();
		return;
	}
	auto bytes = codec.encode(*order);
	if (bytes.empty() || bytes.size() > MAX_ORDER_BYTES)
	{
		std::cerr << "Turn session: dropping an order of " << bytes.size() << " bytes\n";
		stats.orderDropped();
		return;
	}
	stats.orderSubmitted(bytes[0], bytes.size(), linkUp());
	if (bytes[0] == ORDER_TYPE_VOICE)
	{
		// Audio that waits too long is useless: keep the newest packets.
		if (voiceQueue.size() >= config.maxQueuedVoice)
			voiceQueue.pop_front();
		voiceQueue.push_back(std::move(bytes));
		pump();
		return;
	}
	stats.pendingInput(bytes.data(), bytes.size(), executed, now);
	const std::uint32_t key = coalesceKey(bytes);
	if (key)
	{
		for (auto it = queued.begin(); it != queued.end(); ++it)
			if (it->key == key)
			{
				// Latest wins, and moves to the back: everything queued between was
				// issued before it. A flag drop is kept, as the drop also refreshes the
				// flag's gradients.
				if (bytes[0] == ORDER_MOVE_FLAG && bytes.size() == MOVE_FLAG_BYTES &&
				    it->bytes.size() == MOVE_FLAG_BYTES && it->bytes[MOVE_FLAG_DROP_OFFSET])
					bytes[MOVE_FLAG_DROP_OFFSET] = 1;
				stats.orderCoalesced(it->bytes.data(), it->bytes.size());
				queued.erase(it);
				break;
			}
	}
	if (queued.size() >= config.maxQueuedOrders)
	{
		// Only a player (or a script) far beyond what the relay can sequence gets here.
		if (!queueDropped || now >= lastQueueDropAt + 2000000)
			std::cerr << "Turn session: too many queued orders; dropping the newest\n";
		stats.orderQueueDropped();
		queueDropped = true;
		lastQueueDropAt = now;
		return;
	}
	queued.push_back({key, std::move(bytes)});
	stats.queuedDepth(queued.size());
	pump();
}

void TurnSession::refillCredit()
{
	const std::int64_t period = static_cast<std::int64_t>(tickPeriod);
	const std::int64_t cap = period * std::max<std::uint32_t>(1, config.orderBurst);
	if (!creditReady)
	{
		creditReady = true;
		credit = cap;
		creditAt = now;
		return;
	}
	if (now > creditAt)
	{
		credit = std::min<std::int64_t>(cap, credit + static_cast<std::int64_t>(now - creditAt));
		creditAt = now;
	}
}

void TurnSession::sendOrder(std::vector<std::uint8_t> bytes)
{
	credit -= static_cast<std::int64_t>(tickPeriod);
	Outstanding o{nextSequence++, std::move(bytes)};
	if (onSubmitted && !o.bytes.empty() && o.bytes[0] != ORDER_TYPE_VOICE)
		onSubmitted(o.sequence);
	OrderSubmit submit;
	submit.clientSequence = o.sequence;
	submit.order = o.bytes;
	send(submit);
	stats.orderFrameSent(false);
	outstanding.push_back(std::move(o));
	stats.outstandingDepth(outstanding.size());
}

void TurnSession::pump()
{
	if (!linkUp())
		return;
	refillCredit();
	const std::int64_t period = static_cast<std::int64_t>(tickPeriod);
	while (!queued.empty() && credit >= period)
	{
		auto bytes = std::move(queued.front().bytes);
		queued.pop_front();
		sendOrder(std::move(bytes));
	}
	const std::uint64_t voiceGap = std::uint64_t(config.voiceGapTicks) * tickPeriod;
	if (queued.empty() && !voiceQueue.empty() && credit >= period &&
	    (!voiceSentOnce || now >= lastVoiceAt + voiceGap))
	{
		auto bytes = std::move(voiceQueue.front());
		voiceQueue.pop_front();
		lastVoiceAt = now;
		voiceSentOnce = true;
		sendOrder(std::move(bytes));
	}
}

bool TurnSession::tooManyActions() const
{
	if (queueDropped && now < lastQueueDropAt + 2000000)
		return true;
	return queued.size() > config.busyQueueTicks;
}

void TurnSession::pushOrder(std::shared_ptr<Order> order, int playerNumber, bool)
{
	if (playerNumber < 0 || playerNumber >= numberOfPlayers || !order)
		return;
	order->sender = playerNumber;
	aiOrders[playerNumber].push_back(std::move(order));
}

void TurnSession::advanceStep(Uint32 checksum)
{
	++step;
	if (reloadPending || executed % checksumInterval != 0)
		return;
	ChecksumReport report;
	report.tick = executed;
	report.checksum = checksum;
	if (linkUp())
		send(report);
	else
		unsentReports.push_back(report);
}

bool TurnSession::orderReceived(int playerNumber)
{
	if (playerNumber < 0 || playerNumber >= numberOfPlayers)
		return false;
	if (isHuman(playerNumber))
		return !reloadPending && executed < horizonTick;
	return !aiOrders[playerNumber].empty();
}

bool TurnSession::tickReady()
{
	const bool starved = !reloadPending && executed >= horizonTick;
	if (starved)
	{
		if (!stalled && currentState == State::Running && !delay.catchingUp())
		{
			stalled = true;
			stallStart = now;
		}
	}
	else if (!reloadPending && stalled)
	{
		stalled = false;
		const std::uint64_t length = now - stallStart;
		++stallCounters.stalls;
		if (length * 2 > tickPeriod)
			++stallCounters.longStalls;
		stallCounters.stalledMicros += length;
		stallCounters.longestMicros = std::max(stallCounters.longestMicros, length);
	}
	bool ready = !reloadPending && !starved;
	for (int p = 0; ready && p < numberOfPlayers; ++p)
		if (!isHuman(p) && aiOrders[p].empty())
			ready = false;
	stats.readiness(starved && currentState != State::Ended, ready, now);
	return ready;
}

std::shared_ptr<Order> TurnSession::retrieveOrder(int playerNumber)
{
	int undecodableType = -1;
	return retrieveOrder(playerNumber, undecodableType);
}

std::shared_ptr<Order> TurnSession::retrieveOrder(int playerNumber, int& undecodableType)
{
	undecodableType = -1;
	std::shared_ptr<Order> order;
	if (playerNumber >= 0 && playerNumber < numberOfPlayers)
	{
		if (!isHuman(playerNumber))
		{
			if (!aiOrders[playerNumber].empty())
				order = aiOrders[playerNumber].front();
		}
		else
		{
			auto it = turns.find(executed);
			if (it != turns.end())
				for (const auto& e : it->second)
					if (e.seat == playerNumber)
					{
						if (playerNumber == seat)
							stats.ownOrderExecuted(e.order.data(), e.order.size(), executed, now);
						order = codec.decode(e.order.data(), e.order.size());
						if (!order) // reported (rate-limited) by TurnLockstepSession
							undecodableType = e.order.empty() ? 0 : e.order[0];
						break;
					}
		}
	}
	if (!order)
		order = std::make_shared<NullOrder>();
	order->sender = playerNumber;
	return order;
}

void TurnSession::clearTopOrders()
{
	for (int p = 0; p < numberOfPlayers; ++p)
		if (!isHuman(p) && !aiOrders[p].empty())
			aiOrders[p].pop_front();
	turns.erase(executed);
	++executed;
	delay.onTick(bufferedTicks(), buffer.targetTicks());
	stats.tickExecuted(bufferedTicks(), buffer.targetTicks(), delay.rateMultiplier(buffer.targetTicks()), catchingUp(),
	                   executed, now);
	stats.catchUpState(catchingUp(), executed, now);
}

Uint32 TurnSession::getWaitingOnMask()
{
	Uint32 mask = 0;
	for (int p = 0; p < numberOfPlayers && p < 32; ++p)
		if (!isHuman(p) && aiOrders[p].empty())
			mask |= 1u << p;
	if (executed >= horizonTick && !reloadPending)
	{
		Uint32 absent = 0;
		for (int p = 0; p < numberOfPlayers && p < 32; ++p)
			if (isHuman(p) && p != seat)
			{
				const auto s = seatPresence[p];
				if (s == PresenceState::NotConnected || s == PresenceState::Reconnecting || s == PresenceState::Lagging ||
				    s == PresenceState::Resyncing)
					absent |= 1u << p;
			}
		if (!absent && seat >= 0)
			absent = 1u << seat;
		mask |= absent;
	}
	return mask;
}

void TurnSession::flushAllOrders()
{
	// Orders leave as soon as the pacing credit allows (addLocalOrder and update call
	// pump too); sent ones are resent on Welcome after a loss.
	pump();
}

void TurnSession::quit(QuitReason reason)
{
	if (currentState == State::Ended || currentState == State::Rejected)
		return;
	if (linkUp())
	{
		// Orders still waiting for the pacing credit go first, so a last chat line or
		// command is not lost; the relay sequences them before the seat's quit.
		while (!queued.empty())
		{
			auto bytes = std::move(queued.front().bytes);
			queued.pop_front();
			sendOrder(std::move(bytes));
		}
		Quit q;
		q.reason = reason;
		send(q);
	}
	currentState = State::Ended;
}

std::uint64_t TurnSession::tickIntervalMicros() const
{
	if (catchingUp())
		return 0;
	return delay.tickIntervalMicros(tickPeriod, buffer.targetTicks());
}
}
