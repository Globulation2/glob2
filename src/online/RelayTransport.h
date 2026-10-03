// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The client side of a relay connection (docs/multiplayer/turn-protocol.md, "Relay"):
// a Turn::TurnTransport over a binary WebSocket to the relayUrl of a match ticket.
// Each turn-protocol frame travels as a 2-byte big-endian length and the payload; the
// binary messages form one byte stream, so frames may be split or coalesced.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "NetFrame.h"
#include "NetTransport.h"
#include "TurnSession.h"

namespace Online
{
	/// Splits the relay byte stream into frame payloads (NetFrame, as every
	/// u16-framed stream does). Pure, for tests.
	using RelayFrameReader = NetFrame::Reader;

	/// Length prefix plus payload; empty if the payload exceeds 65535 bytes.
	std::vector<std::uint8_t> relayFrame(const std::vector<std::uint8_t>& payload);

	/// Connections of destroyed RelayTransports that still had frames to write, or
	/// that sent a Quit, stay open (reading and dropping what arrives) until the
	/// frames are out and, after a Quit, until the relay closes the connection; at
	/// most `LINGER_MS`. Online::pump() drives them, and the shutdown screen waits for
	/// them (lingeringRelayConnections() == 0) before the process exits.
	constexpr std::uint32_t LINGER_MS = 3000;
	void pumpLingeringRelayConnections();
	std::size_t lingeringRelayConnections();

	class RelayTransport : public Turn::TurnTransport
	{
	public:
		using Factory = std::function<std::unique_ptr<NetTransport>()>;
		/// relayUrl: the ticket's wss:// URL. The factory defaults to makeNetTransport().
		explicit RelayTransport(std::string relayUrl, Factory factory = {});
		~RelayTransport() override;

		State state() override;
		void connect() override;
		void close() override;
		bool send(const std::vector<std::uint8_t>& payload) override;
		bool receive(std::vector<std::uint8_t>& payload) override;
		/// Runs the connection's pending I/O now, so a frame queued by send() is written
		/// without waiting for the next receive() or state() poll.
		void flush() override;

		/// The last transport error, for diagnostics.
		std::string error() const;

	private:
		void pump();

		std::string url;
		Factory factory;
		std::unique_ptr<NetTransport> link;
		RelayFrameReader reader;
		std::string lastError;
		bool sentQuit = false;
	};
}
