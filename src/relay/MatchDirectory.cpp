// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "MatchDirectory.h"

#include <algorithm>

namespace Relay
{
const char* admissionName(Admission a)
{
	switch (a)
	{
	case Admission::Join: return "join";
	case Admission::Create: return "create";
	case Admission::Ended: return "ended";
	case Admission::Draining: return "draining";
	case Admission::Full: return "full";
	case Admission::SimVersionDiffers: return "sim_version_differs";
	case Admission::HumanSeatsDiffer: return "human_seats_differ";
	}
	return "unknown";
}

Admission MatchDirectory::check(const TicketClaims& claims, bool draining, std::size_t maxMatches) const
{
	if (tombstones.count(claims.matchId))
		return Admission::Ended;
	auto it = matches.find(claims.matchId);
	if (it != matches.end())
	{
		switch (checkAgreement(it->second.simVersion, it->second.humanSeats, claims))
		{
		case MatchAgreement::SimVersionDiffers: return Admission::SimVersionDiffers;
		case MatchAgreement::HumanSeatsDiffer: return Admission::HumanSeatsDiffer;
		case MatchAgreement::Agrees: return Admission::Join;
		}
	}
	// A draining relay finishes its matches, including reconnects, but starts none.
	if (draining)
		return Admission::Draining;
	if (matches.size() >= maxMatches)
		return Admission::Full;
	return Admission::Create;
}

void MatchDirectory::create(const TicketClaims& claims)
{
	Entry e;
	e.simVersion = claims.simVersion;
	e.humanSeats = claims.humanSeatMask;
	e.latestExpiry = claims.expiresAt;
	matches[claims.matchId] = e;
}

void MatchDirectory::sawTicket(const TicketClaims& claims)
{
	auto it = matches.find(claims.matchId);
	if (it != matches.end())
		it->second.latestExpiry = std::max(it->second.latestExpiry, claims.expiresAt);
}

void MatchDirectory::end(const std::string& matchId, std::int64_t nowSeconds, std::int64_t leewaySeconds,
                         std::int64_t minimumSeconds)
{
	auto it = matches.find(matchId);
	if (it == matches.end())
		return;
	tombstones[matchId] = std::max(it->second.latestExpiry + leewaySeconds, nowSeconds + minimumSeconds);
	matches.erase(it);
}

void MatchDirectory::prune(std::int64_t nowSeconds)
{
	for (auto it = tombstones.begin(); it != tombstones.end();)
		it = it->second < nowSeconds ? tombstones.erase(it) : std::next(it);
}

std::vector<std::string> MatchDirectory::liveIds() const
{
	std::vector<std::string> ids;
	for (const auto& m : matches)
		ids.push_back(m.first);
	return ids;
}
}
