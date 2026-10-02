// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Deterministic checks for orders that arrive from other machines in a turn-protocol
// game. The relay passes each seat's order bytes through unchanged, so a modified
// client (or relay) can sequence any bytes for its seat. Every client and the verifier
// run the same check on the same order against the same game state, at the same point
// of the same tick, so they all reach the same verdict and execute the same thing: an
// order that fails becomes a NullOrder everywhere.
//
// The check uses only the game state and the seat the relay's bundle attributes the
// order to, never an identity the order claims for itself. AI orders, single player,
// replays and legacy NetEngine games do not go through it.

#include <array>
#include <cstdint>

class Game;
class Order;

namespace OrderValidation
{
	enum class Verdict : std::uint8_t
	{
		Accepted,
		/// The order made sense when it was issued but no longer applies, e.g. it
		/// cancels an upgrade that has finished in the meantime. A real client can
		/// produce these through network latency, so they do not suggest cheating.
		Stale,
		/// No unmodified client sends this: a wrong team or player, a field outside
		/// the range the user interface can produce, an undecodable payload.
		Rejected,
	};

	enum class Reason : std::uint8_t
	{
		None,
		Undecodable,      ///< the bytes are not an order (unknown type, bad length)
		NotPermitted,     ///< an order type clients never submit in a turn game
		WrongTeam,        ///< acts for a team that is not the sender's
		WrongPlayer,      ///< acts for a player that is not the sender
		ForeignBuilding,  ///< names a building id of another team, or no valid id
		BadBuildingType,  ///< a building type that cannot be placed
		OutOfRange,       ///< a count, radius, ratio, level or position out of range
		BadMode,          ///< an unknown brush mode or message type
		BadVoice,         ///< a voice packet larger than the recorder produces
		BadState,         ///< the building is not in a state the order applies to
		Count
	};
	constexpr std::size_t REASON_COUNT = static_cast<std::size_t>(Reason::Count);

	struct Result
	{
		Verdict verdict = Verdict::Accepted;
		Reason reason = Reason::None;
	};

	/// Stable lowercase names for reports (result.json, verdict.json, logs).
	const char* name(Reason reason);
	const char* name(Verdict verdict);

	/// Checks `order`, which the relay sequenced for `senderPlayer`, against the game
	/// state at the moment it would execute. `senderPlayer` must be a valid player.
	Result validate(const Game& game, int senderPlayer, Order& order);

	/// Largest frame count a voice packet may carry: VoiceRecorder flushes after
	/// 19200 samples (120 Speex frames of 160) plus the frame that crossed the limit.
	constexpr int MAX_VOICE_FRAMES = 128;
	/// Largest flag radius an OrderCreate may carry. The settings screen offers 0-20;
	/// scripts may ask for up to 255 (ScriptOrders.cpp), which is what this allows.
	constexpr int MAX_CREATE_FLAG_RADIUS = 255;

	/// Per-seat counts of the human orders a turn session checked. Identical on every
	/// client and in the verifier for the orders in the match record; voice packets are
	/// left out of the record, so they are counted separately and only live.
	struct SeatAudit
	{
		std::uint32_t accepted = 0;
		std::uint32_t stale = 0;
		std::uint32_t rejected = 0;
		std::uint32_t voiceRejected = 0;
		/// Executed tick of the first rejected order, or UINT32_MAX.
		std::uint32_t firstRejectedTick = UINT32_MAX;
		std::array<std::uint32_t, REASON_COUNT> reasons{};
	};

	struct Audit
	{
		static constexpr std::size_t SEATS = 32;
		std::array<SeatAudit, SEATS> seats{};

		void record(int seat, std::uint32_t tick, bool voice, Result result);
		std::uint32_t totalRejected() const;
	};
}
