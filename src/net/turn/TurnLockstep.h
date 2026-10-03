// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The engine side of the turn protocol: TurnLockstepSession puts a TurnSession behind
// the engine's LockstepSession interface, and RecordTransport replays a MatchRecord
// to a TurnSession as if a relay had sent it, so the verifier executes recorded
// matches through exactly the code live clients run.

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "LockstepSession.h"
#include "MatchRecord.h"
#include "TurnSession.h"

namespace Turn
{
	class TurnLockstepSession : public LockstepSession
	{
	public:
		/// Keeps the transport alive for as long as the session uses it.
		TurnLockstepSession(int numberOfPlayers, std::shared_ptr<TurnTransport> transport, TurnSessionConfig config,
		                    OrderCodec codec = defaultOrderCodec());

		TurnSession& turn() { return session; }
		const TurnSession& turn() const { return session; }
		const std::shared_ptr<TurnTransport>& transport() const { return link; }

		/// Called on every advanceStep with the tick about to execute and the state
		/// checksum before it: the value a ChecksumReport for that tick carries.
		std::function<void(std::uint32_t tick, Uint32 checksum)> onChecksum;
		/// Test hook: may replace the order retrieveOrder returns (a cheating or
		/// diverging client). Never set in the game.
		std::function<std::shared_ptr<Order>(std::uint32_t tick, int player, std::shared_ptr<Order>)> orderFilter;

		void setLocalPlayer(int player) override { session.setLocalPlayer(player); }
		void addLocalOrder(std::shared_ptr<Order> order) override { session.addLocalOrder(std::move(order)); }
		void pushOrder(std::shared_ptr<Order> order, int playerNumber, bool isAI) override
		{
			session.pushOrder(std::move(order), playerNumber, isAI);
		}
		bool orderReceived(int playerNumber) override { return session.orderReceived(playerNumber); }
		void advanceStep(Uint32 checksum) override;
		bool tickReady() override { return session.tickReady(); }
		Uint32 getWaitingOnMask() override { return session.getWaitingOnMask(); }
		/// Always true: the relay arbitrates checksums, and a divergence reaches the
		/// engine as TurnSession::needsReload() or desyncFlagged() instead.
		bool matchCheckSums() override { return session.matchCheckSums(); }
		std::shared_ptr<Order> retrieveOrder(int playerNumber) override;
		void clearTopOrders() override { session.clearTopOrders(); }
		void flushAllOrders() override { session.flushAllOrders(); }

	private:
		std::shared_ptr<TurnTransport> link;
		TurnSession session;
	};

	/// A TurnTransport that plays a recorded match: Welcome (as `seat`) followed by
	/// the full turn log in bundles up to record.endTick. Everything sent is dropped.
	class RecordTransport : public TurnTransport
	{
	public:
		RecordTransport(const MatchRecord& record, std::uint8_t seat);

		State state() override { return connected ? State::Connected : State::Disconnected; }
		void connect() override;
		void close() override { connected = false; }
		bool send(const std::vector<std::uint8_t>&) override { return connected; }
		bool receive(std::vector<std::uint8_t>& payload) override;

	private:
		std::deque<std::vector<std::uint8_t>> frames;
		bool connected = false;
		bool served = false;
		std::vector<std::vector<std::uint8_t>> script;
	};
}
