// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Codecs for the binary turn protocol (docs/multiplayer/turn-protocol.md). Each
// message is a NetMessage, so NetConnection frames carry them unchanged; TurnCodec
// adds a strict byte-level entry point for the relay and TurnSession.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "NetMessage.h"
#include "TurnProtocol.h"

namespace Turn
{
	/// One sequenced order: execute `order` for `seat` at `tick`.
	struct TurnEntry
	{
		std::uint32_t tick = 0;
		std::uint8_t seat = 0;
		std::vector<std::uint8_t> order;
		bool operator==(const TurnEntry& o) const { return tick == o.tick && seat == o.seat && order == o.order; }
		bool operator!=(const TurnEntry& o) const { return !(*this == o); }
	};

	/// Bytes one entry occupies inside a TurnBundle body.
	inline std::size_t entryWireBytes(const TurnEntry& e) { return 4 + 1 + 2 + e.order.size(); }

	/// Common base: equality and formatting through the encoded bytes.
	class TurnMessage : public NetMessage
	{
	public:
		std::string format() const override;
		bool operator==(const NetMessage& rhs) const override;
	protected:
		virtual const char* name() const = 0;
	};

	class Hello : public TurnMessage
	{
	public:
		std::uint16_t protocolVersion = PROTOCOL_VERSION;
		std::string ticket;
		std::uint32_t haveHorizon = 0;

		Uint8 getMessageType() const override { return MSG_HELLO; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnHello"; }
	};

	class Welcome : public TurnMessage
	{
	public:
		std::uint16_t protocolVersion = PROTOCOL_VERSION;
		std::uint8_t seat = 0;
		std::uint32_t humanSeatMask = 0;
		std::uint32_t tickRateMilliHz = DEFAULT_TICK_RATE_MILLIHZ;
		std::uint8_t bundleInterval = DEFAULT_BUNDLE_INTERVAL;
		std::uint16_t checksumInterval = DEFAULT_CHECKSUM_INTERVAL;
		std::uint32_t relayTick = 0;
		std::uint32_t resumeFromTick = 0;
		std::uint32_t graceTicks = 0;
		std::uint32_t lastClientSequence = 0;

		Uint8 getMessageType() const override { return MSG_WELCOME; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnWelcome"; }
	};

	class Reject : public TurnMessage
	{
	public:
		RejectReason reason = RejectReason::Malformed;
		std::string detail;

		Uint8 getMessageType() const override { return MSG_REJECT; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnReject"; }
	};

	class OrderSubmit : public TurnMessage
	{
	public:
		std::uint32_t clientSequence = 0;
		std::vector<std::uint8_t> order;

		Uint8 getMessageType() const override { return MSG_ORDER_SUBMIT; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnOrderSubmit"; }
	};

	class TurnBundle : public TurnMessage
	{
	public:
		std::uint32_t fromTick = 0;
		std::uint32_t horizonTick = 0;
		std::vector<TurnEntry> entries;

		Uint8 getMessageType() const override { return MSG_TURN_BUNDLE; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
		/// Bytes this bundle occupies as a frame payload (type byte included).
		std::size_t payloadBytes() const;
		/// Throws std::ios_base::failure unless the invariants in the protocol hold.
		void validate() const;
	protected:
		const char* name() const override { return "TurnBundle"; }
	};

	class ChecksumReport : public TurnMessage
	{
	public:
		std::uint32_t tick = 0;
		std::uint32_t checksum = 0;

		Uint8 getMessageType() const override { return MSG_CHECKSUM_REPORT; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnChecksumReport"; }
	};

	struct SeatPresence
	{
		std::uint8_t seat = 0;
		PresenceState state = PresenceState::NotConnected;
		std::uint32_t graceRemainingTicks = 0;
		std::uint32_t lagTicks = 0;
		bool operator==(const SeatPresence& o) const
		{
			return seat == o.seat && state == o.state && graceRemainingTicks == o.graceRemainingTicks && lagTicks == o.lagTicks;
		}
	};

	class Presence : public TurnMessage
	{
	public:
		std::vector<SeatPresence> seats;

		Uint8 getMessageType() const override { return MSG_PRESENCE; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnPresence"; }
	};

	struct SeatRoundTrip
	{
		std::uint8_t seat = 0;
		/// The relay's smoothed round trip to the seat; 0 when not measured.
		std::uint32_t rttMicros = 0;
		bool operator==(const SeatRoundTrip& o) const { return seat == o.seat && rttMicros == o.rttMicros; }
	};

	/// Protocol version 2: each connected human seat's round trip to the relay, as the
	/// relay measures it on its own transport (WebSocket ping). Presentation only (the
	/// connection panel's Ping); sent with Presence to clients that speak version 2.
	class SeatLatency : public TurnMessage
	{
	public:
		std::vector<SeatRoundTrip> seats;

		Uint8 getMessageType() const override { return MSG_SEAT_LATENCY; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnSeatLatency"; }
	};

	class ResyncRequest : public TurnMessage
	{
	public:
		std::uint32_t fromTick = 0;

		Uint8 getMessageType() const override { return MSG_RESYNC_REQUEST; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnResyncRequest"; }
	};

	class DesyncNotice : public TurnMessage
	{
	public:
		std::uint32_t tick = 0;
		DesyncVerdict verdict = DesyncVerdict::Flagged;
		std::uint32_t divergedSeatMask = 0;

		Uint8 getMessageType() const override { return MSG_DESYNC_NOTICE; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnDesyncNotice"; }
	};

	class Quit : public TurnMessage
	{
	public:
		QuitReason reason = QuitReason::PlayerQuit;

		Uint8 getMessageType() const override { return MSG_QUIT; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnQuit"; }
	};

	class Ping : public TurnMessage
	{
	public:
		std::uint32_t nonce = 0;
		std::uint32_t executedTick = 0;

		Uint8 getMessageType() const override { return MSG_PING; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnPing"; }
	};

	class Pong : public TurnMessage
	{
	public:
		std::uint32_t nonce = 0;
		std::uint32_t relayTick = 0;
		std::uint32_t lastClientSequence = 0;

		Uint8 getMessageType() const override { return MSG_PONG; }
		void encodeData(GAGCore::OutputStream* stream) const override;
		void decodeData(GAGCore::InputStream* stream) override;
	protected:
		const char* name() const override { return "TurnPong"; }
	};

	/// Byte-level entry point shared by the relay and the client.
	namespace TurnCodec
	{
		/// Returns an empty message for the given id, or nullptr if it is not a turn id.
		std::shared_ptr<NetMessage> create(std::uint8_t messageType);
		/// Encodes a frame payload: type byte followed by the body. Throws
		/// std::length_error if the payload would exceed MAX_FRAME_BYTES.
		std::vector<std::uint8_t> encode(const NetMessage& message);
		/// Decodes one frame payload. Returns nullptr for anything malformed: an empty
		/// or oversized payload, a non-turn type, truncation, trailing bytes or a
		/// violated invariant. Never throws.
		std::shared_ptr<NetMessage> decode(const std::uint8_t* data, std::size_t size);
		inline std::shared_ptr<NetMessage> decode(const std::vector<std::uint8_t>& bytes)
		{
			return decode(bytes.data(), bytes.size());
		}
	}

	/// Splits sorted entries for [fromTick, horizonTick) into bundles of at most
	/// maxPayloadBytes each, breaking only at tick boundaries. Always returns at least one
	/// bundle when fromTick < horizonTick. A single tick larger than the limit still
	/// gets a bundle of its own; the sequencer's per-tick budget prevents that.
	std::vector<TurnBundle> splitIntoBundles(const std::vector<TurnEntry>& entries, std::uint32_t fromTick,
	                                         std::uint32_t horizonTick, std::size_t maxPayloadBytes = MAX_BUNDLE_BYTES);
}
