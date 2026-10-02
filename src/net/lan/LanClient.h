// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// A guest in a LAN game (docs/multiplayer/lan.md): joins the host's room over its
// pinned WSS endpoint, mirrors the room state, downloads the map by content hash into
// the map cache, and, when the host starts, hands the engine a TurnTransport that
// shares the same connection. During play that transport reconnects to the host by
// itself after a loss, and ends the game cleanly when the host leaves.
//
// Single-threaded: call update() from the UI thread; the turn transport pumps the
// connection itself while the engine runs.

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "LanProtocol.h"
#include "TurnSession.h"

namespace Online
{
	class MapCache;
	class OnlineStorage;
}

namespace Lan
{
	class LanClient
	{
	public:
		struct Options
		{
			std::string endpoint; ///< the host's pairing string
			std::string name;
			/// Empty: downloaded maps go to the client's shared map cache
			/// (Online::services().maps, online/maps/ in the user directory). Otherwise
			/// a private Online::MapCache rooted at this absolute directory (tests).
			std::string cacheDirectory;
			/// While playing, how long the guest keeps trying to reach a vanished host.
			std::uint64_t hostLossGiveUpMicros = 120000000;
			/// While playing, a connection the host has not written to for this long is
			/// treated as lost (the host sends a bundle every 80 ms).
			std::uint64_t silenceMicros = 5000000;
			/// Test hook: wraps the turn transport handed to the engine (an emulated link).
			std::function<std::shared_ptr<Turn::TurnTransport>(std::shared_ptr<Turn::TurnTransport>)> wrapTransport;
		};

		enum class Phase
		{
			Connecting, ///< opening the connection, waiting for the welcome
			Lobby,      ///< in the room
			Started,    ///< the host started; see startReady()/takeStart()
			Closed,     ///< see closeReason()
		};

		explicit LanClient(Options options);
		~LanClient();

		void update();
		Phase phase() const { return currentPhase; }
		/// Why the room closed: "unreachable", "lost", "version", "full", "started",
		/// "kicked", "malformed", "host-left" or "cancelled".
		const std::string& closeReason() const { return reason; }
		const std::string& closeDetail() const { return detail; }

		/// The last room state, once the host has sent one.
		const RoomState* state() const { return room ? &*room : nullptr; }
		std::uint32_t memberId() const { return member; }
		int localSeat() const;
		const std::string& endpoint() const { return options.endpoint; }

		void setTeam(int team);
		void setReady(bool ready);
		void chat(const std::string& text);
		/// Leaves the room politely and closes the connection.
		void leave();

		/// Map download progress, 0..100, or -1 when no download is running.
		int downloadPercent() const;
		bool hasMap() const { return !cachedMap.empty(); }
		const std::string& mapFile() const { return cachedMap; }

		struct StartInfo
		{
			Online::MatchSetup setup;
			int seat = -1;
			std::string ticket;
			std::string mapFile;
			std::shared_ptr<Turn::TurnTransport> transport;
		};
		/// The host started (or let this guest rejoin) and the map is available.
		bool startReady() const { return start && hasMap(); }
		std::optional<StartInfo> takeStart();

		std::vector<std::string> takeChat() { return std::exchange(chatLines, {}); }
		bool takeChanged() { return std::exchange(changed, false); }

		/// After play: why the host ended the game ("host-left", "cancelled", "lost"), or
		/// empty when it did not.
		std::string endReason() const;

		/// Test hook: drops the connection as a network failure would.
		void dropConnection();

		/// The connection and frame routing, shared with the turn transport.
		struct Connection;

	private:
		void handleRoom(const nlohmann::json& message);
		void handleChunk(const MapChunk& chunk);
		void ensureMap(const std::string& hash, std::uint32_t bytes, bool savedGame);
		void requestChunks();
		void close(const std::string& why, const std::string& text = {});
		void send(const nlohmann::json& message);

		Options options;
		std::shared_ptr<Connection> connection;
		Phase currentPhase = Phase::Connecting;
		std::string reason, detail;
		bool helloSent = false;
		bool wantReady = false;
		std::uint32_t member = 0;
		std::optional<RoomState> room;
		std::vector<std::string> chatLines;
		bool changed = true;

		// Map download
		std::string downloadHash;
		bool downloadSave = false;
		std::string downloadBytes;
		std::uint32_t downloadTotal = 0;
		std::uint32_t nextRequest = 0;
		std::uint32_t received = 0;
		std::vector<bool> chunkDone;
		std::string cachedMap;
		std::string cachedHash;
		Online::MapCache& maps();
		std::unique_ptr<Online::OnlineStorage> privateStorage;
		std::unique_ptr<Online::MapCache> privateMaps;

		std::optional<StartInfo> start;
	};
}
