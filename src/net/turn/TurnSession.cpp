// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "TurnSession.h"

#include <algorithm>
#include <iostream>

#include "Version.h"

namespace Turn
{
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

PresenceState TurnSession::presence(int seatNumber) const
{
	if (seatNumber < 0 || seatNumber >= static_cast<int>(MAX_SEATS))
		return PresenceState::Left;
	return seatPresence[seatNumber];
}

void TurnSession::send(const NetMessage& message)
{
	transport.send(TurnCodec::encode(message));
}

void TurnSession::update(std::uint64_t nowMicros)
{
	now = std::max(now, nowMicros);
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
			currentState = State::Reconnecting;
			retryAt = now + backoff;
		}
		else if (now >= retryAt)
		{
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
		hello.ticket = config.ticket;
		hello.haveHorizon = reloadPending ? 0 : horizonTick;
		send(hello);
		helloSent = true;
		currentState = State::AwaitingWelcome;
	}

	std::vector<std::uint8_t> payload;
	while (transport.receive(payload))
	{
		auto message = TurnCodec::decode(payload);
		if (!message)
		{
			std::cerr << "Turn session: malformed message from relay; reconnecting\n";
			transport.close();
			currentState = State::Reconnecting;
			helloSent = false;
			retryAt = now + backoff;
			return;
		}
		handle(*message);
		if (currentState == State::Rejected)
			return;
	}

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
	buffer.update(jitter.jitterMicros(), tickPeriod, now);
	delay.observe(bufferedTicks(), buffer.targetTicks());
}

void TurnSession::handle(const NetMessage& message)
{
	switch (message.getMessageType())
	{
	case MSG_WELCOME:
		onWelcome(static_cast<const Welcome&>(message));
		break;
	case MSG_REJECT:
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
			seatPresence[p.seat] = p.state;
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
	if (w.protocolVersion != PROTOCOL_VERSION || (seat >= 0 && w.seat != seat))
	{
		currentState = State::Rejected;
		rejection = RejectReason::ProtocolVersion;
		transport.close();
		return;
	}
	seat = w.seat;
	humanMask = w.humanSeatMask;
	tickRate = w.tickRateMilliHz;
	tickPeriod = ticksToMicros(1, tickRate);
	checksumInterval = w.checksumInterval;
	bundleInterval = w.bundleInterval;
	if (w.resumeFromTick != horizonTick)
	{
		// The relay serves the log from tick 0: drop what we hold. If we had already
		// executed ticks, the engine must reload the initial state.
		resetTurns();
		if (executed > 0)
		{
			reloadPending = true;
			reloadNeedsRequest = false;
		}
	}
	resyncOutstanding = false;
	liveThreshold = std::max(w.relayTick + 1, maxSeenHorizon);
	jitter.clear();
	nextSequence = std::max(nextSequence, w.lastClientSequence + 1);
	while (!outstanding.empty() && outstanding.front().sequence <= w.lastClientSequence)
		outstanding.pop_front();
	currentState = State::Running;
	backoff = config.reconnectInitialMicros;
	for (const auto& o : outstanding)
	{
		OrderSubmit submit;
		submit.clientSequence = o.sequence;
		submit.order = o.bytes;
		send(submit);
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
	maxSeenHorizon = std::max(maxSeenHorizon, b.horizonTick);
	if (b.fromTick != horizonTick)
	{
		if (!resyncOutstanding && !reloadPending)
		{
			ResyncRequest request;
			request.fromTick = horizonTick;
			send(request);
			resyncOutstanding = true;
		}
		return;
	}
	resyncOutstanding = false;
	for (const auto& e : b.entries)
		turns[e.tick].push_back(e);
	horizonTick = b.horizonTick;
	if (b.horizonTick > threshold)
		jitter.addSample(static_cast<std::int64_t>(now), b.horizonTick, tickPeriod);
	delay.observe(bufferedTicks(), buffer.targetTicks());
}

void TurnSession::onDesync(const DesyncNotice& d)
{
	if (d.verdict == DesyncVerdict::Flagged)
	{
		flagged = true;
		return;
	}
	if (seat < 0 || !(d.divergedSeatMask & (1u << seat)))
		return;
	std::cerr << "Turn session: diverged at tick " << d.tick << "; reloading\n";
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
		resyncOutstanding = true;
	}
	reloadNeedsRequest = false;
}

void TurnSession::addLocalOrder(std::shared_ptr<Order> order)
{
	if (!order || currentState == State::Ended || currentState == State::Rejected)
		return;
	const Uint8 type = order->getOrderType();
	if (type == ORDER_TYPE_NULL || type == ORDER_TYPE_ADJUST_LATENCY)
		return;
	auto bytes = codec.encode(*order);
	if (bytes.empty() || bytes.size() > MAX_ORDER_BYTES)
	{
		std::cerr << "Turn session: dropping an order of " << bytes.size() << " bytes\n";
		return;
	}
	Outstanding o{nextSequence++, std::move(bytes)};
	if (linkUp())
	{
		OrderSubmit submit;
		submit.clientSequence = o.sequence;
		submit.order = o.bytes;
		send(submit);
	}
	outstanding.push_back(std::move(o));
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
	if (reloadPending || executed >= horizonTick)
		return false;
	for (int p = 0; p < numberOfPlayers; ++p)
		if (!isHuman(p) && aiOrders[p].empty())
			return false;
	return true;
}

std::shared_ptr<Order> TurnSession::retrieveOrder(int playerNumber)
{
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
						order = codec.decode(e.order.data(), e.order.size());
						if (!order)
							std::cerr << "Turn session: undecodable order at tick " << executed << "\n";
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
	// Orders are sent as soon as they are added while the link is up; after a loss
	// they are resent on Welcome. Nothing is held back here.
}

void TurnSession::quit(QuitReason reason)
{
	if (currentState == State::Ended || currentState == State::Rejected)
		return;
	if (linkUp())
	{
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
