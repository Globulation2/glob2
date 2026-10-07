// SPDX-License-Identifier: GPL-3.0-or-later
#include "SnapshotStore.h"
#include "Game.h"
#include <algorithm>
namespace SimulationSnapshot
{
Handle Store::captureBoundary(const Game& game, Requirements required)
{
	const auto requested = required;
	if (latest && latest->worldIdentity != game.map.identity()) reset();
	if (latest && latest->tick == game.stepCounter) {
		if ((required & latest->requirements) == required) return latest->project(requested);
		required |= latest->requirements;
	}
	const auto start = std::chrono::steady_clock::now();
	const auto preparedBefore = storage.preparationNs;
	if (needs(required, Component::Catalogs) && (!catalog || catalogConfigurationRevision != game.gameHeader.observationRevision())) {
		catalog = captureCatalog(game);
		catalogConfigurationRevision = game.gameHeader.observationRevision();
	}
	const auto copiedBefore = storage.bytesCopied;
	auto next = capture(game, catalog, required, latest ? &*latest : nullptr, &storage);
	// Map arrays report the chunks they actually copied; other components are
	// accounted by size when not shared with the previous capture.
	metrics.bytesCopied += storage.bytesCopied - copiedBefore;
	auto account = [&](const auto& current, const auto& previous, Uint64 bytes) {
		if (!current) return;
		if (current == previous) ++metrics.reusedComponents;
		else metrics.bytesCopied += bytes;
	};
	const Handle empty;
	const auto& previous = latest ? *latest : empty;
	account(next.terrain, previous.terrain, 0);
	account(next.resources, previous.resources, 0);
	account(next.occupancy, previous.occupancy, 0);
	account(next.areas, previous.areas, 0);
	account(next.visibility, previous.visibility, 0);
	account(next.catalogs, previous.catalogs, 0);
	account(next.rules, previous.rules, 0);
	if(next.growth) {
		const auto sizes=next.growth->storageSizes();
		account(next.growth,previous.growth,(sizes[0]+sizes[1])*sizeof(Uint32)+sizes[2]*sizeof(Uint16));
	}
	if (next.entities) {
		metrics.bytesCopied += next.entities->buildings.size() * sizeof(BuildingView) + next.entities->units.size() * sizeof(UnitView)
			+ next.entities->projects.size() * sizeof(BuildProjectView);
		metrics.bytesCopied += next.entities->relationships.size() * sizeof(UnitRef);
	}
	if (next.teams) {
		metrics.bytesCopied += next.teams->values.size() * sizeof(TeamView);
		for (const auto& team : next.teams->values) metrics.bytesCopied += team.statistics.buildingCountByVariant.size() * sizeof(int)
			+ (team.virtualBuildings.size()+team.swarms.size()) * sizeof(BuildingRef);
	}
	if (next.resourceFields) for (const auto& field : next.resourceFields->planes) {
		const auto* found = previous.resourceFields ? previous.resourceFields->find(field.key) : nullptr;
		if (found && found->values == field.values) ++metrics.reusedComponents;
		else metrics.bytesCopied += field.values->size() * sizeof(Uint16);
	}
	metrics.allocations = storage.allocations;
	const auto preparation = storage.preparationNs-preparedBefore;
	metrics.preparationNs += preparation;
	++metrics.captures;
	metrics.captureNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count()-preparation;
	latest = std::move(next);
	return latest->project(requested);
}

MemoryMetrics Store::memoryMetrics() const
{
	auto value = storage.memoryMetrics();
	memoryPeaks.peakAllocatedBuffers = std::max(memoryPeaks.peakAllocatedBuffers, value.allocatedBuffers);
	memoryPeaks.peakReusableBuffers = std::max(memoryPeaks.peakReusableBuffers, value.reusableBuffers);
	memoryPeaks.peakLeasedBuffers = std::max(memoryPeaks.peakLeasedBuffers, value.leasedBuffers);
	memoryPeaks.peakRetainedBytes = std::max(memoryPeaks.peakRetainedBytes, value.retainedBytes);
	memoryPeaks.peakCapacityBytes = std::max(memoryPeaks.peakCapacityBytes, value.capacityBytes);
	memoryPeaks.peakLeasedBytes = std::max(memoryPeaks.peakLeasedBytes, value.leasedBytes);
	value.peakAllocatedBuffers = memoryPeaks.peakAllocatedBuffers;
	value.peakReusableBuffers = memoryPeaks.peakReusableBuffers;
	value.peakLeasedBuffers = memoryPeaks.peakLeasedBuffers;
	value.peakRetainedBytes = memoryPeaks.peakRetainedBytes;
	value.peakCapacityBytes = memoryPeaks.peakCapacityBytes;
	value.peakLeasedBytes = memoryPeaks.peakLeasedBytes;
	return value;
}
} // namespace SimulationSnapshot
