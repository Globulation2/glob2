// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The host side of a LAN game (docs/multiplayer/lan.md): the room (seats, teams,
// readiness, chat, map transfer) and, once the match starts, the turn relay
// (TurnSequencer) in-process. Guests connect over a pinned WSS listener; discovery stays on NetBroadcaster. The host's own player
// talks to the relay through an in-memory TurnTransport (localTransport()), which
// also pumps the host while the host's engine runs.
//
// A hosting LanHost services its connections and the relay on its own thread, so
// guests are served at the relay's pace whatever the host's own engine or UI is doing
// (loading, a slow frame, a dialog). Every public member is thread-safe. An offline
// preview has no thread; call update() yourself. Not copyable.

#include <cstdint>
#include <deque>
#include <functional>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <optional>
#include <string>
#include <vector>

#include "AI.h"
#include "LanProtocol.h"
#include "MapHeader.h"
#include "TurnSequencer.h"
#include "TurnSession.h"

class NetBroadcaster;
class NetTransportListener;
class NetWaker;
struct NetWaitHandle;
class GameHeader;

namespace Lan
{
	inline Turn::SequencerConfig lanSequencerConfig()
	{
		Turn::SequencerConfig config;
		config.bundleInterval = 1;
		return config;
	}

	/// Where the host writes the relay's network summary for a record path:
	/// "x.g2mr" -> "x.network.json".
	std::string networkSummaryPath(const std::string& recordPath);

	class LanHost : public std::enable_shared_from_this<LanHost>, private Turn::SequencerOutput
	{
	public:
		struct Options
		{
			std::string hostName = "Host";
			/// The map or save to play, as ChooseMapScreen returns it (FileManager path).
			MapHeader map;
			std::string mapFile;
			/// 0 uses the default LAN port (7489).
			std::uint16_t port = 0;
			/// Accept guests and announce the game; false makes an offline preview.
			bool network = true;
			bool broadcast = true;
			/// Overrides the address in the pairing endpoint (tests: 127.0.0.1).
			std::string advertisedAddress;
			/// LAN relays send a bundle every tick, like the online relay
			/// (Turn::DEFAULT_BUNDLE_INTERVAL).
			Turn::SequencerConfig sequencer = lanSequencerConfig();
			/// How long the relay waits for every seat's first Hello before its clock
			/// starts anyway.
			std::uint64_t loadWaitMicros = 30000000;
			/// Where the host writes the match record when the match ends (empty: none).
			std::string recordPath;
			/// Service the network on a thread (network hosts only).
			bool thread = true;
		};

		/// Throws std::exception when the listener cannot start or the map cannot be read.
		explicit LanHost(Options options);
		~LanHost() override;
		LanHost(const LanHost&) = delete;
		LanHost& operator=(const LanHost&) = delete;

		/// Accepts guests, handles their messages, and advances the relay. The thread
		/// calls it whenever a connection is ready or the relay has work due; without
		/// a thread, call it often.
		void update();
		bool threaded() const { return worker.joinable(); }
		/// How many times the worker thread has woken (diagnostics and tests).
		std::uint64_t workerWakeups() const { return wakeups; }

		// --- Room (host player's actions) ---
		RoomState state() const;
		/// The pinned endpoint guests paste ("wss://addr:port/yog#sha256=...").
		const std::string& pairing() const { return pairingEndpoint; }
		const std::string& mapFile() const { return options.mapFile; }
		const MapHeader& mapHeader() const { return options.map; }
		void setSeatTeam(int seat, int team);
		/// Removes an AI seat, or disconnects the guest in a human seat.
		void kickSeat(int seat);
		void addAI(AI::ImplementationID ai);
		/// Rules, alliances and experiments edited through a GameHeader (the options screen).
		void applyOptions(const GameHeader& header);
		void chat(const std::string& text);
		/// Every guest is ready and has the map.
		bool canStart() const;

		struct LocalStart
		{
			Online::MatchSetup setup;
			int seat = 0;
			std::string ticket;
			std::string mapFile;
			std::shared_ptr<Turn::TurnTransport> transport;
		};
		/// Starts the match: picks the seed, issues tickets, tells every guest to start,
		/// and returns what the host's own engine needs.
		LocalStart start();
		bool started() const { return room.started; }

		/// The host is leaving: ends the relay (final bundle), tells every guest the
		/// game is over, and stops listening. `reason` is "host-left" or "cancelled".
		void close(const std::string& reason);
		bool isClosed() const { return closed; }

		// --- Events for the room screen ---
		/// Chat lines ("name: text") and notices since the last call.
		std::vector<std::string> takeChat();
		/// True once after anything in state() changed.
		bool takeChanged();

		// --- Relay ---
		bool relayRunning() const;
		Turn::PresenceState presence(int seat) const;
		std::uint32_t horizon() const;
		std::optional<std::uint32_t> agreedChecksum(std::uint32_t tick) const;
		std::size_t guestCount() const;
		/// The relay's per-seat network summary so far (null before the match starts).
		nlohmann::json networkSummary() const;

	private:
		struct Peer
		{
			std::unique_ptr<LanLink> link;
			std::uint32_t member = 0;     ///< room member id; 0 = not a member
			bool greeted = false;         ///< sent a valid room hello
			bool inRelay = false;         ///< passed to TurnSequencer::onConnect
			bool closing = false;         ///< close after flushing
			std::uint64_t mapOffsetLimit = 0;
		};
		class LocalTransport;
		friend class LocalTransport;

		// SequencerOutput
		void send(Turn::PeerId peer, const std::vector<std::uint8_t>& payload) override;
		void close(Turn::PeerId peer) override;

		/// True when a connection was accepted.
		bool accept(std::uint64_t now);
		void handle(Turn::PeerId id, Peer& peer, const std::vector<std::uint8_t>& payload, std::uint64_t now);
		void handleRoom(Turn::PeerId id, Peer& peer, const nlohmann::json& message, std::uint64_t now);
		void handleHello(Turn::PeerId id, Peer& peer, const nlohmann::json& message);
		void handleTurn(Turn::PeerId id, Peer& peer, const std::vector<std::uint8_t>& payload, std::uint64_t now);
		void sendMapChunk(Peer& peer, std::uint32_t offset);
		void dropPeer(Turn::PeerId id, bool notifyRelay, std::uint64_t now);
		void refuse(Peer& peer, const std::string& reason, const std::string& detail = {});
		void removeMemberSeat(std::uint32_t member);
		void renumberSeats();
		int freeTeam() const;
		void broadcastState();
		void broadcast(const nlohmann::json& message);
		void sendTo(Peer& peer, const std::vector<std::uint8_t>& payload);
		void startRelay(std::uint64_t now);
		void localFrame(const std::vector<std::uint8_t>& payload);
		std::string uniqueName(const std::string& wanted) const;
		void writeRecord();
		/// One round of servicing; true when more work is ready at once.
		bool updateLocked();
		void serve();
		/// When the worker must next run, collecting what it can wait on meanwhile.
		std::uint64_t nextWakeLocked(std::uint64_t now, std::vector<NetWaitHandle>& handles, bool& supported);
		/// Wakes the worker after another thread queued work for it.
		void wakeWorker();
		void stopThread();

		Options options;
		RoomState room;
		std::string pairingEndpoint;
		std::string mapHash;
		std::string mapTransfer; ///< gzip bytes guests download
		std::unique_ptr<NetTransportListener> listener;
		std::unique_ptr<NetBroadcaster> broadcaster;
		std::map<Turn::PeerId, Peer> peers;
		Turn::PeerId nextPeer = 1;
		std::uint32_t nextMember = 1;
		std::vector<std::string> chatLines;
		bool changed = true;
		bool closed = false;

		// Match
		std::map<std::string, int> tickets; ///< ticket -> seat
		std::map<int, std::string> seatTickets;
		std::unique_ptr<Turn::TurnSequencer> relay;
		std::uint64_t startRequestedAt = 0;
		/// Turn frames that arrived before the relay's clock started.
		std::deque<std::pair<Turn::PeerId, std::vector<std::uint8_t>>> early;
		std::uint32_t helloSeen = 0; ///< human seats whose Hello arrived before the clock
		// Local player link (peer id 0)
		std::deque<std::vector<std::uint8_t>> toLocal;
		bool localConnected = false;
		bool localInRelay = false;
		bool recordWritten = false;

		mutable std::recursive_mutex mutex;
		std::unique_ptr<NetWaker> waker;
		std::thread worker;
		std::atomic<bool> stopping{false};
		std::atomic<std::uint64_t> wakeups{0};
	};
}
