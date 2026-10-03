// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The relay's network side (docs/multiplayer/relay.md): one listener serving the match
// WebSocket route plus /healthz, /readyz and /metrics, optional TLS, connection and
// message limits, Origin checks, and graceful drain. Each match is a TurnSequencer
// created on the first valid ticket for its matchId and driven by a timer. The relay
// runs on a single event-loop thread, which serializes every call into a sequencer;
// operators scale out with more relay replicas.

#include <boost/asio/io_context.hpp>

#include <functional>
#include <memory>

#include "PlatformLink.h"
#include "RelayConfig.h"

namespace Relay
{
	class JwksStore;
	class RelayMetrics;

	class RelayServer
	{
	public:
		RelayServer(boost::asio::io_context& io, const RelayConfig& config, RelayMetrics& metrics, JwksStore& jwks,
		            PlatformLink& platform);
		~RelayServer();

		/// Binds the listener and starts accepting. Returns the bound port (useful
		/// with GLOB2_RELAY_PORT=0). Throws if the address cannot be bound.
		std::uint16_t start();
		/// Stops starting new matches; existing matches (and their reconnects) carry on
		/// until they end or the drain timeout aborts them. onDrained runs once no match
		/// is left and every record has been handed to the platform link.
		void beginDrain();
		/// Ends every match now as aborted (second signal, drain timeout).
		void abortAll();
		/// Closes the listener.
		void stopListening();
		bool draining() const;

		LoadSnapshot snapshot() const;
		std::function<void()> onDrained;

		struct Impl;

	private:
		std::shared_ptr<Impl> impl;
	};
}
