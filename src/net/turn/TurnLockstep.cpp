// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "TurnLockstep.h"

#include <iostream>

namespace Turn
{
TurnLockstepSession::TurnLockstepSession(int numberOfPlayers, std::shared_ptr<TurnTransport> transport,
                                         TurnSessionConfig config, OrderCodec codec)
	: link(std::move(transport)), session(numberOfPlayers, *link, std::move(config), std::move(codec))
{
}

void TurnLockstepSession::advanceStep(Uint32 checksum)
{
	if (onChecksum && !session.needsReload())
		onChecksum(session.executedTick(), checksum);
	session.advanceStep(checksum);
}

std::shared_ptr<Order> TurnLockstepSession::retrieveOrder(int playerNumber)
{
	int undecodableType = -1;
	auto order = session.retrieveOrder(playerNumber, undecodableType);
	const bool undecodable = undecodableType >= 0;
	if (session.isHumanSeat(playerNumber) && (undecodable || (validator && order->getOrderType() != ORDER_NULL)))
	{
		using namespace OrderValidation;
		const bool voice = (undecodable ? undecodableType : order->getOrderType()) == ORDER_VOICE_DATA;
		const Result result = undecodable ? Result{Verdict::Rejected, Reason::Undecodable} : validator(playerNumber, *order);
		const std::uint32_t tick = session.executedTick();
		audit.record(playerNumber, tick, voice, result);
		if (result.verdict != Verdict::Accepted)
		{
			// Enough to diagnose, not enough for a flooding client to fill the log.
			if (result.verdict == Verdict::Rejected && ++loggedRejections <= 20)
				std::cerr << "Turn session: rejected an order of type " << (undecodable ? undecodableType : int(order->getOrderType()))
				          << " from seat " << playerNumber << " at tick " << tick << " (" << name(result.reason) << ")\n";
			order = std::make_shared<NullOrder>();
			order->sender = playerNumber;
		}
	}
	if (orderFilter)
	{
		order = orderFilter(session.executedTick(), playerNumber, std::move(order));
		order->sender = playerNumber;
	}
	return order;
}

RecordTransport::RecordTransport(const MatchRecord& record, std::uint8_t seat)
{
	Welcome welcome;
	welcome.seat = seat;
	welcome.humanSeatMask = record.humanSeatMask;
	welcome.tickRateMilliHz = record.tickRateMilliHz;
	welcome.bundleInterval = record.bundleInterval;
	welcome.checksumInterval = record.checksumInterval;
	welcome.relayTick = record.endTick;
	welcome.resumeFromTick = 0;
	script.push_back(TurnCodec::encode(welcome));

	// The whole log in bundles below the frame limit, split at tick boundaries
	// exactly as the relay splits its resync bundles.
	TurnBundle bundle;
	bundle.fromTick = 0;
	std::size_t bytes = 0;
	auto flush = [&](std::uint32_t horizon) {
		bundle.horizonTick = horizon;
		script.push_back(TurnCodec::encode(bundle));
		bundle.entries.clear();
		bundle.fromTick = horizon;
		bytes = 0;
	};
	std::size_t i = 0;
	while (i < record.turns.size())
	{
		const std::uint32_t tick = record.turns[i].tick;
		std::size_t tickBytes = 0, end = i;
		while (end < record.turns.size() && record.turns[end].tick == tick)
			tickBytes += entryWireBytes(record.turns[end++]);
		if (!bundle.entries.empty() && bytes + tickBytes > MAX_BUNDLE_BYTES)
			flush(tick);
		for (; i < end; ++i)
			bundle.entries.push_back(record.turns[i]);
		bytes += tickBytes;
	}
	flush(record.endTick);
}

void RecordTransport::connect()
{
	connected = true;
	if (!served)
	{
		served = true;
		frames.assign(script.begin(), script.end());
	}
}

bool RecordTransport::receive(std::vector<std::uint8_t>& payload)
{
	if (!connected || frames.empty())
		return false;
	payload = std::move(frames.front());
	frames.pop_front();
	return true;
}
}
