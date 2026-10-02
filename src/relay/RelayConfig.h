// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// glob2-relay configuration, read from GLOB2_RELAY_* environment variables. The full
// list with defaults is in docs/multiplayer/relay.md; keep the two in step.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Relay
{
	struct RelayConfig
	{
		// Listener
		std::string bindAddress = "0.0.0.0";
		std::uint16_t port = 7495;
		std::string route = "/relay";
		std::string tlsCertificateFile; ///< TLS is enabled when both files are set
		std::string tlsKeyFile;
		std::vector<std::string> allowedOrigins; ///< browsers' Origin must be listed; "*" allows any
		std::vector<std::string> trustedProxies; ///< peers whose X-Forwarded-For is believed
		std::string metricsToken;                ///< bearer token for /metrics when set

		// Limits
		std::size_t maxConnections = 2048;
		std::size_t maxConnectionsPerAddress = 32;
		std::size_t maxMatches = 200;
		unsigned helloTimeoutSeconds = 10;
		unsigned framesPerSecond = 200;  ///< sustained inbound frame rate per connection
		unsigned frameBurst = 600;       ///< token bucket size
		std::size_t maxOutgoingBytes = 8u << 20; ///< queued bytes before a slow reader is dropped

		// Matches
		unsigned graceSeconds = 180;
		/// Load barrier: a match's clock starts when every human seat has connected
		/// (clients connect once loaded), or this long after the first one; 0 starts
		/// it at the first connection.
		unsigned loadWaitSeconds = 60;
		unsigned drainTimeoutSeconds = 4 * 3600;
		/// WebSocket ping interval for the per-seat round-trip telemetry; 0 disables.
		unsigned rttPingMillis = 2000;

		// Tickets
		std::string jwksFile;  ///< static JWKS; disables fetching
		std::string jwksUrl;   ///< default: the registration response, then <platform>/.well-known/jwks.json
		unsigned jwksRefreshSeconds = 600;
		unsigned jwksMinRefreshSeconds = 10; ///< unknown-kid refreshes are at most this often
		std::int64_t leewaySeconds = 30;
		std::string issuer;    ///< when set, tickets' iss must match

		// Platform
		std::string platformUrl; ///< empty: no registration, heartbeat or upload
		std::string platformCaFile;
		std::string relayKey;    ///< bearer token for /internal calls
		std::string relayId;
		std::string publicUrl;
		std::string region = "default";
		std::string spoolDirectory; ///< records are written here before upload
		unsigned uploadAttempts = 8;

		/// Reads the environment. Throws std::invalid_argument on a bad value.
		static RelayConfig fromEnvironment();
		/// Same, from an explicit map (tests).
		static RelayConfig fromMap(const std::map<std::string, std::string>& values);
		bool tlsEnabled() const { return !tlsCertificateFile.empty(); }
	};
}
