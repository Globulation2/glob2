// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Client side of the turn protocol (docs/multiplayer/turn-protocol.md). TurnSession
// implements the lockstep method set EngineRun uses from NetEngine, so it can stand
// behind the LockstepSession interface: human orders come from relay bundles, AI orders
// are pushed locally as before. It runs over an abstract transport and takes the time
// as an argument, so it is testable without sockets.

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "JitterBuffer.h"
#include "Order.h"
#include "TurnMessages.h"

namespace Turn
{
	/// A message-oriented, ordered and reliable connection to the relay: TCP or WSS
	/// through NetConnection, or a simulated link in tests.
	class TurnTransport
	{
	public:
		enum class State { Disconnected, Connecting, Connected };
		virtual ~TurnTransport() = default;
		virtual State state() = 0;
		/// Starts a new connection attempt (after a loss, or initially).
		virtual void connect() = 0;
		virtual void close() = 0;
		/// Queues one frame payload; false if the transport cannot take it.
		virtual bool send(const std::vector<std::uint8_t>& payload) = 0;
		/// Pops the next received frame payload, if any.
		virtual bool receive(std::vector<std::uint8_t>& payload) = 0;
	};

	/// Converts Orders to and from their wire bytes (type byte + getData()).
	struct OrderCodec
	{
		std::function<std::vector<std::uint8_t>(Order&)> encode;
		std::function<std::shared_ptr<Order>(const std::uint8_t*, std::size_t)> decode;
	};
	/// Order::getOrder at VERSION_MINOR, and getOrderType()+getData() for encoding.
	OrderCodec defaultOrderCodec();

	struct TurnSessionConfig
	{
		std::string ticket;
		std::uint64_t pingIntervalMicros = 500000;
		std::uint64_t reconnectInitialMicros = 250000;
		std::uint64_t reconnectMaxMicros = 5000000;
		std::size_t jitterWindow = 128;
		JitterBufferConfig jitter;
		DelayControllerConfig delay;
	};

	class TurnSession
	{
	public:
		enum class State
		{
			Connecting,      ///< first connection attempt
			AwaitingWelcome, ///< Hello sent
			Running,
			Reconnecting,    ///< transport lost; retrying with backoff
			Rejected,        ///< the relay refused us; see rejectReason()
			Ended,           ///< we quit
		};

		TurnSession(int numberOfPlayers, TurnTransport& transport, TurnSessionConfig config,
		            OrderCodec codec = defaultOrderCodec());

		/// Pumps the transport and timers. Call once per frame, before the engine step.
		void update(std::uint64_t nowMicros);

		// --- The LockstepSession method set (mirrors NetEngine) ---
		/// Submits a local human order to the relay; it returns in a bundle.
		void addLocalOrder(std::shared_ptr<Order> order);
		/// Queues a locally computed AI order for the next tick of that player.
		void pushOrder(std::shared_ptr<Order> order, int playerNumber, bool isAI);
		/// Called once before each tick executes with the current state checksum.
		void advanceStep(Uint32 checksum);
		/// True when the next tick is authorized and every AI seat has its order.
		bool tickReady();
		/// True if the player's order for the next tick is available.
		bool orderReceived(int playerNumber);
		/// The player's order for the next tick (a NullOrder when there is none).
		std::shared_ptr<Order> retrieveOrder(int playerNumber);
		/// As above; `undecodableType` is the type byte of a human seat's sequenced
		/// order when its bytes did not decode (the NullOrder then stands in for them),
		/// and -1 otherwise.
		std::shared_ptr<Order> retrieveOrder(int playerNumber, int& undecodableType);
		/// True for the seats whose orders come from the relay.
		bool isHumanSeat(int player) const { return isHuman(player); }
		/// Finishes the tick: drops its orders and advances the executed tick.
		void clearTopOrders();
		/// Seats the engine is waiting on: AI seats without orders, then human seats
		/// the relay reports as absent, or the local seat if our own link is down.
		Uint32 getWaitingOnMask();
		/// Always true: desyncs are arbitrated by the relay (see needsReload()).
		bool matchCheckSums() { return true; }
		/// Pushes any unsent local orders to the transport.
		void flushAllOrders();
		int getStep() const { return step; }
		void setLocalPlayer(int) {}

		// --- Turn-specific ---
		/// Tells the relay we are leaving; it sequences our PlayerQuitsGameOrder.
		void quit(QuitReason reason = QuitReason::PlayerQuit);
		/// The engine must reload the initial state and call reloadDone(), after a rejoin
		/// notice or a resume the relay could only serve from tick 0.
		bool needsReload() const { return reloadPending; }
		void reloadDone();
		/// Run ticks uncapped with rendering skipped.
		bool catchingUp() const { return reloadPending || delay.catchingUp(); }
		/// Interval to the next tick after the +/-5% nudge; 0 while catching up.
		std::uint64_t tickIntervalMicros() const;
		bool desyncFlagged() const { return flagged; }

		State state() const { return currentState; }
		RejectReason rejectReason() const { return rejection; }
		int localSeat() const { return seat; }
		std::uint32_t humanSeatMask() const { return humanMask; }
		std::uint32_t executedTick() const { return executed; }
		std::uint32_t horizon() const { return horizonTick; }
		std::uint32_t bufferedTicks() const { return horizonTick > executed ? horizonTick - executed : 0; }
		std::uint32_t targetTicks() const { return buffer.targetTicks(); }
		std::int64_t jitterMicros() const { return jitter.jitterMicros(); }
		std::int64_t rttMicros() const { return rtt; }
		PresenceState presence(int seatNumber) const;
		std::uint64_t tickPeriodMicros() const { return tickPeriod; }

		// --- Presentation only (connection HUD); never part of simulation state ---
		/// The last Presence entry for a seat: grace left while reconnecting and how
		/// many ticks behind the relay its last Ping was.
		struct SeatPresence
		{
			PresenceState state = PresenceState::NotConnected;
			std::uint32_t graceRemainingTicks = 0;
			std::uint32_t lagTicks = 0;
			/// When the entry arrived (session clock), to count the grace down locally.
			std::uint64_t receivedMicros = 0;
		};
		SeatPresence seatPresenceInfo(int seatNumber) const;
		/// Connection attempts since the link was last up (0 while connected).
		int reconnectAttempts() const { return attempts; }
		/// The relay's grace period for a lost seat (from Welcome; 0 before it).
		std::uint32_t graceTicks() const { return grace; }
		/// The relay told this client it diverged; true until it has caught up again.
		bool rejoiningAfterDesync() const { return desyncRejoin; }
		/// Highest horizon seen, for catch-up progress.
		std::uint32_t maxHorizon() const { return maxSeenHorizon; }

	private:
		struct Outstanding
		{
			std::uint32_t sequence;
			std::vector<std::uint8_t> bytes;
		};

		void send(const NetMessage& message);
		void handle(const NetMessage& message);
		void onWelcome(const Welcome& w);
		void onBundle(const TurnBundle& b);
		void onDesync(const DesyncNotice& d);
		void resetTurns();
		bool linkUp() const { return currentState == State::Running; }
		bool isHuman(int player) const { return player >= 0 && player < 32 && (humanMask & (1u << player)); }

		int numberOfPlayers;
		TurnTransport& transport;
		TurnSessionConfig config;
		OrderCodec codec;

		State currentState = State::Connecting;
		RejectReason rejection = RejectReason::Malformed;
		bool connectStarted = false;
		bool helloSent = false;
		std::uint64_t now = 0;
		std::uint64_t retryAt = 0;
		std::uint64_t backoff;

		int seat = -1;
		std::uint32_t humanMask = 0;
		std::uint32_t tickRate = DEFAULT_TICK_RATE_MILLIHZ;
		std::uint64_t tickPeriod = 40000;
		std::uint16_t checksumInterval = DEFAULT_CHECKSUM_INTERVAL;
		std::uint32_t bundleInterval = DEFAULT_BUNDLE_INTERVAL;

		std::uint32_t executed = 0;
		std::uint32_t horizonTick = 0;
		int step = 0;
		std::map<std::uint32_t, std::vector<TurnEntry>> turns;
		std::vector<std::deque<std::shared_ptr<Order>>> aiOrders;
		std::uint32_t liveThreshold = 0;
		std::uint32_t maxSeenHorizon = 0;
		bool resyncOutstanding = false;
		bool reloadPending = false;
		bool reloadNeedsRequest = false;
		bool flagged = false;

		std::uint32_t nextSequence = 1;
		std::deque<Outstanding> outstanding;
		std::deque<ChecksumReport> unsentReports;
		std::array<PresenceState, MAX_SEATS> seatPresence{};
		std::array<SeatPresence, MAX_SEATS> seatDetails{};
		int attempts = 0;
		std::uint32_t grace = 0;
		bool desyncRejoin = false;

		std::uint32_t pingNonce = 0;
		std::uint64_t lastPingAt = 0;
		std::map<std::uint32_t, std::uint64_t> pingsInFlight;
		std::int64_t rtt = 0;

		JitterEstimator jitter;
		JitterBuffer buffer;
		DelayController delay;
	};
}
