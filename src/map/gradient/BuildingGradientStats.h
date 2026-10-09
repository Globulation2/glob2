// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

#include "BuildingGradientSearch.h"

class Building;
class Game;
class Map;

// Diagnostics for building walking fields (Building::globalGradient). A Map
// owns one only when GLOB2_GRADIENT_STATS is set (Headless: --telemetry
// gradient-stats); every hook is guarded by that null pointer. The stats read
// simulation state and never write it, so a run's checksums, RNG, saves and
// replays are identical with or without them.
//
// Each field lifetime ends with one row: a rebuild or scheduled publication that
// replaces it, a drop (Building::resetPathfindGradients), an idle eviction or
// the end of the run. With the stats on, scheduled fields are published with
// only their seeds settled, so a lifetime's settled depth is what its readers
// needed, as the depth model is fitted on; the depth never changes a result.
class BuildingGradientStats
{
  public:
	enum class Reason : std::uint8_t { Null, Dirty, Generation, Clearing, Stuck, Scheduled, Other, Count };
	enum class Event : std::uint8_t { Rebuild, Drop, Evict, End, Count };

	static constexpr int DEPTH_BINS = BuildingGradientSearch::DEPTH_BINS;

	// The building and its team when a lifetime's depth was decided: at staging
	// for a scheduled field, at the build itself otherwise. These are the inputs
	// a depth prediction could read, each in O(1).
	struct Context
	{
		bool known = false;
		bool staged = false; // taken when a scheduled job was staged
		std::int16_t level = 0;
		bool site = false;
		std::uint8_t construction = 0; // BuildingStateRecord::ConstructionResultState
		std::int8_t progress = -1; // delivered/needed material quartile 0..3 on sites
		std::uint16_t units = 0, buildings = 0;
		// The depth model's inputs: settledCostHint, the depth the last replaced
		// lifetime's readers required, and the serving field's reader demand; -1 if
		// unknown.
		std::int32_t previousHint = -1, servingSettled = -1;
	};
	static Context context(const Building &building, int slot);
	// The next fieldRebuilding call uses this context, taken at staging.
	void setPendingContext(const Context &value) { pendingContext = value; }

	struct Row
	{
		std::uint32_t tick = 0;
		std::uint16_t gid = 0;
		std::uint8_t team = 0, route = 0, swim = 0;
		Event event = Event::Rebuild;
		Reason reason = Reason::Other; // why the rebuild happened (rebuild rows)
		std::uint16_t type = 0; // index into typeNames
		bool hasPrevious = false, prevComplete = false, prevSearch = false, prevLocked = false;
		Reason lifetimeReason = Reason::Other; // how the ending lifetime started
		bool lifetimeKnown = false;
		std::int64_t age = -1; // ticks since the ending lifetime's build
		std::int32_t prevSettledCost = -1;
		std::uint64_t prevPopped = 0, prevQueries = 0, prevExtensions = 0;
		std::array<std::uint32_t, DEPTH_BINS> poppedAtDepth{};
		Context context; // of the ending lifetime, captured at its start
	};

	static bool enabledByEnvironment();

	void clearingGoalGone() { ++clearingGoalGoneCount; }

	// The next updateGlobalGradient call is attributed to this reason.
	void setPendingReason(Reason reason) { pendingReason = reason; }

	// Called by Map::updateGlobalGradient before the field is reinitialized.
	void fieldRebuilding(const Map &map, const Building &building, int slot, int access, std::uint32_t tick,
						 std::uint32_t topologyGeneration);
	// Called before a live field is released.
	void fieldReleased(const Building &building, int slot, Event event, std::uint32_t tick);
	// Emits End rows for every live field.
	void finish(const Game &game);

	void writeCsv(std::ostream &out) const;
	void writeJson(std::ostream &out) const;

	const std::vector<Row> &rows() const { return rowList; }
	std::uint64_t rebuilds(Reason reason) const { return rebuildCounts[unsigned(reason)]; }

	static const char *name(Reason reason);
	static const char *name(Event event);

  private:
	struct Lifetime
	{
		std::uint32_t start = 0;
		Reason reason = Reason::Other;
		Context context;
	};

	Row previousRow(const Building &building, int slot, Event event, std::uint32_t tick, bool hasPrevious, int access);
	void closeLifetime(Row &row, const Building &building, int slot);
	std::uint16_t typeIndex(const Building &building);
	static std::uint64_t key(const Building &building, int slot);

	int mapWidth = 0, mapHeight = 0;

	Reason pendingReason = Reason::Other;
	Context pendingContext;
	std::vector<Row> rowList;
	std::vector<std::string> typeNames;
	std::unordered_map<std::string, std::uint16_t> typeIndices;
	std::unordered_map<std::uint64_t, Lifetime> lifetimes;

	std::array<std::uint64_t, std::size_t(Reason::Count)> rebuildCounts{};
	std::array<std::uint64_t, std::size_t(Event::Count)> eventCounts{};
	// Popped entries of finished lifetimes, by how each lifetime started.
	std::array<std::uint64_t, std::size_t(Reason::Count)> poppedByReason{};
	std::uint64_t poppedTotal = 0, poppedUnknownLifetime = 0;
	std::uint64_t dirtyWithGeneration = 0;
	std::uint64_t clearingGoalGoneCount = 0;
};
