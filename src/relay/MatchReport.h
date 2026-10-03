// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The JSON the relay sends the platform: RelayRegistration, RelayHeartbeat and
// RelayMatchEnded (platform/packages/protocol/src/relay.ts), plus helpers to read the
// map hash out of a MatchSetup document for the match record.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "MatchRecord.h"
#include "TicketVerifier.h"

namespace Relay
{
	struct RelayLoad
	{
		std::size_t matches = 0;
		std::size_t connections = 0;
	};

	struct RegistrationInfo
	{
		std::string relayId;
		std::string publicUrl;
		std::string region;
		std::string build;
		std::size_t maxMatches = 0;
	};

	std::string registrationJson(const RegistrationInfo& info, const RelayLoad& load, bool draining);
	std::string heartbeatJson(const std::string& relayId, const RelayLoad& load, bool draining,
	                          const std::vector<std::string>& activeMatchIds);

	/// RelayMatchEnded.reason.
	enum class EndReason
	{
		Completed, ///< a client reported the game finished (Quit reason GameFinished)
		Abandoned, ///< every human left without a finished game
		Aborted,   ///< the relay ended the match: drain timeout, shutdown or error
	};
	const char* endReasonName(EndReason reason);

	struct MatchEndInfo
	{
		std::string matchId;
		std::string relayId;
		SimVersion simVersion;
		std::int64_t startedAt = 0; ///< Unix seconds
		std::int64_t endedAt = 0;
		EndReason reason = EndReason::Abandoned;
	};

	/// Builds RelayMatchEnded from the finished record and its serialized bytes.
	std::string matchEndedJson(const MatchEndInfo& info, const Turn::MatchRecord& record,
	                           const std::vector<std::uint8_t>& recordBytes);

	std::string sha256Hex(const std::vector<std::uint8_t>& bytes);

	/// Reads MatchSetup.map.hash (lowercase SHA-256 hex). False if absent or invalid.
	bool setupMapHash(const std::string& setupJson, std::array<std::uint8_t, 32>& out);
}
