// SPDX-License-Identifier: GPL-3.0-or-later
// Scheduled building gradients: the Map side of BuildingGradientPipeline. Requests, staging, preparation at the
// observation boundary, publication and supersession of building walking
// fields; save/load lives in MapIO.cpp with the other runtime state.
//
// Boundary placement: publish due fields, capture one immutable observation,
// then stage and submit requests using that observation. Team stepping requests
// refreshes while serving existing fields until a later boundary publishes them.
// Every owner-side step is O(1) per job; seeding and the search run on workers.

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "BuildingGradientSearch.h"
#include "SharedBuildingGradient.h"
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

int slotSwim(int slot) { return slot % MOVEMENT_CLASS_COUNT; }
BuildingRoute slotRoute(int slot) { return BuildingRoute(slot / MOVEMENT_CLASS_COUNT); }


void buildScheduled(PipelineJob& job)
{
	auto& p = job.payload;
	if (building_gradient::workHook) building_gradient::workHook(p);
	p.inputs =
		BuildingGradientSearch::Inputs::ofSnapshot(*job.snapshotLease, p.swim, p.seed.teamMask);
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
		rt.buildingSynchronous, metrics.maxPending, rt.synchronousByReason};
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
		if (!b || b->refreshEpoch[p.slot] != p.epoch) return false;
		const int slot = p.slot, swim = p.swim;
        auto guard=SharedBuildingGradient::lock(b->sharedGradient[slot]);

		// Pointer swap: the transferred search already points at data, so a
		// partial field resumes from its own buckets with no rescan.
		auto& search = b->globalGradientSearch[slot];
		// The depth model reads the replaced lifetime; a locked one leaves the older value.
		if (search) b->settledCostHint[slot] = Uint16(std::min(search->requiredCost(), 0xFFFE));
		b->retireSharedGradient(slot);
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
		// direct map stepping, can differ.) At most four entries per team.
		if (!sameTick.empty() && (sameTick.front().captureTick != p.captureTick || sameTick.front().publishTick != game->stepCounter))
			sameTick.clear();
		const auto publishedEarlier = [&](int other) {
			for (const auto& e : sameTick)
				if (e.team == p.team && e.building == p.buildingId && e.slot == other) return true;
			return false;
		};
		bool newest = true;
		for (int s = 0; s < MOVEMENT_CLASS_COUNT; ++s)
		{
			if (s == swim || swimAccessVariant(s) != swimAccessVariant(swim)) continue;
			const int other = b->routeSlot(s, p.route);
            if (!b->globalGradient[other]) continue;
			const auto stamped = b->lastGlobalGradientUpdateStepCounter[other];
			if (stamped > p.captureTick || (stamped == p.captureTick && !publishedEarlier(other))) newest = false;
		}
		sameTick.push_back({p.team, p.buildingId, slot, p.captureTick, game->stepCounter});
		if (newest)
		{
			b->setLocked(b->routeAccess(swim, p.route),p.locked);
			if (p.route == BuildingRoute::Clearing) b->anyResourceToClear[swimAccessVariant(swim)] = p.resourceState;
		}
        if(auto& stats=gradientRuntime->workStats) {
            const auto key=GradientWorkStats::buildingKey(p.team,p.swim,p.buildingId,p.scriptIdentity,unsigned(p.route));
            stats->published(key,game->stepCounter,p.workReason,size);
            if(b->globalGradientSearch[slot])stats->partial(key,game->stepCounter,0,b->globalGradientSearch[slot]->settledCost(),true);
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
	// Work observed at c publishes at boundary c + remaining.
	pipeline.deadline = [this](unsigned remaining) {
		return ComputeExecutor::advanceDue(std::uint64_t(game ? game->stepCounter : 0) + remaining);
	};
	// Statistics record what readers needed, so they default to seeds only.
	if (delay) pipeline.configure(compute, delay, buildScheduled);
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
	if (!buildingGradientPipelineActive()) return false;
	if (!building->globalGradient[slot] && game->preparingBuildingGradient(*building, slot)) return true;
    if(auto& stats=rt.workStats) {
        const auto key=GradientWorkStats::buildingKey(building->owner->teamNumber,slotSwim(slot),Building::GIDtoID(building->gid),building->scriptIdentity,unsigned(slotRoute(slot)));
        stats->request(key,game->stepCounter,unsigned(building->globalGradient[slot]?AdaptiveGradientState::Reason::Reactive:AdaptiveGradientState::Reason::Cold),building->refreshRequested.test(slot));
    }
	if (building->refreshRequested.test(slot)) return true; // Already queued or in flight.
	building->refreshRequested.set(slot);
	rt.buildingRequests.push_back({building->owner->teamNumber, Uint16(Building::GIDtoID(building->gid)),
		building->scriptIdentity, building->refreshEpoch[slot], slot, false});
    std::erase_if(rt.buildingRequests,[this](const auto& request) {
        const auto* b=buildingGradientDestination(request.team,request.building,request.identity);
        return request.stale || !b || b->refreshEpoch[request.slot]!=request.epoch;
    });

	return true;
}

int Map::predictBuildingDepth(const Building* building, int slot) const
{
	// Clearing and Combat goals move with resources and enemies: full depth.
	if (slotRoute(slot) != BuildingRoute::Footprint) return COST_LIMIT;
	// The field's own past depths when a
	// job is staged: how deep readers of the serving field have needed it so far, and
	// the final reader demand of the lifetime it replaced.
	int serving = -1;
	if (const auto& search = building->globalGradientSearch[slot]) serving = search->requiredCost();
	const Uint16 hint = building->settledCostHint[slot];
	const int previous = hint == Building::UNKNOWN_SETTLED_COST ? -1 : hint;
	return std::min(BuildingGradientDepth::target(serving, previous), COST_LIMIT);
}

void Map::stageBuildingGradientPreparation()
{
	if (!buildingGradientPipelineActive()) return;
	auto& rt = *gradientRuntime;
	auto& pipeline = rt.buildings;
	// Scan this boundary's queue once. A full team quota must not block another
	// team behind it; deferred requests retain their order within each team.
	const auto requests = rt.buildingRequests.size();
	std::array<bool, TEAM_MAX_COUNT> blocked{};
	for (std::size_t i = 0; i < requests; ++i)
	{
		const auto request = rt.buildingRequests.front();
		rt.buildingRequests.pop_front();
		Building* b = buildingGradientDestination(request.team, request.building, request.identity);
		if (!b || request.stale || b->refreshEpoch[request.slot] != request.epoch) continue;
		const bool initial = !b->globalGradient[request.slot];
		const unsigned ticks = initial ? 1u : pipeline.delayTicks();
		if (blocked[request.team] || !pipeline.canReserve(request.team, ticks)) {
			blocked[request.team] = true;
			rt.buildingRequests.push_back(request);
			continue;
		}
		auto* job = pipeline.reserve(request.team, ticks);
		job->payload = {};
		auto& p = job->payload;
		p.team = request.team; p.buildingId = request.building; p.scriptIdentity = request.identity;
		p.slot = request.slot; p.swim = slotSwim(request.slot); p.route = slotRoute(request.slot);
		p.epoch = request.epoch; p.captureTick = game->stepCounter; p.generation = topologyGeneration;
        if(auto& stats=rt.workStats)p.workReason=stats->admitted(GradientWorkStats::buildingKey(p.team,p.swim,p.buildingId,p.scriptIdentity,unsigned(p.route)),game->stepCounter,size);
		p.data.reset(acquireBuildingGradientBuffer());
		p.search = acquireBuildingGradientSearch();
		// Cold initialization stays lazy for every route, including flags.
        p.depthTarget = b->globalGradient[p.slot] ? predictBuildingDepth(b,p.slot) : BuildingGradientDepth::MIN_DEPTH;

		// The capture consumes earlier dirty marks; later ones survive publication.
		b->dirtyGradient[p.slot] = false;
		rt.stagedBuildings.push_back(job);
	}
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
		// Defer movement-plane construction to the worker's captured lease.
		p.inputs = {};
		job->snapshotLease = foundation.buildingGradientView();
	}
	rt.buildings.prepare(staged);
}
