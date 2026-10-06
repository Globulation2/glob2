// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorldSnapshot.h"
#include <array>

namespace SimulationSnapshot
{
// Seventeen slots cover the largest snapshot consumer horizon: delayed map
// gradients retain up to sixteen ticks, and AI decisions retain up to eight.
// The pool itself owns one reference; every other reference is a consumer lease.
// Storage is reused only after all such leases have gone away.
template<class T> class BufferPool
{
	std::array<std::shared_ptr<T>, 17> buffers;
public:
	template<class Visit> void inspect(Visit&& visit) const
	{
		for (const auto& buffer : buffers) if (buffer) visit(*buffer, buffer.use_count() > 1);
	}
	std::shared_ptr<T> acquire(Uint64& allocations)
	{
		for (auto& buffer : buffers) if (!buffer || buffer.use_count() == 1) {
			if (!buffer) { buffer = std::make_shared<T>(); ++allocations; }
			return buffer;
		}
		throw std::logic_error("snapshot storage exceeded its bounded consumer horizon");
	}
};
// Counts describe component/plane buffer epochs, not whole-world generations.
// Leases include the latest owner snapshot and references from pooled components.
// Bytes include pool objects and enumerated vector capacities, deduplicating shared
// catalog/terrain payloads. Registry/configuration heaps, strings, map nodes,
// allocator overhead and shared_ptr control blocks are outside this accounting.
struct MemoryMetrics
{
	Uint64 allocatedBuffers = 0, reusableBuffers = 0, leasedBuffers = 0;
	Uint64 retainedBytes = 0, capacityBytes = 0, leasedBytes = 0;
	Uint64 peakAllocatedBuffers = 0, peakReusableBuffers = 0, peakLeasedBuffers = 0;
	Uint64 peakRetainedBytes = 0, peakCapacityBytes = 0, peakLeasedBytes = 0;
};
struct Storage
{
	BufferPool<Catalogs> catalogs;
	BufferPool<Terrain> terrain;
	BufferPool<Resources> resources;
	BufferPool<Occupancy> occupancy;
	BufferPool<Areas> areas;
	BufferPool<Visibility> visibility;
	BufferPool<Entities> entities;
	BufferPool<Teams> teams;
	BufferPool<Rules> rules;
	BufferPool<ResourceFields> resourceFields;
	BufferPool<Fertility::GrowthCache> growth;
	std::map<ResourceFieldKey, BufferPool<std::vector<Uint16>>> resourcePlanes;
	Uint64 allocations = 0;
	Uint64 preparationNs = 0;
	MemoryMetrics memoryMetrics() const;
};

inline MemoryMetrics Storage::memoryMetrics() const
{
	MemoryMetrics result;
	const auto vectorBytes = []<class T>(const std::vector<T>& values) { return Uint64(values.capacity()) * sizeof(T); };
	struct SharedPayload { const void* identity = nullptr; Uint64 object = 0, capacity = 0; bool leased = false; };
	std::array<SharedPayload, 34> shared{};
	std::size_t sharedCount = 0;
	const auto remember = [&](const auto& owner, bool leased) {
		if (!owner) return;
		for (std::size_t i = 0; i < sharedCount; ++i) if (shared[i].identity == owner.get()) { shared[i].leased |= leased; return; }
		shared.at(sharedCount++) = {owner.get(), sizeof(*owner), vectorBytes(*owner), leased};
	};
	const auto account = [&]<class T>(const BufferPool<T>& pool, auto payload) {
		pool.inspect([&](const T& buffer, bool leased) {
			++result.allocatedBuffers;
			if (leased) ++result.leasedBuffers; else ++result.reusableBuffers;
			const Uint64 capacity = payload(buffer, leased);
			result.capacityBytes += capacity;
			result.retainedBytes += sizeof(T) + capacity;
			if (leased) result.leasedBytes += sizeof(T) + capacity;
		});
	};
	account(catalogs, [&](const Catalogs& value, bool leased) { remember(value.buildings, leased); return Uint64(0); });
	account(terrain, [&](const Terrain& value, bool leased) { remember(value.identity, leased); return vectorBytes(value.legacy); });
	const auto cells = [&](const auto& value, bool) { return vectorBytes(value.cells); };
	account(resources, cells); account(occupancy, cells); account(areas, cells); account(visibility, cells);
	account(entities, [&](const Entities& value, bool) {
		return vectorBytes(value.buildings) + vectorBytes(value.units) + vectorBytes(value.relationships) + vectorBytes(value.projects);
	});
	account(teams, [&](const Teams& value, bool) {
		Uint64 bytes = vectorBytes(value.values);
		for (const auto& team : value.values) bytes += vectorBytes(team.statistics.buildingCountByVariant)
			+ vectorBytes(team.virtualBuildings) + vectorBytes(team.swarms);
		return bytes;
	});
	account(rules, [&](const Rules& value, bool) { return vectorBytes(value.named) + vectorBytes(value.experiments); });
	account(resourceFields, [](const ResourceFields&, bool) { return Uint64(0); });
	account(growth, [](const Fertility::GrowthCache& value, bool) {
		const auto capacities = value.storageCapacities();
		return Uint64(capacities[0] + capacities[1]) * sizeof(Uint32) + Uint64(capacities[2] + capacities[3]) * sizeof(Uint16);
	});
	for (const auto& [key, pool] : resourcePlanes) account(pool, [&](const auto& value, bool) { return vectorBytes(value); });
	for (std::size_t i = 0; i < sharedCount; ++i) {
		result.capacityBytes += shared[i].capacity;
		result.retainedBytes += shared[i].object + shared[i].capacity;
		if (shared[i].leased) result.leasedBytes += shared[i].object + shared[i].capacity;
	}
	return result;
}
} // namespace SimulationSnapshot
