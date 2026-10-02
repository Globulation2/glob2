// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Match admission bookkeeping for the relay, without sockets: which matches are live,
// which have ended (so a late ticket cannot start a second match with the same id),
// and whether a verified ticket agrees with the match it names. The relay creates a
// match on the first valid ticket for its matchId; every later ticket must carry the
// same simVersion and humanSeats.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "TicketVerifier.h"

namespace Relay
{
	enum class Admission
	{
		Join,              ///< the match exists and the ticket agrees with it
		Create,            ///< first valid ticket for this match
		Ended,             ///< the match already ended on this relay
		Draining,          ///< no new matches while draining
		Full,              ///< maxMatches reached
		SimVersionDiffers, ///< the ticket disagrees with the match's other tickets
		HumanSeatsDiffer,
	};
	const char* admissionName(Admission a);

	class MatchDirectory
	{
	public:
		/// Decides what a verified ticket may do. Does not change anything.
		Admission check(const TicketClaims& claims, bool draining, std::size_t maxMatches) const;

		/// Records a new live match.
		void create(const TicketClaims& claims);
		/// Notes a ticket's expiry, so the tombstone outlives every ticket seen.
		void sawTicket(const TicketClaims& claims);
		/// Moves a live match to the ended set until no ticket for it can still be
		/// valid: its latest ticket expiry plus leeway, and at least minimumSeconds.
		void end(const std::string& matchId, std::int64_t nowSeconds, std::int64_t leewaySeconds,
		         std::int64_t minimumSeconds = 3600);
		/// Forgets tombstones that have expired.
		void prune(std::int64_t nowSeconds);

		bool live(const std::string& matchId) const { return matches.count(matchId) != 0; }
		bool ended(const std::string& matchId) const { return tombstones.count(matchId) != 0; }
		std::size_t liveCount() const { return matches.size(); }
		std::vector<std::string> liveIds() const;

	private:
		struct Entry
		{
			SimVersion simVersion;
			std::uint32_t humanSeats = 0;
			std::int64_t latestExpiry = 0;
		};
		std::map<std::string, Entry> matches;
		std::map<std::string, std::int64_t> tombstones; ///< matchId -> forget after
	};
}
