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

std::uint32_t TurnLockstepSession::pauseBudgetTicks() const
{
	return pauseLimit ? static_cast<std::uint32_t>(std::uint64_t(pauseLimit->seconds) * session.tickRateMilliHz() / 1000)
	                  : 0;
}

OrderValidation::Result TurnLockstepSession::checkPause(int seat, bool pause)
{
	using namespace OrderValidation;
	if (!pause)
	{
		// Anyone may resume.
		pauses.paused = false;
		pauses.by = -1;
		return {};
	}
	if (pauses.paused)
		return {}; // already paused: changes nothing, costs nothing
	if (pauseLimit && seat >= 0 && seat < 32)
	{
		if (pauses.count[seat] >= pauseLimit->pauses || pauses.ticks[seat] >= pauseBudgetTicks())
		{
			if (onPauseNotice)
				onPauseNotice(PauseNotice::Refused, seat);
			return {Verdict::Stale, Reason::PauseLimit};
		}
		++pauses.count[seat];
	}
	pauses.paused = true;
	pauses.by = seat;
	return {};
}

std::shared_ptr<Order> TurnLockstepSession::takeForcedResume()
{
	if (!pauseLimit || !pauses.paused || pauses.by < 0 || pauses.ticks[pauses.by] < pauseBudgetTicks())
		return nullptr;
	const int seat = pauses.by;
	pauses.paused = false;
	pauses.by = -1;
	if (onPauseNotice)
		onPauseNotice(PauseNotice::Expired, seat);
	auto order = std::make_shared<PauseGameOrder>(false);
	order->sender = seat;
	return order;
}

void TurnLockstepSession::clearTopOrders()
{
	// The tick that just executed counts against the seat whose pause held it.
	if (pauses.paused && pauses.by >= 0)
		++pauses.ticks[pauses.by];
	session.clearTopOrders();
}

std::shared_ptr<Order> TurnLockstepSession::retrieveOrder(int playerNumber)
{
	int undecodableType = -1;
	auto order = session.retrieveOrder(playerNumber, undecodableType);
	const bool undecodable = undecodableType >= 0;
	const bool human = session.isHumanSeat(playerNumber);
	const bool pause = human && !undecodable && order->getOrderType() == ORDER_PAUSE_GAME;
	if (human && (undecodable || pause || (validator && order->getOrderType() != ORDER_NULL)))
	{
		using namespace OrderValidation;
		const bool voice = (undecodable ? undecodableType : order->getOrderType()) == ORDER_VOICE_DATA;
		Result result = undecodable ? Result{Verdict::Rejected, Reason::Undecodable}
		                : validator ? validator(playerNumber, *order)
		                            : Result{};
		if (pause && result.verdict == Verdict::Accepted)
		{
			// The engine's codec decodes a PauseGameOrder; a test codec may not.
			const auto* p = dynamic_cast<const PauseGameOrder*>(order.get());
			const Uint8* data = p ? nullptr : order->getData();
			const bool wantPause = p ? p->pause : (order->getDataLength() >= 1 && data && data[0]);
			result = checkPause(playerNumber, wantPause);
		}
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

	// The whole log in bundles below the frame limit, split at tick boundaries by
	// the relay's own splitter (TurnSequencer sends resync bundles the same way).
	for (const auto& bundle : splitIntoBundles(record.turns, 0, record.endTick))
		script.push_back(TurnCodec::encode(bundle));
	if (record.endTick == 0)
	{
		// A record that ended before its first tick: one empty bundle, as before.
		TurnBundle empty;
		script.push_back(TurnCodec::encode(empty));
	}
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
