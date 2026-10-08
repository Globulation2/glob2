// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "field/GradientBucket.h"
#include "map/TerrainType.h"
#include "map/TerrainRegistry.h"
#include "field/TerrainGradientWorkspace.h"

class Map;

// Resumable version of the building field's zero-cost, multi-source Dijkstra.
// Obstacles/goals are already frozen in the initialized gradient. Weighted swim
// classes additionally retain water costs, so a pause never mixes map snapshots.
// Callers must resolve a cell before reading it; completion preserves the old
// full-field API and save representation without refreshing the field's age.
class BuildingGradientSearch
{
	std::array<GradientBucket, GradientBucket::COUNT> buckets;
	std::shared_ptr<const TerrainRegistry> registry;
	std::shared_ptr<const TerrainMovementSnapshot> profiles;
	std::shared_ptr<const std::vector<std::uint8_t>> water;
	std::unique_ptr<TerrainGradientWorkspace> custom;
	unsigned terrainBuckets = 64;
	bool modifiedCosts = false;
	std::uint16_t *gradient = nullptr;
	std::size_t cells = 0, pending = 0;
	int currentCost = 0, swimClass = 0;
	std::uint64_t popped = 0;
	int widthMask = 0, heightMask = 0;

  public:
	// Diagnostic depth histogram: popped entries per DEPTH_BIN_COST cost band,
	// the last bin collecting everything deeper. Never read by the simulation.
	static constexpr int DEPTH_BINS = 32;
	static constexpr int DEPTH_BIN_COST = 80; // eight land tiles
	static constexpr int depthBin(int cost)
	{
		return cost / DEPTH_BIN_COST < DEPTH_BINS - 1 ? cost / DEPTH_BIN_COST : DEPTH_BINS - 1;
	}
	// Plain diagnostic counters, reset by begin() and clearForReuse():
	// resolve() calls, calls that expanded at least one layer, and popped
	// entries by depth band.
	std::uint64_t queries = 0, extensions = 0;
	std::array<std::uint64_t, DEPTH_BINS> poppedAtDepth{};

	void begin(const Map &map, std::uint16_t *seeded, int swim);
	// target == cells finishes the field. A whole cost layer is completed to
	// preserve equal-distance sidesteps as well as the requested scalar value.
	void resolve(std::size_t target);
	void finish() { resolve(cells); }
	bool complete() const { return pending == 0; }
	bool resolved(std::size_t target) const;
	std::uint64_t poppedEntries() const { return popped; }
	// Every cost below this value is settled; the next layer to expand.
	int settledCost() const { return currentCost; }
	std::size_t retainedBytes() const;
	void clearForReuse();
};

// Bounded scratch reuse; neither the search nor its queues are serialized.
std::unique_ptr<BuildingGradientSearch> acquireBuildingGradientSearch();
void recycleBuildingGradientSearch(std::unique_ptr<BuildingGradientSearch> search);
void clearBuildingGradientSearchPool();
