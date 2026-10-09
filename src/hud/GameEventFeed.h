// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD's game-event notifications, coalesced into one row per situation.
//
// The simulation rate-limits each event type in ticks, so at high game speeds
// it reports the same few situations many times per second. Instead of one
// line per report, the feed keeps one row per situation (an attack on one unit
// or building type in one area, conversions with one team, completions of one
// building type), counts the reports it has absorbed and keeps the row while
// the situation goes on. Rows expire on game time, with a wall-clock floor so
// they stay readable at any speed and remain while the game is paused.
//
// Pure client state: times are explicit arguments and the map distance is
// injected, so every case is exact and testable without a game.
#pragma once

#include "GameEvent.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// What the feed needs from a GameEvent.
struct GameEventFeedEvent
{
	GameEventType type;
	/// Unit type for unit attacks; building catalog ID for building attacks
	/// and completions; the other team's number for conversions.
	std::uint32_t subject;
	/// Simulation tick the event was raised on.
	std::uint32_t tick;
	int x, y;
	/// The event's message and colour, formatted when it arrived.
	std::string label;
	GAGCore::Color color;
};

class GameEventFeed
{
public:
	/// Hard cap on visible rows; a new situation evicts the least recently
	/// updated row.
	static constexpr std::size_t kMaxRows = 8;
	/// Attacks on the same unit or building type within this many tiles
	/// (warp-safe) of a row's latest position are the same situation.
	static constexpr int kRegionRadius = 16;
	/// Game ticks a row lingers after its last report (30 ticks per second at
	/// normal speed).
	static constexpr std::uint32_t kAttackLingerTicks = 250;
	static constexpr std::uint32_t kConversionLingerTicks = 250;
	static constexpr std::uint32_t kCompletedLingerTicks = 125;
	/// Wall-clock floor: a row stays at least this long after its last report,
	/// however fast the game runs.
	static constexpr std::uint64_t kMinVisibleMs = 5000;
	/// A row fades out over this long once both lifetimes have run out.
	static constexpr std::uint64_t kFadeMs = 1000;
	/// GoToEvent presses closer together than this step through the rows;
	/// a later press starts again from the most recent one.
	static constexpr std::uint64_t kJumpCycleMs = 3000;

	/// Warp-safe squared distance between two map positions.
	using DistanceSquared = std::function<std::int64_t(int, int, int, int)>;

	struct Row
	{
		GameEventType type;
		std::uint32_t subject;
		/// Latest report's position: where a click or GoToEvent jumps.
		int x, y;
		/// Reports coalesced into this row. The simulation rate-limits reports,
		/// so this is not a count of units.
		std::uint32_t count;
		std::uint32_t lastTick;
		std::uint64_t lastWallMs;
		/// Wall time at which the game-time lifetime was first seen to have run
		/// out; 0 while it has not.
		std::uint64_t tickLapsedAtMs;
		/// Stable identity, and the feed-wide sequence of its latest update.
		std::uint64_t id;
		std::uint64_t updateSequence;
		/// The latest report's message and colour.
		std::string label;
		GAGCore::Color color;
	};

	static std::uint32_t lingerTicks(GameEventType type)
	{
		switch (type)
		{
		case GEUnitUnderAttack:
		case GEBuildingUnderAttack:
			return kAttackLingerTicks;
		case GEUnitLostConversion:
		case GEUnitGainedConversion:
			return kConversionLingerTicks;
		case GEBuildingCompleted:
		case GESize:
			break;
		}
		return kCompletedLingerTicks;
	}

	/// Merge \a event into the row for its situation, or add a row on top.
	/// A merged row keeps its place, so rows do not shuffle at high speed.
	void ingest(const GameEventFeedEvent &event, std::uint64_t nowMs, const DistanceSquared &distanceSquared)
	{
		++sequence;
		for (Row &row : rowList)
		{
			if (!sameSituation(row, event, distanceSquared))
				continue;
			row.x = event.x;
			row.y = event.y;
			++row.count;
			row.lastTick = event.tick;
			row.lastWallMs = nowMs;
			row.tickLapsedAtMs = 0;
			row.updateSequence = sequence;
			row.label = event.label;
			row.color = event.color;
			return;
		}
		rowList.insert(rowList.begin(), Row{event.type, event.subject, event.x, event.y, 1, event.tick, nowMs, 0,
											++nextId, sequence, event.label, event.color});
		if (rowList.size() > kMaxRows)
		{
			auto stalest = std::min_element(rowList.begin() + 1, rowList.end(), [](const Row &a, const Row &b)
											{ return a.updateSequence < b.updateSequence; });
			rowList.erase(stalest);
		}
	}

	/// Drop rows whose game-time and wall-clock lifetimes have both run out and
	/// whose fade has finished.
	void expire(std::uint32_t simTick, std::uint64_t nowMs)
	{
		for (Row &row : rowList)
			if (!row.tickLapsedAtMs && simTick - row.lastTick > lingerTicks(row.type))
				row.tickLapsedAtMs = nowMs ? nowMs : 1;
		rowList.erase(std::remove_if(rowList.begin(), rowList.end(),
									 [&](const Row &row) { return opacity(row, nowMs) <= 0.f; }),
					  rowList.end());
	}

	/// 1 while the row is live, ramping to 0 over kFadeMs once it is done.
	static float opacity(const Row &row, std::uint64_t nowMs)
	{
		if (!row.tickLapsedAtMs)
			return 1.f;
		const std::uint64_t doneAt = std::max(row.lastWallMs + kMinVisibleMs, row.tickLapsedAtMs);
		if (nowMs <= doneAt)
			return 1.f;
		const std::uint64_t fading = nowMs - doneAt;
		if (fading >= kFadeMs)
			return 0.f;
		return 1.f - static_cast<float>(fading) / static_cast<float>(kFadeMs);
	}

	/// The row GoToEvent should jump to: the most recently updated row, then,
	/// on presses within kJumpCycleMs of each other, the next less recently
	/// updated row, wrapping round. Null when there are no rows.
	const Row *nextJumpTarget(std::uint64_t nowMs)
	{
		const bool continuing = jumpCycle.size() && nowMs - lastJumpMs < kJumpCycleMs;
		lastJumpMs = nowMs;
		if (continuing)
		{
			for (std::size_t step = 1; step <= jumpCycle.size(); ++step)
			{
				const std::size_t index = (jumpIndex + step) % jumpCycle.size();
				if (const Row *row = find(jumpCycle[index]))
				{
					jumpIndex = index;
					return row;
				}
			}
		}
		std::vector<const Row *> byRecency;
		for (const Row &row : rowList)
			byRecency.push_back(&row);
		std::sort(byRecency.begin(), byRecency.end(),
				  [](const Row *a, const Row *b) { return a->updateSequence > b->updateSequence; });
		jumpCycle.clear();
		for (const Row *row : byRecency)
			jumpCycle.push_back(row->id);
		jumpIndex = 0;
		return byRecency.empty() ? nullptr : byRecency.front();
	}

	/// Rows top to bottom, newest situation first.
	const std::vector<Row> &rows() const { return rowList; }

	/// Forget every row, as when a game starts or the viewed team changes.
	void clear(int team = -1)
	{
		rowList.clear();
		jumpCycle.clear();
		viewedTeam = team;
	}
	/// The team whose events the rows describe; -1 before the first.
	int team() const { return viewedTeam; }

private:
	static bool sameSituation(const Row &row, const GameEventFeedEvent &event, const DistanceSquared &distanceSquared)
	{
		if (row.type != event.type || row.subject != event.subject)
			return false;
		if (event.type != GEUnitUnderAttack && event.type != GEBuildingUnderAttack)
			return true;
		const std::int64_t radius = kRegionRadius;
		return distanceSquared(row.x, row.y, event.x, event.y) <= radius * radius;
	}

	const Row *find(std::uint64_t id) const
	{
		for (const Row &row : rowList)
			if (row.id == id)
				return &row;
		return nullptr;
	}

	std::vector<Row> rowList;
	std::uint64_t sequence = 0;
	std::uint64_t nextId = 0;
	std::vector<std::uint64_t> jumpCycle;
	std::size_t jumpIndex = 0;
	std::uint64_t lastJumpMs = 0;
	int viewedTeam = -1;
};
