// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingGradientPipeline.h"
#include "BuildingGradientSearch.h"
#include "BuildingGradientStats.h"
#include "SeedCells.h"
#include <cstdint>
#include <memory>

namespace building_gradient
{
// Payload of one scheduled building walking field. The owner fills it in O(1):
// scalars and shared immutable inputs only, never a loop over map cells.
// Destinations are identities, not pointers, so publication can validate them
// against the live building.
struct Job
{
	// Destination.
	int team = 0;
	std::uint16_t buildingId = 0;
	std::uint32_t scriptIdentity = 0;
	int slot = 0, swim = 0;
	BuildingRoute route = BuildingRoute::Footprint; // resolved, never Automatic
	// Schedule. generation is the topology generation at reservation, carried
	// forward like live fields (Map::carryPendingBuildingGenerations); epoch is
	// the destination slot's supersession counter at reservation. Owner-only:
	// workers never read either.
	std::uint32_t epoch = 0, captureTick = 0, generation = 0;
	int depthTarget = 0;
	// Diagnostics only (BuildingGradientStats): the depth inputs at staging.
	BuildingGradientStats::Context statsContext;
	// Owner scalars and immutable cost planes, taken at the observation boundary
	// together with the snapshot lease the seeder reads.
	gradient_preparation::BuildingSeed seed;
	BuildingGradientSearch::Inputs inputs;
	// Outputs. data comes from Map::acquireBuildingGradientBuffer and search from
	// acquireBuildingGradientSearch; publication hands both to the building, and
	// retirement recycles whatever was not published.
	std::unique_ptr<std::uint16_t[]> data;
	std::unique_ptr<BuildingGradientSearch> search;
	bool locked = false;
	int resourceState = 0;
	// The pipeline drops the snapshot lease itself.
	void releaseInputs() noexcept { inputs = {}; }
};
// Test seam: runs on the worker before each scheduled build, so harnesses can
// stand in a slow or failing worker. Null in the game.
inline void (*workHook)(const Job&) = nullptr;
} // namespace building_gradient

using BuildingGradientJobPipeline = BuildingGradientPipeline<building_gradient::Job>;
