// SPDX-License-Identifier: GPL-3.0-or-later
#include "SnapshotStorage.h"
#include "TeamStat.h"
namespace SimulationSnapshot
{
MemoryMetrics Storage::memoryMetrics() const
{
	MemoryMetrics result;
	const auto vectorBytes = []<class T>(const std::vector<T>& values) { return Uint64(values.capacity()) * sizeof(T); };
	struct SharedPayload { const void* identity = nullptr; Uint64 object = 0, capacity = 0; bool leased = false; };
	std::array<SharedPayload, (4 + 64) * BufferPool<Catalogs>::Limit> shared{};
	std::size_t sharedCount = 0;
	const auto remember = [&](const auto& owner, bool leased) {
		if (!owner) return;
		for (std::size_t i = 0; i < sharedCount; ++i) if (shared[i].identity == owner.get()) { shared[i].leased |= leased; return; }
		const Uint64 capacity = [&] {
			if constexpr (requires { owner->displayCapacityBytes(); }) return Uint64(owner->displayCapacityBytes());
            else if constexpr (requires { owner->capacityBytes(); }) return Uint64(owner->capacityBytes());
			else return vectorBytes(*owner);
		}();
		shared.at(sharedCount++) = {owner.get(), sizeof(*owner), capacity, leased};
	};
	const auto account = [&]<class T, std::size_t Limit>(const BufferPool<T, Limit>& pool, auto payload) {
		pool.inspect([&](const T& buffer, bool leased) {
			++result.allocatedBuffers;
			if (leased) ++result.leasedBuffers; else ++result.reusableBuffers;
			const Uint64 capacity = payload(buffer, leased);
			result.capacityBytes += capacity;
			result.retainedBytes += sizeof(T) + capacity;
			if (leased) result.leasedBytes += sizeof(T) + capacity;
		});
	};
	account(catalogs, [&](const Catalogs& value, bool leased) { remember(value.buildings, leased); remember(value.capabilities, leased); remember(value.typeDefinitions, leased); return Uint64(value.buildingFingerprint.capacity()); });
    account(session, [&](const Session& value, bool) { return vectorBytes(value.players); });
    account(effects, [&](const Effects& value, bool) {
        Uint64 bytes=vectorBytes(value.sectors);
        for (const auto& sector:value.sectors) bytes+=vectorBytes(sector.bullets)+vectorBytes(sector.explosions)+vectorBytes(sector.deaths);
        return bytes;
    });
    account(statistics, [&](const Statistics& value, bool leased) {
        Uint64 bytes=vectorBytes(value.teams);
        for (const auto& team:value.teams) remember(team,leased);
        return bytes;
    });
    account(telemetry, [&](const Telemetry& value, bool) {
        Uint64 bytes = vectorBytes(value.rows);
        for (const auto& row : value.rows) bytes += vectorBytes(row.fields) + vectorBytes(row.values) + vectorBytes(row.named);
        return bytes;
    });
    account(history, [&](const History& value, bool leased) {
        for (const auto& team : value.teams) remember(team, leased);
        return vectorBytes(value.teams);
    });
    account(entityDiagnostics, [&](const EntityDiagnostics& value, bool) {
        Uint64 bytes=vectorBytes(value.buildings);
        for (const auto& building:value.buildings) {
            bytes+=vectorBytes(building.gradient);
            for (const auto& failed:building.failingUnits) bytes+=vectorBytes(failed);
        }
        return bytes;
    });
    account(annotations, [&](const Annotations& value, bool) { auto bytes = vectorBytes(value.scriptAreas) + vectorBytes(value.areaNames);
        for (const auto& name : value.areaNames) bytes += name.capacity();
        return bytes; });
	account(terrain, [&](const Terrain& value, bool leased) { remember(value.vertices, leased); remember(value.rules, leased); return vectorBytes(value.cellRules); });
	const auto cells = [&](const auto& value, bool) { return vectorBytes(value.cells); };
	account(resources, cells); account(occupancy, cells); account(areas, cells); account(visibility, [&](const Visibility& value, bool) { return vectorBytes(value.discovered) + vectorBytes(value.visible); });
	account(entities, [&](const Entities& value, bool) {
		return vectorBytes(value.buildings) + vectorBytes(value.units) + vectorBytes(value.buildingSlotIndices) + vectorBytes(value.unitSlotIndices) + vectorBytes(value.relationships) + vectorBytes(value.projects);
	});
	account(teams, [&](const Teams& value, bool) {
		Uint64 bytes = vectorBytes(value.values);
		for (const auto& team : value.values) bytes += vectorBytes(team.statistics.buildingCountByVariant)
			+ vectorBytes(team.virtualBuildings) + vectorBytes(team.swarms);
		return bytes;
	});
	account(rules, [&](const Rules& value, bool) { return vectorBytes(value.named) + vectorBytes(value.experiments); });
	account(resourceFields, [&](const ResourceFields& value, bool) { return vectorBytes(value.planes); });
	account(growth, [](const Fertility::GrowthCache& value, bool) {
		const auto capacities = value.storageCapacities();
		return Uint64(capacities[0] + capacities[1]) * sizeof(Uint32) + Uint64(capacities[2]) * sizeof(Uint16);
	});
	account(areaFertility, [&](const BuildingAreaEffects::FertilitySnapshot& value, bool) { return vectorBytes(value.values)+vectorBytes(value.stamps); });
	account(resourcePlanes, [&](const std::vector<Uint16>& value, bool) { return vectorBytes(value); });
	for (std::size_t i = 0; i < sharedCount; ++i) {
		result.capacityBytes += shared[i].capacity;
		result.retainedBytes += shared[i].object + shared[i].capacity;
		if (shared[i].leased) result.leasedBytes += shared[i].object + shared[i].capacity;
	}
	return result;
}
} // namespace SimulationSnapshot
