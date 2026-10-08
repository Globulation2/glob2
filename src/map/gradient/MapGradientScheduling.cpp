// SPDX-License-Identifier: GPL-3.0-or-later
// Scheduled building gradients: the Map side of BuildingGradientPipeline. Requests, staging, preparation at the
// observation boundary, publication and supersession of building walking
// fields; save/load lives in MapIO.cpp with the other runtime state.
//
// Tick placement (Game::syncStep):
//   advanceGradientPipeline  publishes jobs due this tick, before teams step;
//   team stepping            requests refreshes and keeps serving old fields;
//   after stepCounter++      stageBuildingGradientPreparation admits requests;
//   observation boundary     prepareStagedBuildingGradients captures and submits.
// Every owner-side step is O(1) per job; seeding and the search run on workers.

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "BuildingGradientSearch.h"
#include "BuildingGradientStats.h"
#include "BuildingType.h"
#include "Game.h"
#include "MapInternal.h"
#include "SnapshotGradient.h"
#include "Team.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <optional>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
using PipelineJob = BuildingGradientJobPipeline::Job;
using gradient_kernel::COST_LIMIT;

int slotSwim(int slot) { return slot % SWIM_CLASS_COUNT; }
BuildingRoute slotRoute(int slot) { return BuildingRoute(slot / SWIM_CLASS_COUNT); }


struct DepthSetting
{
	GradientRuntime::BuildingDepth mode = GradientRuntime::BuildingDepth::Table;
	int point = BuildingGradientDepth::DEFAULT_POINT;
};

std::optional<DepthSetting> parseBuildingDepth(std::string_view mode)
{
	if (mode == "full") return DepthSetting{GradientRuntime::BuildingDepth::Full};
	if (mode == "lazy") return DepthSetting{GradientRuntime::BuildingDepth::Lazy};
	if (mode == "table") return DepthSetting{};
	if (const int point = BuildingGradientDepth::pointIndex(mode); point >= 0)
		return DepthSetting{GradientRuntime::BuildingDepth::Table, point};
	return std::nullopt;
}

// GLOB2_BUILDING_DEPTH, read once per process. A timing switch only, so an
// invalid value is reported and ignored rather than failing a game.
std::optional<DepthSetting> environmentBuildingDepth()
{
	static const auto depth = []() -> std::optional<DepthSetting> {
		const char* value = std::getenv("GLOB2_BUILDING_DEPTH");
		if (!value) return std::nullopt;
		const auto parsed = parseBuildingDepth(value);
		if (!parsed)
			std::cerr << "Ignoring GLOB2_BUILDING_DEPTH=" << value
					  << ": expected full, table, lazy or an operating point name\n";
		return parsed;
	}();
	return depth;
}

void buildScheduled(PipelineJob& job)
{
	auto& p = job.payload;
	if (building_gradient::workHook) building_gradient::workHook(p);
	const auto result = gradient_preparation::buildBuilding(p.seed, *job.snapshotLease, p.inputs, p.data.get(), *p.search, p.depthTarget);
	p.locked = result.locked;
	p.resourceState = result.resourceState;
}
} // namespace

bool Map::buildingGradientPipelineActive() const
{
	return gradientRuntime->buildings.enabled();
}

Map::BuildingGradientPipelineStatus Map::buildingGradientPipelineStatus() const
{
	const auto& rt = *gradientRuntime;
	const auto& metrics = rt.buildings.metrics;
	return {buildingGradientPipelineActive(), rt.buildings.delayTicks(), rt.buildings.pendingCount(),
		rt.buildingRequests.size(), metrics.jobs, metrics.published, metrics.discarded,
		rt.buildingSynchronous, metrics.maxPending, metrics.waitNs, rt.synchronousByReason};
}

const char* Map::buildingSyncReasonName(BuildingSyncReason reason)
{
	static constexpr const char* names[] = {"inactive", "cold_new", "cold_idle", "cold_own", "cold_team", "cold_area",
		"overflow", "other"};
	static_assert(std::size(names) == std::size_t(BuildingSyncReason::Count));
	return names[std::size_t(reason)];
}

void Map::noteBuildingSyncReason(BuildingSyncReason reason)
{
	gradientRuntime->syncReason = buildingGradientPipelineActive() ? reason : BuildingSyncReason::Inactive;
}

void Map::carryPendingBuildingGenerations(Uint32 from, Uint32 to)
{
	// Superseded jobs (the editing team's reset buildings) are discarded anyway.
	gradientRuntime->buildings.visitOwnerFields([&](building_gradient::Job& p) {
		if (p.generation == from) p.generation = to;
	});
}

Building* Map::buildingGradientDestination(int team, int id, Uint32 identity) const
{
	if (!game || team < 0 || team >= game->teamsCount() || id < 0 || id >= Building::MAX_COUNT || !game->teams[team])
		return nullptr;
	Building* building = game->teams[team]->myBuildings[id];
	return building && building->scriptIdentity == identity ? building : nullptr;
}

// Follows the match's buildingGradientDelay; a map without a game (the editor,
// map-only tests) builds its fields synchronously. A change with pending work
// would move deadlines, so it is refused; with an empty pipeline (a new match,
// or a fork of a loaded game) it reconfigures.
void Map::ensureBuildingGradientPipeline()
{
	auto& rt = *gradientRuntime;
	auto& pipeline = rt.buildings;
	const unsigned delay = game ? unsigned(game->gameHeader.getBuildingGradientDelay()) : 0;
	if (pipeline.delayTicks() == delay) return;
	if (pipeline.pendingCount() || !rt.buildingRequests.empty() || !rt.stagedBuildings.empty())
		throw std::logic_error("Scheduled building gradients cannot change with pending work");
	// Captures of one tick publish together, in request order; the synchronous
	// path built them in that order, so a later one overwrites an earlier one.
	struct Published { int team; Uint16 building; int slot; Uint32 captureTick, publishTick; };
	pipeline.publish = [this, sameTick = std::vector<Published>{}](PipelineJob& job) mutable {
		auto& p = job.payload;
		Building* b = buildingGradientDestination(p.team, p.buildingId, p.scriptIdentity);
		if (!b || b->refreshEpoch[p.slot] != p.epoch || !b->globalGradient[p.slot]) return false;
		const int slot = p.slot, swim = p.swim;
		if (gradientStats)
		{
			// The replaced lifetime ends here, as a synchronous rebuild's would.
			gradientStats->setPendingReason(BuildingGradientStats::Reason::Scheduled);
			gradientStats->setPendingContext(p.statsContext);
			gradientStats->fieldRebuilding(*this, *b, slot, b->routeAccess(swim, p.route), game->stepCounter, topologyGeneration);
		}
		// Pointer swap: the transferred search already points at data, so a
		// partial field resumes from its own buckets with no rescan.
		auto& search = b->globalGradientSearch[slot];
		// The depth model reads the replaced lifetime; a locked one leaves the older value.
		if (search) b->settledCostHint[slot] = Uint16(std::min(search->requiredCost(), 0xFFFE));
		recycleBuildingGradientSearch(std::move(search));
		recycleBuildingGradientBuffer(b->globalGradient[slot]);
		b->globalGradient[slot] = p.data.release();
		if (!p.locked) search = std::move(p.search);
		b->lastGlobalGradientUpdateStepCounter[slot] = p.captureTick;
		b->gradientGeneration[slot] = p.generation;
		// Access metadata is shared by the swim classes of one variant: the
		// newest capture wins. A synchronous build stamped with the capture's
		// tick ran during that tick's team stepping, after the capture. A
		// scheduled one stamped with it was requested earlier in this tick, so
		// the later one wins as the later synchronous build would. (Values of
		// one boundary are equal; a boundary repeated within one tick, as in
		// direct map stepping, can differ.) At most MaxJobsPerTick entries.
		if (!sameTick.empty() && (sameTick.front().captureTick != p.captureTick || sameTick.front().publishTick != game->stepCounter))
			sameTick.clear();
		const auto publishedEarlier = [&](int other) {
			for (const auto& e : sameTick)
				if (e.team == p.team && e.building == p.buildingId && e.slot == other) return true;
			return false;
		};
		bool newest = true;
		for (int s = 0; s < SWIM_CLASS_COUNT; ++s)
		{
			if (s == swim || (s > 0) != (swim > 0)) continue;
			const int other = b->routeSlot(s, p.route);
			const auto stamped = b->lastGlobalGradientUpdateStepCounter[other];
			if (stamped > p.captureTick || (stamped == p.captureTick && !publishedEarlier(other))) newest = false;
		}
		sameTick.push_back({p.team, p.buildingId, slot, p.captureTick, game->stepCounter});
		if (newest)
		{
			b->locked[b->routeAccess(swim, p.route)] = p.locked;
			if (p.route == BuildingRoute::Clearing) b->anyResourceToClear[swim > 0] = p.resourceState;
		}
		b->refreshRequested.reset(slot);
		return true;
	};
	// Teardown-safe: recycles pooled outputs only, never touches a building.
	pipeline.retire = [this](PipelineJob& job) {
		auto& p = job.payload;
		recycleBuildingGradientBuffer(p.data.release());
		recycleBuildingGradientSearch(std::move(p.search));
	};
	// Staged after tick c-1's step counter advanced to c, as periodic jobs are.
	pipeline.deadline = [this](unsigned remaining) {
		return ComputeExecutor::advanceDue(std::uint64_t(game ? game->stepCounter : 0) + remaining - 1);
	};
	// Statistics record what readers needed, so they default to seeds only.
	if (const auto depth = environmentBuildingDepth())
	{
		rt.buildingDepth = depth->mode;
		rt.buildingDepthPoint = depth->point;
	}
	else if (gradientStats) rt.buildingDepth = GradientRuntime::BuildingDepth::Lazy;
	if (delay) pipeline.configure(compute, rt.buildingShared, delay, buildScheduled);
	else pipeline.drain();
}

void Map::resetBuildingGradientPipeline() noexcept
{
	auto& rt = *gradientRuntime;
	rt.stagedBuildings.clear(); // Reserved jobs are pending too; drain retires them.
	rt.buildingRequests.clear();
	rt.buildings.drain();
}

bool Map::requestBuildingRefresh(Building* building, int slot)
{
	auto& rt = *gradientRuntime;
	if (!buildingGradientPipelineActive() || !building->globalGradient[slot]) return false;
	if (building->refreshRequested.test(slot)) return true; // Already queued or in flight.
	building->refreshRequested.set(slot);
	rt.buildingRequests.push_back({building->owner->teamNumber, Uint16(Building::GIDtoID(building->gid)),
		building->scriptIdentity, building->refreshEpoch[slot], slot, false});
	if (rt.buildingRequests.size() > GradientRuntime::BuildingRequestLimit)
	{
		// Deterministic overflow: the oldest request is served synchronously now.
		const auto oldest = rt.buildingRequests.front();
		rt.buildingRequests.pop_front();
		Building* b = buildingGradientDestination(oldest.team, oldest.building, oldest.identity);
		if (b && !oldest.stale && b->refreshEpoch[oldest.slot] == oldest.epoch && b->globalGradient[oldest.slot])
		{
			noteBuildingSyncReason(BuildingSyncReason::Overflow);
			updateGlobalGradient(b, slotSwim(oldest.slot), slotRoute(oldest.slot)); // Supersedes and clears the request.
		}
	}
	return true;
}

void Map::setBuildingGradientDepth(std::string_view mode)
{
	const auto depth = parseBuildingDepth(mode);
	if (!depth) throw std::invalid_argument("building gradient depth must be full, table, lazy or a point name");
	gradientRuntime->buildingDepth = depth->mode;
	gradientRuntime->buildingDepthPoint = depth->point;
}

int Map::predictBuildingDepth(const Building* building, int slot) const
{
	// Clearing and Combat goals move with resources and enemies: full depth.
	if (slotRoute(slot) != BuildingRoute::Footprint) return COST_LIMIT;
	switch (gradientRuntime->buildingDepth)
	{
	case GradientRuntime::BuildingDepth::Full: return COST_LIMIT;
	case GradientRuntime::BuildingDepth::Lazy: return 0;
	case GradientRuntime::BuildingDepth::Table: break;
	}
	// The field's own past depths, as BuildingGradientStats records them when a
	// job is staged: how deep readers of the serving field have needed it so far, and
	// the final reader demand of the lifetime it replaced.
	int serving = -1;
	if (const auto& search = building->globalGradientSearch[slot]) serving = search->requiredCost();
	const Uint16 hint = building->settledCostHint[slot];
	const int previous = hint == Building::UNKNOWN_SETTLED_COST ? -1 : hint;
	return std::min(BuildingGradientDepth::target(serving, previous, gradientRuntime->buildingDepthPoint), COST_LIMIT);
}

void Map::stageBuildingGradientPreparation()
{
	if (!buildingGradientPipelineActive()) return;
	auto& rt = *gradientRuntime;
	auto& pipeline = rt.buildings;
	while (!rt.buildingRequests.empty() && pipeline.canReserve())
	{
		const auto request = rt.buildingRequests.front();
		rt.buildingRequests.pop_front();
		Building* b = buildingGradientDestination(request.team, request.building, request.identity);
		if (!b || request.stale || b->refreshEpoch[request.slot] != request.epoch || !b->globalGradient[request.slot]) continue;
		auto* job = pipeline.reserve();
		job->payload = {};
		auto& p = job->payload;
		p.team = request.team; p.buildingId = request.building; p.scriptIdentity = request.identity;
		p.slot = request.slot; p.swim = slotSwim(request.slot); p.route = slotRoute(request.slot);
		p.epoch = request.epoch; p.captureTick = game->stepCounter; p.generation = topologyGeneration;
		p.data.reset(acquireBuildingGradientBuffer());
		p.search = acquireBuildingGradientSearch();
		p.depthTarget = predictBuildingDepth(b, p.slot);
		if (gradientStats)
		{
			p.statsContext = BuildingGradientStats::context(*b, p.slot);
			p.statsContext.staged = true;
		}
		// The capture consumes earlier dirty marks; later ones survive publication.
		b->dirtyGradient[p.slot] = false;
		rt.stagedBuildings.push_back(job);
	}
}

SimulationSnapshot::Requirements Map::pendingBuildingRequirements() const
{
	return gradientRuntime->stagedBuildings.empty() ? 0 : gradient_preparation::buildingRequirements();
}

void Map::prepareStagedBuildingGradients(const SimulationSnapshot::Handle& foundation)
{
	auto& rt = *gradientRuntime;
	if (rt.stagedBuildings.empty()) return;
	const auto staged = std::exchange(rt.stagedBuildings, {});
	for (auto* job : staged)
	{
		auto& p = job->payload;
		// Nothing mutates the world between staging and this boundary.
		Building* b = buildingGradientDestination(p.team, p.buildingId, p.scriptIdentity);
		if (!b) throw std::logic_error("Staged building gradient lost its destination");
		p.seed = gradient_preparation::captureBuildingSeed(*b, p.swim, p.route);
		// Owner cost planes from the same boundary as the lease the seeder reads.
		p.inputs = BuildingGradientSearch::Inputs::of(*this, p.swim);
		job->snapshotLease = foundation.project(gradient_preparation::buildingRequirements());
	}
	rt.buildings.prepare(staged);
}
