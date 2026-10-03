// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The engine side of the turn protocol: TurnLockstepSession puts a TurnSession behind
// the engine's LockstepSession interface, and RecordTransport replays a MatchRecord
// to a TurnSession as if a relay had sent it, so the verifier executes recorded
// matches through exactly the code live clients run.

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "LockstepSession.h"
#include "MatchRecord.h"
#include "NetConsts.h"
#include "Order.h"
#include "OrderValidation.h"
#include "TurnSession.h"

namespace Turn
{
	/// A match's limit on pausing (Online::PauseLimit): per human seat, at most
	/// `pauses` pauses and `seconds` paused in total, counted in executed ticks.
	struct PauseLimit
	{
		std::uint32_t pauses = 3;
		std::uint32_t seconds = 60;
	};

	/// What the pause bookkeeping tells the player (presentation only).
	enum class PauseNotice
	{
		Refused, ///< the seat has no pauses or pause time left; its pause did nothing
		Expired, ///< the pausing seat's pause time ran out; the game resumes
	};

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
		/// Checks every order of a human seat before the engine executes it (the engine
		/// installs OrderValidation::validate on its game). An order that is not
		/// Accepted, or that does not decode, is replaced by a NullOrder; the verdict is
		/// counted in orderAudit(). Unset: orders pass through unchecked.
		std::function<OrderValidation::Result(int player, Order& order)> validator;
		/// Per-seat counts of checked orders since the game (re)started from tick 0.
		const OrderValidation::Audit& orderAudit() const { return audit; }
		/// Called when the engine reloads the initial state: the orders are checked again,
		/// and the pause bookkeeping starts over with them.
		void resetOrderAudit()
		{
			audit = {};
			pauses = {};
		}

		/// Limits pausing as the match setup says; without a call pausing is unlimited.
		/// Pause orders of human seats are then checked when they are retrieved: a pause
		/// beyond the seat's limit executes as a NullOrder everywhere (stale,
		/// pause_limit), and the game resumes by itself once the seat that paused has
		/// used its time (takeForcedResume). Ticks count while paused, as the relay's
		/// clock runs on, so every client and the verifier agree on when that is.
		void setPauseLimit(PauseLimit limit) { pauseLimit = limit; }
		/// After the engine executed a tick's orders: a PauseGameOrder(false) it must
		/// execute too, when the pausing seat's pause time has run out; else null.
		std::shared_ptr<Order> takeForcedResume();
		/// Pauses started and ticks paused by a seat so far (diagnostics, tests).
		std::uint32_t pausesUsed(int seat) const { return seat >= 0 && seat < 32 ? pauses.count[seat] : 0; }
		std::uint32_t pauseTicksUsed(int seat) const { return seat >= 0 && seat < 32 ? pauses.ticks[seat] : 0; }
		/// The match's pause limit, if it has one (presentation: the menu and label).
		const std::optional<PauseLimit>& currentPauseLimit() const { return pauseLimit; }
		/// The seat whose pause is running, or -1.
		int pausedBy() const { return pauses.paused ? pauses.by : -1; }
		/// Presentation hook for the pause limit, with the seat concerned.
		std::function<void(PauseNotice, int seat)> onPauseNotice;
		/// Test hook: may replace the order retrieveOrder returns, after the check (a
		/// cheating or diverging client). Never set in the game.
		std::function<std::shared_ptr<Order>(std::uint32_t tick, int player, std::shared_ptr<Order>)> orderFilter;

		void setLocalPlayer(int player) override { session.setLocalPlayer(player); }
		/// The local player's own PlayerQuitsGameOrder (in-game Quit, end-of-game
		/// dialog) is not submitted when this is set: the engine leaves with a Quit
		/// message instead (the relay sequences the same quit order). Only Quit says
		/// whether the game was decided, and once the relay has sequenced the order it
		/// closes the seat before a Quit behind it could arrive.
		std::function<void()> onLocalQuit;
		void addLocalOrder(std::shared_ptr<Order> order) override
		{
			if (order && onLocalQuit && order->getOrderType() == ORDER_PLAYER_QUIT_GAME)
			{
				onLocalQuit();
				return;
			}
			session.addLocalOrder(std::move(order));
		}
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
		void clearTopOrders() override;
		void flushAllOrders() override { session.flushAllOrders(); }

	private:
		std::shared_ptr<TurnTransport> link;
		TurnSession session;
		OrderValidation::Audit audit;
		std::uint32_t loggedRejections = 0;

		/// The pause state as the executed orders left it, and what each seat used.
		struct PauseBook
		{
			bool paused = false;
			int by = -1; ///< the seat whose pause is running, -1 when none counts
			std::array<std::uint32_t, 32> count{};
			std::array<std::uint32_t, 32> ticks{};
		};
		std::optional<PauseLimit> pauseLimit;
		PauseBook pauses;
		std::uint32_t pauseBudgetTicks() const;
		OrderValidation::Result checkPause(int seat, bool pause);
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
