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
	if (latest && latest->tick == game.stepCounter && latest->observationRevision == observationRevision
        && latest->configurationRevision == game.gameHeader.observationRevision()
        && latest->mapGenerations == game.map.snapshotGenerations()) {
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
	next.observationRevision = observationRevision;
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
			+ next.entities->projects.size() * sizeof(BuildProjectView)
            + (next.entities->buildingSlotIndices.size() + next.entities->unitSlotIndices.size()) * sizeof(Uint32);
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
    if (next.session) metrics.bytesCopied+=sizeof(Session)+next.session->players.size()*sizeof(Session::Player)
        +next.session->legacyScriptText.size();
    if (next.effects) {
        metrics.bytesCopied+=next.effects->sectors.size()*sizeof(SectorEffects);
        for (const auto& sector:next.effects->sectors) metrics.bytesCopied+=sector.bullets.size()*sizeof(BulletRecord)
            +sector.explosions.size()*sizeof(ExplosionRecord)+sector.deaths.size()*sizeof(DeathRecord);
    }
    if (next.statistics) {
        for (size_t i=0;i<next.statistics->teams.size();++i) {
            const auto& team=next.statistics->teams[i];
            if (previous.statistics && i<previous.statistics->teams.size() && team==previous.statistics->teams[i]) ++metrics.reusedComponents;
            else if (team) metrics.bytesCopied+=sizeof(TeamStats)+team->displayCapacityBytes();
        }
    }
    if (next.telemetry) {
        for (const auto& row:next.telemetry->rows) metrics.bytesCopied+=sizeof(row)
            +row.fields.size()*sizeof(AITelemetry::Field)+row.values.size()*sizeof(AITelemetry::Value)
            +row.named.size()*sizeof(AITelemetry::NamedValue);
    }
    if (next.history) {
        for (size_t i = 0; i < next.history->teams.size(); ++i) {
            const auto& team = next.history->teams[i];
            if (previous.history && i < previous.history->teams.size() && team == previous.history->teams[i]) ++metrics.reusedComponents;
            else if (team) metrics.bytesCopied += sizeof(TeamStats) + team->displayCapacityBytes();
        }
    }
    if (next.entityDiagnostics) for (const auto& building:next.entityDiagnostics->buildings) {
        metrics.bytesCopied+=sizeof(building)+building.gradient.size()*sizeof(Uint16);
        for (const auto& failed:building.failingUnits) metrics.bytesCopied+=failed.size()*sizeof(Uint16);
    }
    if (next.annotations && next.annotations != previous.annotations) {
        metrics.bytesCopied += next.annotations->areaNames.size() * sizeof(std::string);
        for (const auto& name : next.annotations->areaNames) metrics.bytesCopied += name.size();
    }
    account(next.annotations,previous.annotations,0); // Stamped array copies counted by storage.
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
