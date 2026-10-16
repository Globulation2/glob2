// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private Map-owned execution state. Keeping this behind a pointer in Map.h
// prevents queue, thread and scratch-storage details from entering Map's API.
#include "GradientPipeline.h"
#include "AirGradientRuntime.h"
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
	// A positive-speed action completes within 256 simulation ticks. Keep a
    // new result for that first read opportunity, then permit deterministic
    // reclamation when the original request has been abandoned.
    static constexpr Uint32 MaterialReadHoldTicks = 256;
	struct MaterialField
	{
		std::unique_ptr<Uint16[]> cells;
        Uint16* published = nullptr;
        // Consumer pool membership changes which otherwise eligible suppliers
        // must be excluded, even when script identity/type/position are stable.
        bool excludesShared = false;
        bool awaitingFirstRead = false;
        Uint32 publishedStep = 0;
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
	// Publication is idempotent during pause, inspection and restored boundaries.
	std::optional<Uint32> worldResultsPublishedTick;
	std::optional<Uint32> worldWorkSubmittedTick;
	GradientPipeline pipeline;
    AirGradientRuntime air;
    struct FieldRequest { Uint16** slot; gradient_preparation::Request request; };
    std::deque<FieldRequest> fieldRequests;
	// Scheduled building fields share the executor with the periodic pipeline.
	// Drain it before teams and the gradient buffer pool are destroyed.
	BuildingGradientJobPipeline buildings;
	// A refresh asked for during team stepping, admitted FIFO after the tick.
	// Superseded requests are pruned without reordering valid requests.
	struct BuildingRequest
	{
		int team = 0;
		Uint16 building = 0;
		Uint32 identity = 0, epoch = 0;
		int slot = 0;
		bool stale = false;
	};
	static constexpr std::size_t BuildingRequestLimit = Team::MAX_COUNT * Building::MAX_COUNT * BUILDING_GRADIENT_COUNT;
	std::deque<BuildingRequest> buildingRequests;
	// Reserved this tick, prepared together at the observation boundary.
	std::vector<BuildingGradientJobPipeline::Job*> stagedBuildings;
	// Explicit synchronous/editor field builds, split by diagnostic reason.
	Uint64 buildingSynchronous = 0;
	std::array<Uint64, std::size_t(Map::BuildingSyncReason::Count)> synchronousByReason{};
	Map::BuildingSyncReason syncReason = Map::BuildingSyncReason::Other;
	void countSynchronous()
	{
		++buildingSynchronous;
		++synchronousByReason[std::size_t(std::exchange(syncReason, Map::BuildingSyncReason::Other))];
	}
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
    std::shared_ptr<gradient_kernel::GradientDeviceService> deviceService;
    std::shared_ptr<gradient_kernel::BackendSession> backendSession;
    void shareBackendSession()
    {
        for (auto& workspace : workspaces) workspace.propagation.backendSession = backendSession;
        pipeline.setBackendSession(backendSession);

    }
    void resetBackendSession()
    {
        if (backendSession) backendSession = std::make_shared<gradient_kernel::BackendSession>();
        shareBackendSession();
    }
    GradientRuntime() { shareBackendSession(); }

};
