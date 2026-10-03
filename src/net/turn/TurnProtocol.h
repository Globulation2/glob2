// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Constants shared by the turn relay (TurnSequencer) and client (TurnSession). The
// protocol is described in docs/multiplayer/turn-protocol.md; keep the two in step.

#include <cstddef>
#include <cstdint>

namespace Turn
{
	/// Bumped whenever an encoding or relay behaviour clients rely on changes.
	constexpr std::uint16_t PROTOCOL_VERSION = 1;

	/// Message ids. The 0xA0-0xBF range is reserved for the turn protocol in
	/// NetMessageType.h so YOG message churn never renumbers it.
	enum MessageId : std::uint8_t
	{
		MSG_HELLO = 0xA0,
		MSG_WELCOME = 0xA1,
		MSG_REJECT = 0xA2,
		MSG_ORDER_SUBMIT = 0xA3,
		MSG_TURN_BUNDLE = 0xA4,
		MSG_CHECKSUM_REPORT = 0xA5,
		MSG_PRESENCE = 0xA6,
		MSG_RESYNC_REQUEST = 0xA7,
		MSG_DESYNC_NOTICE = 0xA8,
		MSG_QUIT = 0xA9,
		MSG_PING = 0xAA,
		MSG_PONG = 0xAB,
		MSG_FIRST = MSG_HELLO,
		MSG_LAST = MSG_PONG,
	};

	enum class RejectReason : std::uint8_t
	{
		ProtocolVersion = 1,
		BadTicket = 2,
		SeatTaken = 3,
		SeatLeft = 4,
		MatchOver = 5,
		Malformed = 6,
		Flooding = 7,
	};
	constexpr std::uint8_t REJECT_REASON_MAX = 7;

	enum class PresenceState : std::uint8_t
	{
		NotConnected = 0,
		Connected = 1,
		Lagging = 2,
		Reconnecting = 3,
		Resyncing = 4,
		Left = 5,
	};
	constexpr std::uint8_t PRESENCE_STATE_MAX = 5;

	enum class DesyncVerdict : std::uint8_t
	{
		Rejoin = 1,
		Flagged = 2,
	};
	constexpr std::uint8_t DESYNC_VERDICT_MAX = 2;

	enum class QuitReason : std::uint8_t
	{
		PlayerQuit = 0,
		GameFinished = 1,
	};
	constexpr std::uint8_t QUIT_REASON_MAX = 1;

	// Timing defaults.
	constexpr std::uint32_t DEFAULT_TICK_RATE_MILLIHZ = 25000; // 25 ticks/s
	constexpr std::uint8_t DEFAULT_BUNDLE_INTERVAL = 2;
	constexpr std::uint16_t DEFAULT_CHECKSUM_INTERVAL = 25;
	constexpr std::uint64_t DEFAULT_GRACE_MICROS = 180ull * 1000000ull;

	// Size limits. Decoders reject anything outside these.
	constexpr std::size_t MAX_FRAME_BYTES = 65535; // NetConnection's u16 length prefix
	constexpr std::size_t MAX_ORDER_BYTES = 4096;
	constexpr std::size_t MAX_TICKET_BYTES = 8192;
	constexpr std::size_t MAX_REJECT_DETAIL_BYTES = 1024;
	constexpr std::size_t MAX_TICK_ORDER_BYTES = 30000;
	constexpr std::size_t MAX_BUNDLE_BYTES = 60000;
	constexpr unsigned MAX_SEATS = 32; // The engine's waiting mask is 32 bits.

	// Order type ids the relay inspects (mirrors src/net/NetConsts.h; a unit test
	// checks they agree, so the relay need not include engine headers).
	constexpr std::uint8_t ORDER_TYPE_NULL = 51;
	constexpr std::uint8_t ORDER_TYPE_PLAYER_QUIT = 67;
	constexpr std::uint8_t ORDER_TYPE_VOICE = 72;
	constexpr std::uint8_t ORDER_TYPE_ADJUST_LATENCY = 100;

	/// The wire bytes of PlayerQuitsGameOrder(seat): type byte then big-endian Sint32.
	inline void encodePlayerQuitOrder(std::uint8_t seat, std::uint8_t out[5])
	{
		out[0] = ORDER_TYPE_PLAYER_QUIT;
		out[1] = 0; out[2] = 0; out[3] = 0; out[4] = seat;
	}

	/// Converts a tick count to microseconds since match start at the given rate.
	inline std::uint64_t ticksToMicros(std::uint64_t ticks, std::uint32_t rateMilliHz)
	{
		return ticks * 1000000000ull / rateMilliHz;
	}
	/// The tick in progress at elapsed microseconds since match start.
	inline std::uint64_t microsToTicks(std::uint64_t micros, std::uint32_t rateMilliHz)
	{
		return micros * rateMilliHz / 1000000000ull;
	}
}
