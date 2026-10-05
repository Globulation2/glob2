// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "field/GradientBucket.h"
#include "map/TerrainType.h"

class Map;
class BuildingGradientDiagnostics;

// Resumable version of the building field's zero-cost, multi-source Dijkstra.
// Obstacles/goals are already frozen in the initialized gradient. Weighted swim
// classes additionally retain water costs, so a pause never mixes map snapshots.
// Callers must resolve a cell before reading it; completion preserves the old
// full-field API and save representation without refreshing the field's age.
class BuildingGradientSearch
{
	std::array<GradientBucket, GradientBucket::COUNT> buckets;
	std::shared_ptr<const std::vector<TerrainType>> terrain;
	bool modifiedCosts = false;
	std::uint16_t *gradient = nullptr;
	std::size_t cells = 0, pending = 0;
	int currentCost = 0, swimClass = 0;
	std::uint64_t popped = 0;
	int widthMask = 0, heightMask = 0;
	const Map *sourceMap = nullptr;
	int buildingId = -1;
	std::uint32_t snapshotGeneration = 0;
	void advance(std::size_t target);

  public:
	void begin(const Map &map, std::uint16_t *seeded, int swim, int gid = -1);
	// target == cells finishes the field. A whole cost layer is completed to
	// preserve equal-distance sidesteps as well as the requested scalar value.
	void resolve(std::size_t target, const char *caller = "query");
	void finish(const char *caller = "full_api") { resolve(cells, caller); }
	bool complete() const { return pending == 0; }
	bool resolved(std::size_t target) const;
	std::uint64_t poppedEntries() const { return popped; }
	std::size_t retainedBytes() const;
	// Complete a detached copy with the original frozen costs. No live fields,
	// ages, call lists or diagnostic collectors are touched.
	std::vector<std::uint16_t> completePrivateSnapshot() const;
	void clearForReuse();
};

// Bounded scratch reuse; neither the search nor its queues are serialized.
std::unique_ptr<BuildingGradientSearch> acquireBuildingGradientSearch();
void recycleBuildingGradientSearch(std::unique_ptr<BuildingGradientSearch> search);
void clearBuildingGradientSearchPool();
