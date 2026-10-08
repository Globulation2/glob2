// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private Map-owned execution state. Keeping this behind a pointer in Map.h
// prevents queue, thread and scratch-storage details from entering Map's API.
#include "GradientPipeline.h"
#include "BuildingGradientJob.h"
#include "ResourceGrowth.h"
#include "BuildingGradientDepthPolicy.h"
#include "SnapshotGradient.h"
#include "ResourceSeedCache.h"
#include "TerrainSafetyCache.h"
#include "field/GradientWorkspace.h"

#include <vector>
#include <array>
#include <map>
#include <list>
#include "Team.h"
#include "Ressource.h"
#include <unordered_map>
#include <deque>
#include <utility>
#include "Map.h"

struct GradientRuntime
{
	struct Workspace
	{
		GradientWorkspace propagation;
		gradient_preparation::CrowdingScratch crowding;
	};
    TerrainSafetyCache safety;
	struct MaterialField
	{
		std::unique_ptr<Uint16[]> cells;
		Uint64 sourceRevision=0, recency=0;
		Uint32 topology=0, builtStep=0, identity=0;
		Sint32 consumer=-1, type=-1, x=0, y=0;
		int team=0, resource=0, swim=0;
		unsigned modes=0;
		std::list<Uint64>::iterator lru;
	};
	std::map<Uint64,MaterialField> materialFields;
	std::list<Uint64> materialLru;
	std::array<std::array<Uint64,MaterialCount>,Team::MAX_COUNT> stockRevision{};
	Uint64 materialCacheClock=0, materialCacheBudget=64ull*1024*1024;
	std::vector<Workspace> workspaces{1};
	GradientPipeline pipeline;
	// Scheduled building fields share the executor with the periodic pipeline.
	// Drain it before teams and the gradient buffer pool are destroyed.
	BuildingGradientJobPipeline buildings;
	// Execution placement for building jobs, following the periodic pipeline's
	// worker setting. Local configuration, never saved.
	bool buildingShared = true;
	// A refresh asked for during team stepping, admitted FIFO after the tick.
	// stale requests were superseded while queued; they stay in place so queue
	// length (and with it the overflow fallback) survives save and load.
	struct BuildingRequest
	{
		int team = 0;
		Uint16 building = 0;
		Uint32 identity = 0, epoch = 0;
		int slot = 0;
		bool stale = false;
	};
	static constexpr std::size_t BuildingRequestLimit = 64;
	std::deque<BuildingRequest> buildingRequests;
	// Reserved this tick, prepared together at the observation boundary.
	std::vector<BuildingGradientJobPipeline::Job*> stagedBuildings;
	// Owner-side building walking field builds (cold fields, own-building
	// changes and queue overflow; every build on a map without a game), split
	// by reason.
	Uint64 buildingSynchronous = 0;
	std::array<Uint64, std::size_t(Map::BuildingSyncReason::Count)> synchronousByReason{};
	Map::BuildingSyncReason syncReason = Map::BuildingSyncReason::Other;
	void countSynchronous()
	{
		++buildingSynchronous;
		++synchronousByReason[std::size_t(std::exchange(syncReason, Map::BuildingSyncReason::Other))];
	}
	// Worker depth for scheduled walking fields (Map::predictBuildingDepth):
	// the generated model at one of its operating points, everything, or
	// nothing beyond the seeds. Results never depend on it;
	// GLOB2_BUILDING_DEPTH=full|table|lazy|<point name> selects it for timing
	// comparisons. Local configuration, never saved.
	enum class BuildingDepth { Table, Full, Lazy };
	BuildingDepth buildingDepth = BuildingDepth::Table;
	int buildingDepthPoint = BuildingGradientDepth::DEFAULT_POINT;
    ResourceGrowth::Pipeline growth;
	// One simulation-owned reservation, consumed once during the observation phase.
	// Scalar identities survive the scheduling barrier without borrowing stack lambdas.
	struct Preparation {
		using Kind = gradient_preparation::Kind;
		GradientPipeline::Job *job = nullptr;
		Kind kind = Kind::Materials;
		int team = 0, material = 0, swim = 0;
	} preparation;
	bool supplierLocationsDirty = true;
	std::unordered_map<std::size_t, std::vector<std::uint16_t>> overlaySupplierLocations;
	ResourceSeedCache resourceSeeds;
	// Shared inert field for materials with no natural or supplier source.
	std::vector<Uint16> absentMaterialField;
};
