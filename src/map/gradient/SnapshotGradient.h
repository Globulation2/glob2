// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/snapshot/WorldSnapshot.h"
#include "field/GradientWorkspace.h"
#include <vector>
#include "Team.h"
#include "BuildingGradientSearch.h"
#include "Building.h"
#include "SeedCells.h"

namespace gradient_preparation
{
enum class Kind { Materials, Markets, Guard, Clear };
struct Request
{
    Kind kind = Kind::Materials;
    int team = 0, material = 0, swim = 0;
    Uint32 allies = 0;
    bool crowding = false;
    unsigned terrainBuckets = 64;
    SimulationSnapshot::Requirements requirements() const;
};
// One executor thread owns each cache. Stamps identify immutable inputs, not ticks:
// jobs may arrive out of order and a pooled snapshot buffer may have been reused.
struct MaterialSeedCache
{
    using Bits = std::vector<Uint64>;
    size_t budget = 32 * 1024 * 1024;
    Uint64 world = 0, refreshedCells = 0;
    int width = 0, height = 0;
    std::shared_ptr<const TerrainRegistry> terrainRegistry;
    std::shared_ptr<const ResourceRegistry> resourceRegistry;
    std::array<std::vector<Uint16>, 2> base;
    std::vector<Uint16> signatures;
    std::vector<Uint32> resourceTraits;
    std::vector<Uint32> forbiddenMasks;
    std::array<Bits, MaterialCount> goals;
    std::array<Bits, Team::MAX_COUNT> forbidden;
    Bits buildings;
    std::array<std::vector<Uint64>, 4> stamps;
    bool disabled = false;
    bool trySeed(const SimulationSnapshot::Handle&, int team, int material, int swim,
        Uint16* output, const Uint16* suppliers);
};
struct CrowdingScratch
{
    MaterialSeedCache materials;
    std::vector<Uint16> warriors, paint, rows;
    std::vector<int> columnSums;
    std::vector<size_t> positions, seeds;
};
void boxSum(Uint16* grid, int width, int height, CrowdingScratch& scratch);
void seed(const Request& request, const SimulationSnapshot::Handle& snapshot, Uint16* output, CrowdingScratch& scratch);
void propagate(const Request& request, const SimulationSnapshot::Handle& snapshot, Uint16* output, GradientWorkspace& scratch);

// The snapshot components a building field reads: terrain, resources,
// occupancy and areas, without visibility or entities.
SimulationSnapshot::Requirements buildingRequirements();
// Owner side, O(1) per field: the scalars a worker needs instead of the
// Building. route may be Automatic.
BuildingSeed captureBuildingSeed(const Building& building, int swim, BuildingRoute route);
// Worker side, reading only the captured map (buildingRequirements()).
BuildingSeedResult seedBuilding(const BuildingSeed& building, const SimulationSnapshot::Handle& snapshot, Uint16* output);
// Seeds the walking field and, unless it is locked, begins the search on it and
// settles cost layers up to depthTarget (COST_LIMIT or more finishes it).
BuildingSeedResult buildBuilding(const BuildingSeed& building, const SimulationSnapshot::Handle& snapshot,
    const BuildingGradientSearch::Inputs& inputs, Uint16* output, BuildingGradientSearch& search, int depthTarget);
}
