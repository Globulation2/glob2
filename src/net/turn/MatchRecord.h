// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The relay's persistent output for a match: setup, map hash, sequenced turns,
// checksum reports and presence events. The format is documented in
// docs/multiplayer/turn-protocol.md ("Match record").

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "TurnMessages.h"

namespace Turn
{
	struct ChecksumEntry
	{
		std::uint32_t tick = 0;
		std::uint8_t seat = 0;
		std::uint32_t checksum = 0;
		bool operator==(const ChecksumEntry& o) const { return tick == o.tick && seat == o.seat && checksum == o.checksum; }
	};

	enum class MatchEventKind : std::uint8_t
	{
		Connected = 1,
		Disconnected = 2,
		LeftByQuit = 3,
		LeftByGrace = 4,
		ToldToRejoin = 5,
		Resynced = 6,
		Flagged = 7,
	};
	constexpr std::uint8_t MATCH_EVENT_KIND_MAX = 7;

	struct MatchEvent
	{
		std::uint32_t tick = 0;
		std::uint8_t seat = 0;
		MatchEventKind kind = MatchEventKind::Connected;
		bool operator==(const MatchEvent& o) const { return tick == o.tick && seat == o.seat && kind == o.kind; }
	};

	class MatchRecordError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	struct MatchRecord
	{
		static constexpr std::uint16_t FORMAT_VERSION = 1;
		static constexpr std::uint32_t FLAG_DESYNC_FLAGGED = 1u << 0;
		static constexpr std::uint32_t FLAG_INCOMPLETE = 1u << 1;
		static constexpr std::size_t MAX_ID_BYTES = 256;
		static constexpr std::size_t MAX_SETUP_BYTES = 4u << 20;

		std::uint32_t flags = 0;
		std::string matchId;
		std::string simVersion;
		std::uint32_t tickRateMilliHz = DEFAULT_TICK_RATE_MILLIHZ;
		std::uint8_t bundleInterval = DEFAULT_BUNDLE_INTERVAL;
		std::uint16_t checksumInterval = DEFAULT_CHECKSUM_INTERVAL;
		std::uint32_t humanSeatMask = 0;
		std::uint32_t endTick = 0;
		std::string setupJson;
		std::array<std::uint8_t, 32> mapHash{};
		std::vector<TurnEntry> turns;        ///< sorted by (tick, seat); voice excluded
		std::vector<ChecksumEntry> reports;  ///< sorted by (tick, seat)
		std::vector<MatchEvent> events;      ///< sorted by tick

		bool operator==(const MatchRecord& o) const;

		/// Serializes; throws MatchRecordError if a field breaks a format limit.
		std::vector<std::uint8_t> serialize() const;
		/// Parses and validates; throws MatchRecordError on any defect.
		static MatchRecord parse(const std::vector<std::uint8_t>& bytes);

		void writeFile(const std::string& path) const;
		static MatchRecord readFile(const std::string& path);
	};

	/// CRC-32 (IEEE 802.3, reflected, as in zlib).
	std::uint32_t crc32(const std::uint8_t* data, std::size_t size);
}
