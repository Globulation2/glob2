// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The LAN room protocol (docs/multiplayer/lan.md). A LAN host runs the room and the
// turn relay (TurnSequencer) in-process; each guest keeps one WSS connection to it.
// Both protocols share that connection and NetConnection's framing: a 2-byte
// big-endian length, then a payload whose first byte is the message type. Turn
// messages use 0xA0-0xBF (TurnProtocol.h); the room uses 0xC0-0xCF:
//
//   0xC0 RoomJson  u32 length, then a UTF-8 JSON object with a "type" member
//   0xC1 MapChunk  32-byte SHA-256 of the decompressed map, u32 offset, u32 total
//                  (both into the gzip-compressed transfer bytes), then the bytes

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "MatchSetup.h"
#include "NetFrame.h"
#include "NetTransport.h"

namespace Lan
{
	/// Bumped whenever a room message or the room flow changes incompatibly.
	constexpr std::uint16_t ROOM_PROTOCOL_VERSION = 1;

	constexpr std::uint8_t MSG_ROOM_JSON = 0xC0;
	constexpr std::uint8_t MSG_MAP_CHUNK = 0xC1;
	constexpr std::uint8_t MSG_ROOM_FIRST = 0xC0;
	constexpr std::uint8_t MSG_ROOM_LAST = 0xCF;

	constexpr std::size_t MAX_FRAME_BYTES = 65535;
	constexpr std::size_t MAX_JSON_BYTES = 60000;
	constexpr std::size_t MAP_CHUNK_BYTES = 48 * 1024;
	constexpr std::size_t MAP_WINDOW_CHUNKS = 8;          ///< chunk requests a guest keeps in flight
	constexpr std::size_t MAX_MAP_BYTES = 256u << 20;      ///< compressed transfer limit
	constexpr std::size_t MAX_MAP_INFLATED_BYTES = 1u << 30;
	constexpr std::size_t MAX_NAME_BYTES = 32;
	constexpr std::size_t MAX_CHAT_BYTES = 256;
	/// Unsent frames one connection may hold (the turn log a rejoining guest needs
	/// is sent in one go).
	constexpr std::size_t MAX_OUTBOX_BYTES = 64u << 20;

	inline bool isTurnMessage(std::uint8_t type) { return type >= 0xA0 && type <= 0xBF; }
	inline bool isRoomMessage(std::uint8_t type) { return type >= MSG_ROOM_FIRST && type <= MSG_ROOM_LAST; }

	std::vector<std::uint8_t> encodeJson(const nlohmann::json& message);
	/// Decodes a RoomJson payload; nullopt if malformed (the caller drops the peer).
	std::optional<nlohmann::json> decodeJson(const std::vector<std::uint8_t>& payload);

	struct MapChunk
	{
		std::string hash; ///< lowercase hex
		std::uint32_t offset = 0;
		std::uint32_t total = 0;
		std::vector<std::uint8_t> bytes;
	};
	std::vector<std::uint8_t> encodeMapChunk(const std::string& hash, std::uint32_t offset, std::uint32_t total,
	                                         const std::uint8_t* data, std::size_t size);
	std::optional<MapChunk> decodeMapChunk(const std::vector<std::uint8_t>& payload);

	/// One connection, framed as NetConnection frames it, without NetConnection's
	/// 256-message receive cap (a rejoining guest receives the whole turn log at once).
	/// Outgoing frames wait in an outbox while the transport's own queue is full.
	class LanLink
	{
	public:
		explicit LanLink(std::unique_ptr<NetTransport> transport);
		~LanLink();
		/// Opens a client connection to a pinned LAN endpoint.
		static std::unique_ptr<LanLink> open(const std::string& endpoint);

		NetTransport::State state();
		bool connected() { return state() == NetTransport::State::Connected; }
		bool closed() { return state() == NetTransport::State::Closed; }
		/// Queues one payload; false (and the link closes) on overflow.
		bool send(const std::vector<std::uint8_t>& payload);
		/// The next complete payload, if any. A framing error closes the link.
		bool receive(std::vector<std::uint8_t>& payload);
		/// Sends what the transport will take now.
		void flush();
		bool outboxEmpty() const { return outbox.empty(); }
		/// As NetTransport::waitHandles; Ready while whole frames wait to be received.
		NetWaitStatus waitHandles(std::vector<NetWaitHandle>& handles) const;
		void close();
		std::string error() const;
		std::string peerAddress() const;

	private:
		void pump();
		std::unique_ptr<NetTransport> transport;
		NetFrame::Reader reader;
		std::deque<std::vector<std::uint8_t>> frames;
		std::deque<std::vector<std::uint8_t>> outbox;
		std::size_t outboxBytes = 0;
		std::string failure;
		bool failed = false;
	};

	/// A connected human in the room.
	struct Member
	{
		std::uint32_t id = 0; ///< 0 is the host
		std::string name;
		int seat = -1;
		bool ready = false;
		bool hasMap = false;
	};

	/// What the host broadcasts as {"type":"state", ...}: the MatchSetup the match
	/// will start from (seed 0 until the start), the map's display data, and the
	/// connected humans.
	struct RoomState
	{
		Online::MatchSetup setup;
		std::string mapName;
		std::string hostName;
		std::vector<std::array<std::uint8_t, 3>> teamColors;
		std::uint32_t mapBytes = 0; ///< compressed transfer size
		std::vector<Member> members;
		bool started = false;

		nlohmann::json toJson() const;
		/// Throws std::exception (including Online::MatchSetupError) when malformed.
		static RoomState fromJson(const nlohmann::json& value);
		const Member* member(std::uint32_t id) const;
		const Member* memberForSeat(int seat) const;
	};

	/// A short random token (hex) for LAN tickets and room identities.
	std::string randomToken(std::size_t bytes = 16);
	/// Steady-clock microseconds, the time base of the LAN host's relay.
	std::uint64_t nowMicros();
	/// Truncates to at most `limit` bytes without splitting a UTF-8 sequence.
	std::string clampUtf8(const std::string& text, std::size_t limit);
}
