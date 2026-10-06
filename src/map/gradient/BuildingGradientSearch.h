// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <utility>

#include "field/GradientBucket.h"
#include "field/GradientCosts.h"
#include "map/TerrainType.h"
#include "map/TerrainRegistry.h"
#include "field/TerrainGradientWorkspace.h"

struct BuildingGradientInputs
{
 std::shared_ptr<const std::vector<TerrainType>> terrain;
 std::shared_ptr<const TerrainRegistry> registry = TerrainRegistry::builtins();
 std::shared_ptr<const TerrainMovementSnapshot> profiles;
 std::shared_ptr<const std::vector<std::uint8_t>> water;
 bool modified = false;
 unsigned buckets = 64;
};

class Map;
class BuildingGradientDiagnostics;

// Resumable multi-source Dijkstra over caller-owned fields and frozen costs.
// Obstacles/goals are already frozen in the initialized gradient. Weighted swim
// classes additionally retain water costs, so a pause never mixes map snapshots.
// Callers must resolve a cell before reading it; completion preserves the old
// full-field API and save representation without refreshing the field's age.
class BuildingGradientSearch
{
	std::array<GradientBucket, GradientBucket::COUNT> buckets;
	std::shared_ptr<const std::vector<TerrainType>> terrain;
	std::shared_ptr<const TerrainRegistry> registry;
	std::shared_ptr<const TerrainMovementSnapshot> profiles;
	std::shared_ptr<const std::vector<std::uint8_t>> water;
	std::unique_ptr<TerrainGradientWorkspace> custom;
	unsigned terrainBuckets = 64;
	bool modifiedCosts = false;
	std::uint16_t *gradient = nullptr;
	std::size_t cells = 0, pending = 0, nextSeed = 0;
	std::vector<std::pair<int, std::uint32_t>> deferredSeeds;
	int costLimit = gradient_kernel::COST_LIMIT;
	int currentCost = 0, swimClass = 0;
	std::uint64_t popped = 0;
	int widthMask = 0, heightMask = 0;
	const Map *sourceMap = nullptr;
	int buildingId = -1;
	std::uint32_t snapshotGeneration = 0;
	void advance(std::size_t target);

  public:
	void begin(const Map &map, std::uint16_t *seeded, int swim, int gid = -1);
 void beginFrozen(int width, int height, std::uint16_t *seeded, int swim,
                  const BuildingGradientInputs &inputs, int limit, int settled = 0);
 void beginFrozen(int width, int height, std::uint16_t *seeded, int swim,
                  std::shared_ptr<const std::vector<TerrainType>> costs, bool modified,
                  int limit, int settled = 0)
 { BuildingGradientInputs inputs; inputs.terrain=std::move(costs); inputs.modified=modified;
   beginFrozen(width,height,seeded,swim,inputs,limit,settled); }
 void attach(const Map &map, std::uint16_t *field, int gid, std::uint32_t generation);
	// target == cells finishes the field. A whole cost layer is completed to
	// preserve equal-distance sidesteps as well as the requested scalar value.
	void resolve(std::size_t target, const char *caller = "query");
	void finish(const char *caller = "full_api") { resolve(cells, caller); }
	bool complete() const { return pending == 0 && nextSeed == deferredSeeds.size(); }
	bool resolved(std::size_t target) const;
	std::uint64_t poppedEntries() const { return popped; }
	int settledCost() const { return complete() ? -1 : currentCost; }
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
