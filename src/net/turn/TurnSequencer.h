// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The relay core of the turn protocol (docs/multiplayer/turn-protocol.md): owns the
// match clock, assigns execution ticks to human orders, broadcasts turn bundles, keeps
// the turn log, tracks presence and grace timers, and arbitrates checksum reports.
// Transport-agnostic and clock-injected: the host passes connection events, frame
// payloads and the current time in, and receives frames through SequencerOutput.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "MatchRecord.h"
#include "TurnMessages.h"

namespace Turn
{
	using PeerId = std::uint64_t;

	struct SequencerConfig
	{
		std::uint32_t tickRateMilliHz = DEFAULT_TICK_RATE_MILLIHZ;
		std::uint8_t bundleInterval = DEFAULT_BUNDLE_INTERVAL;
		std::uint16_t checksumInterval = DEFAULT_CHECKSUM_INTERVAL;
		std::uint64_t graceMicros = DEFAULT_GRACE_MICROS;
		std::uint32_t maxAheadTicks = 250;            ///< flood limit on a seat's order queue
		std::uint32_t arbitrationTimeoutTicks = 250;  ///< arbitrate with partial reports after this
		std::uint32_t lagThresholdTicks = 50;         ///< connected seat shown as lagging beyond this
		std::uint32_t presenceRefreshTicks = 25;
		std::uint32_t maxRejoins = 3;
		std::size_t tickByteBudget = MAX_TICK_ORDER_BYTES;
	};

	/// Where the sequencer's frames go. Implemented by the relay's socket layer, the LAN
	/// host, or a test network.
	class SequencerOutput
	{
	public:
		virtual ~SequencerOutput() = default;
		/// Queues one frame payload for the peer, in order.
		virtual void send(PeerId peer, const std::vector<std::uint8_t>& payload) = 0;
		/// Closes the peer's connection after its queued frames. The host must not call
		/// onDisconnect for a peer the sequencer closed itself.
		virtual void close(PeerId peer) = 0;
	};

	/// Verifies a ticket and returns the seat it admits, or -1 to refuse it.
	using Admission = std::function<int(const std::string& ticket)>;

	class TurnSequencer
	{
	public:
		struct Stats
		{
			std::uint64_t ordersSequenced = 0;
			std::uint64_t ordersDropped = 0;
			std::uint64_t bundlesSent = 0;
			std::uint64_t peersRejected = 0;
		};

		TurnSequencer(SequencerConfig config, std::uint32_t humanSeatMask, Admission admission, SequencerOutput& output,
		              std::uint64_t startMicros);

		/// A transport connection opened; the peer must send Hello first.
		void onConnect(PeerId peer, std::uint64_t nowMicros);
		/// One frame payload arrived from the peer.
		void onReceive(PeerId peer, const std::uint8_t* data, std::size_t size, std::uint64_t nowMicros);
		void onReceive(PeerId peer, const std::vector<std::uint8_t>& payload, std::uint64_t nowMicros)
		{
			onReceive(peer, payload.data(), payload.size(), nowMicros);
		}
		/// The peer's transport closed.
		void onDisconnect(PeerId peer, std::uint64_t nowMicros);
		/// Advances the clock: grace expiry, bundles, arbitration timeouts, presence.
		void update(std::uint64_t nowMicros);

		/// The tick in progress at the given time.
		std::uint32_t relayTick(std::uint64_t nowMicros) const;
		/// Every tick below this has been broadcast.
		std::uint32_t horizon() const { return sentHorizon; }
		/// Broadcast turns without voice, sorted by (tick, seat).
		const std::vector<TurnEntry>& turnLog() const { return log; }
		PresenceState presence(std::uint8_t seat) const;
		bool desyncFlagged() const { return flagged; }
		/// True once every human seat has left.
		bool matchOver() const { return over; }
		std::optional<std::uint32_t> agreedChecksum(std::uint32_t tick) const;
		const Stats& stats() const { return counters; }
		std::uint32_t humanSeats() const { return humanMask; }

		/// Flushes pending turns into a final bundle and stops accepting play. Called
		/// automatically once every seat has left; a host shutting down early calls it
		/// and gets FLAG_INCOMPLETE in the record.
		void finish(std::uint64_t nowMicros);

		/// Assembles the persistent record of everything broadcast so far.
		MatchRecord buildRecord(const std::string& matchId, const std::string& simVersion, const std::string& setupJson,
		                        const std::array<std::uint8_t, 32>& mapHash) const;

	private:
		struct Seat
		{
			PresenceState state = PresenceState::NotConnected;
			PeerId peer = 0;
			bool hasPeer = false;
			bool streaming = false;      ///< receives live bundles
			bool awaitingResync = false; ///< told to rejoin, ignore reports until ResyncRequest(0)
			std::uint64_t graceStart = 0;
			std::uint32_t nextFreeTick = 0;
			std::uint32_t executedTick = 0;
			std::uint32_t lastClientSequence = 0;
			std::uint32_t rejoins = 0;
		};
		struct Peer
		{
			int seat = -1;
		};
		struct TickReports
		{
			std::map<std::uint8_t, std::uint32_t> current;  ///< reports awaiting arbitration
			std::map<std::uint8_t, std::uint32_t> first;    ///< first report per seat, for the record
			bool arbitrated = false;
			std::optional<std::uint32_t> agreed;
			std::uint32_t support = 0;
		};

		void send(PeerId peer, const NetMessage& message);
		void reject(PeerId peer, RejectReason reason, const std::string& detail);
		void handleHello(PeerId peer, const Hello& hello, std::uint64_t now);
		void handleOrder(int seat, const OrderSubmit& submit, std::uint64_t now);
		void handleChecksum(int seat, const ChecksumReport& report);
		void handleResync(PeerId peer, int seat, std::uint32_t fromTick);
		bool assign(std::uint8_t seat, std::vector<std::uint8_t> order, std::uint32_t currentTick, bool floodLimit = true);
		void sequenceQuit(std::uint8_t seat, MatchEventKind why, std::uint64_t now);
		void emitUpTo(std::uint32_t newHorizon);
		void sendLog(PeerId peer, std::uint32_t fromTick);
		void arbitrate(std::uint32_t tick);
		void tellRejoin(std::uint8_t seat, std::uint32_t tick);
		void flag(std::uint32_t tick, std::uint32_t seatMask);
		bool expectedReporter(const Seat& s) const;
		void setState(std::uint8_t seat, PresenceState state);
		void event(std::uint8_t seat, MatchEventKind kind);
		void broadcastPresence();
		Presence presenceSnapshot() const;
		void dropPeer(PeerId peer);

		SequencerConfig config;
		std::uint32_t humanMask;
		Admission admission;
		SequencerOutput& output;
		std::uint64_t start;
		std::uint64_t now = 0;
		std::uint64_t tickPeriod;

		std::array<Seat, MAX_SEATS> seats{};
		std::unordered_map<PeerId, Peer> peers;
		std::map<std::uint32_t, std::vector<TurnEntry>> pending; ///< sorted by seat within a tick
		std::map<std::uint32_t, std::size_t> pendingBytes;
		std::vector<TurnEntry> log;
		std::map<std::uint32_t, TickReports> reports;
		std::vector<MatchEvent> events;
		std::uint32_t sentHorizon = 0;
		std::uint32_t lastPresenceTick = 0;
		bool presenceDirty = true;
		bool flagged = false;
		bool over = false;
		bool incomplete = false;
		Stats counters;
	};
}
