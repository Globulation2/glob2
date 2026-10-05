// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingGradientCapture.h"
#include "GradientRuntime.h"
#include "BuildingGradientSearch.h"
#include "Unit.h"
#include "Utilities.h"
using building_gradient::refreshDescription;
using building_gradient::refreshDestination;

bool Map::buildingPipelineEnabled() const
{
	return game && game->gameHeader.hasExperiment(ExperimentId::BuildingGradientPipeline);
}

bool Map::requestBuildingRefresh(Building *building, int swim)
{
	if (!buildingPipelineEnabled() || !building->globalGradient[swim])
		return false;
	auto &scheduler = gradientRuntime->buildings;
	const BuildingGradientScheduler::Key key{building->gid, swim};
	++scheduler.metrics.requests;
	const auto pending = scheduler.pending.find(key);
	if (pending != scheduler.pending.end() &&
		pending->second->destination.identity == building->scriptIdentity &&
		pending->second->destination.epoch == scheduler.epochs[key])
		++scheduler.metrics.coalesced;
	else if (scheduler.requests.count(key))
		++scheduler.metrics.coalesced;
	else
		scheduler.requests[key] = building->scriptIdentity;
	return true;
}

int Map::buildingAccessIndex(int swim) const
{
	if (!buildingPipelineEnabled())
		return int(swim > 0);
	if (!gradientRuntime->buildingAccessByClass)
	{
		// A fork from legacy/off execution starts with its existing published metadata.
		for (int team = 0; team < game->teamsCount(); ++team)
			for (int id = 0; id < Building::MAX_COUNT; ++id)
				if (auto *building = game->teams[team]->myBuildings[id])
					for (int sw = SWIM_VARIANT_COUNT; sw < SWIM_CLASS_COUNT; ++sw)
					{
						building->locked[sw] = building->locked[SWIM_VARIANT_CAN_SWIM];
						building->anyResourceToClear[sw] =
							building->anyResourceToClear[SWIM_VARIANT_CAN_SWIM];
					}
		gradientRuntime->buildingAccessByClass = true;
	}
	return swim;
}

void Map::invalidateBuildingRefresh(Building *building, int swim)
{
	if (gradientRuntime)
		gradientRuntime->buildings.invalidate(building->gid, swim);
}

void Map::submitBuildingRefreshes()
{
	if (!buildingPipelineEnabled())
		return;
	auto &scheduler = gradientRuntime->buildings;
	std::shared_ptr<building_gradient::Terrain> terrain;
	std::shared_ptr<BuildingGradientSpool> spool;
	for (auto it = scheduler.requests.begin(); it != scheduler.requests.end();)
	{
		const auto key = it->first;
		auto *b = refreshDestination(game, key.first);
		if (!b || b->scriptIdentity != it->second || !b->globalGradient[key.second])
		{
			it = scheduler.requests.erase(it);
			continue;
		}
		if (scheduler.pending.count(key))
		{
			++it;
			continue;
		}
		unsigned children = 0;
		for (int r = 0; r < MAX_NB_RESOURCES; ++r)
			if (b->roundTripGradient[r][key.second])
				++children;
		const auto fieldBytes = size * sizeof(Uint16) * (1 + 2 * children),
				   snapshotBytes = size * sizeof(building_gradient::Cell);
		bool snapshotReserved = false;
		for (const auto &[destination, pending] : scheduler.pending)
			if (pending->captured == game->stepCounter && pending->snapshotBytes)
				snapshotReserved = true;
		const auto needed = fieldBytes + (snapshotReserved ? 0 : snapshotBytes);
		const bool synchronous = scheduler.bytes() + needed > scheduler.BYTE_LIMIT;
		const auto started = BuildingGradientDiagnostics::now(),
				   cpu = BuildingGradientDiagnostics::threadCpuNow();
		if (!terrain)
		{
			terrain = std::make_shared<building_gradient::Terrain>();
			terrain->width = getW();
			terrain->height = getH();
			terrain->generation = topologyGeneration;
			terrain->modifiedCosts = hasTerrainMovementModifiers();
			terrain->cells.resize(size);
			for (std::size_t i = 0; i < size; ++i)
				terrain->cells[i] = {tiles[i].forbidden,
									 tiles[i].building,
									 tiles[i].resource.type,
									 immobileUnits[i],
									 Uint8(tiles[i].building == NOGBID
											   ? 0
											   : Building::GIDtoTeam(tiles[i].building)), terrainTypeAt(i)};
		}
		auto job = std::make_shared<BuildingGradientScheduler::Job>();
		job->destination = refreshDescription(*b, key.second, scheduler.epochs[key]);
		job->terrain = terrain;
		job->reservedBytes = fieldBytes;
		job->snapshotBytes = snapshotBytes;
		job->captured = game->stepCounter;
		job->due = job->captured + game->gameHeader.getBuildingGradientDelay();
		job->generation = topologyGeneration;
		for (int r = 0; r < MAX_NB_RESOURCES; ++r)
			if (b->roundTripGradient[r][key.second])
			{
				const auto *parent = getResourceGradient(b->owner->teamNumber, r, key.second);
				job->parents[r].assign(parent, parent + size);
				Uint32 fingerprint = 2166136261u;
				for (auto value : job->parents[r])
					fingerprint = (fingerprint ^ value) * 16777619u;
				job->parentVersions[r] = fingerprint;
			}
		b->dirtyGradient[key.second] =
			false; // Later dirty notifications belong to the next snapshot.
		scheduler.metrics.snapshotNs += BuildingGradientDiagnostics::now() - started;
		const auto end = BuildingGradientDiagnostics::threadCpuNow();
		scheduler.metrics.snapshotCpuNs += cpu && end >= cpu ? end - cpu : 0;
		++scheduler.metrics.walkingFields;
		scheduler.metrics.tripFields += children;
		it = scheduler.requests.erase(it);
		++scheduler.metrics.jobs;
		auto build = [job](GradientWorkspace &scratch)
		{
			const auto started = BuildingGradientDiagnostics::now(),
					   cpu = BuildingGradientDiagnostics::threadCpuNow();
			job->result =
				building_gradient::build(*job->terrain, job->destination, job->parents, scratch);
			job->buildNs = BuildingGradientDiagnostics::now() - started;
			const auto end = BuildingGradientDiagnostics::threadCpuNow();
			job->buildCpuNs = cpu && end >= cpu ? end - cpu : 0;
		};
		if (synchronous)
		{
			const auto fallbackStarted = BuildingGradientDiagnostics::now(),
					   fallbackCpu = BuildingGradientDiagnostics::threadCpuNow();
			GradientWorkspace scratch;
			build(scratch);
			if (!spool)
				spool = std::make_shared<BuildingGradientSpool>();
			job->spillResult(spool);
			job->terrain.reset();
			for (auto &parent : job->parents)
				std::vector<Uint16>().swap(parent);
			job->reservedBytes = job->snapshotBytes = 0;
			++scheduler.metrics.synchronousFallback;
			scheduler.metrics.fallbackNs += BuildingGradientDiagnostics::now() - fallbackStarted;
			const auto fallbackEnd = BuildingGradientDiagnostics::threadCpuNow();
			scheduler.metrics.fallbackCpuNs +=
				fallbackCpu && fallbackEnd >= fallbackCpu ? fallbackEnd - fallbackCpu : 0;
		}
		else
			job->task = scheduler.executor->submit(std::move(build));
		scheduler.pending[key] = job;
		scheduler.metrics.maxBytes =
			std::max<std::uint64_t>(scheduler.metrics.maxBytes, scheduler.bytes());
		scheduler.metrics.maxPending =
			std::max<std::uint64_t>(scheduler.metrics.maxPending, scheduler.pending.size());
	}
}

void Map::publishBuildingRefreshes()
{
	auto &scheduler = gradientRuntime->buildings;
	for (auto it = scheduler.pending.begin(); it != scheduler.pending.end();)
	{
		auto &job = *it->second;
		if (static_cast<Sint32>(game->stepCounter - job.due) < 0)
		{
			++it;
			continue;
		}
		const auto started = BuildingGradientDiagnostics::now();
		scheduler.executor->wait(job.task);
		scheduler.metrics.waitNs += BuildingGradientDiagnostics::now() - started;
		scheduler.account(job);
		auto *b = refreshDestination(game, it->first.first);
		const int sw = it->first.second;
		if (!b || b->scriptIdentity != job.destination.identity ||
			scheduler.epochs[it->first] != job.destination.epoch || !b->globalGradient[sw])
			++scheduler.metrics.discarded;
		else
		{
			job.visitResult(
				[&](const auto &result)
				{
					recycleBuildingGradientSearch(std::move(b->globalGradientSearch[sw]));
					std::copy(result.walking.begin(), result.walking.end(), b->globalGradient[sw]);
					b->lastGlobalGradientUpdateStepCounter[sw] = job.captured;
					b->gradientGeneration[sw] = job.generation;
					b->locked[buildingAccessIndex(sw)] = result.locked;
					if (job.destination.clearing)
						b->anyResourceToClear[buildingAccessIndex(sw)] = result.resourceState;
					for (int r = 0; r < MAX_NB_RESOURCES; ++r)
						if (!result.trips[r].empty() && b->roundTripGradient[r][sw])
						{
							std::copy(result.trips[r].begin(), result.trips[r].end(),
									  b->roundTripGradient[r][sw]);
							b->roundTripGradientStep[r][sw] = job.captured;
						}
				});
			++scheduler.metrics.published;
		}
		it = scheduler.pending.erase(it);
	}
}
